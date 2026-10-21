#!/usr/bin/env python3
"""Parse an original-Xbox XBE: header, certificate, sections, kernel imports, XDK library versions.

Usage: python3 -I xbe_info.py path/to/default.xbe [kernel.def] [--json out.json]
"""
import json
import re
import struct
import sys

RETAIL_EP_KEY, DEBUG_EP_KEY = 0xA8FC57AB, 0x94859D4B
RETAIL_KT_KEY, DEBUG_KT_KEY = 0x5B6D40B6, 0xEFB1F152

REGIONS = {0x1: "North America", 0x2: "Japan", 0x4: "Rest of world (PAL)", 0x80000000: "Manufacturing"}
MEDIA = {
    0x1: "HDD", 0x2: "DVD X2", 0x4: "DVD/CD", 0x8: "CD", 0x10: "DVD-5 RO", 0x20: "DVD-9 RO",
    0x40: "DVD-5 RW", 0x80: "DVD-9 RW", 0x100: "Dongle", 0x200: "Media board", 0x40000000: "Nonsecure HDD",
    0x80000000: "Nonsecure mode",
}
SEC_FLAGS = {0x1: "W", 0x2: "preload", 0x4: "X", 0x8: "insertfile", 0x10: "head-ro", 0x20: "tail-ro"}


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def load_kernel_names(path):
    names = {}
    if not path:
        return names
    for line in open(path, encoding="utf-8"):
        m = re.match(r"\s*@?([A-Za-z_][\w]*)(?:@\d+)?\s+@\s*(\d+)", line)
        if m:
            names[int(m.group(2))] = m.group(1)
    return names


class XBE:
    def __init__(self, data):
        self.d = data
        if data[:4] != b"XBEH":
            raise ValueError("not an XBE (missing XBEH magic)")
        self.base = u32(data, 0x104)

    def va2off(self, va):
        """Map a virtual address to a file offset (header area or section raw data)."""
        if self.base <= va < self.base + u32(self.d, 0x108):
            return va - self.base
        for s in self.sections:
            if s["va"] <= va < s["va"] + s["raw_size"]:
                return s["raw"] + (va - s["va"])
        return None

    def cstr(self, va, limit=256):
        o = self.va2off(va) if va else None
        if o is None:
            return None
        end = self.d.find(b"\0", o, o + limit)
        return self.d[o:end].decode("latin-1")

    def parse(self):
        d = self.d
        h = {
            "base": self.base,
            "size_headers": u32(d, 0x108),
            "size_image": u32(d, 0x10C),
            "timedate": u32(d, 0x114),
            "cert_addr": u32(d, 0x118),
            "num_sections": u32(d, 0x11C),
            "sections_addr": u32(d, 0x120),
            "init_flags": u32(d, 0x124),
            "entry_enc": u32(d, 0x128),
            "tls_addr": u32(d, 0x12C),
            "stack_commit": u32(d, 0x130),
            "heap_reserve": u32(d, 0x134),
            "heap_commit": u32(d, 0x138),
            "pe_base": u32(d, 0x13C),
            "pe_size": u32(d, 0x140),
            "debug_path_addr": u32(d, 0x14C),
            "debug_file_addr": u32(d, 0x150),
            "kthunk_enc": u32(d, 0x158),
            "nonkernel_import_dir": u32(d, 0x15C),
            "num_libs": u32(d, 0x160),
            "libs_addr": u32(d, 0x164),
            "kernel_lib_addr": u32(d, 0x168),
            "xapi_lib_addr": u32(d, 0x16C),
            "logo_addr": u32(d, 0x170),
            "logo_size": u32(d, 0x174),
        }
        # Decide retail vs debug by which key yields a plausible entry point.
        ep_r, ep_d = h["entry_enc"] ^ RETAIL_EP_KEY, h["entry_enc"] ^ DEBUG_EP_KEY
        lo, hi = self.base, self.base + h["size_image"]
        if lo <= ep_r < hi:
            h["build"], h["entry"], h["kthunk"] = "retail", ep_r, h["kthunk_enc"] ^ RETAIL_KT_KEY
        elif lo <= ep_d < hi:
            h["build"], h["entry"], h["kthunk"] = "debug", ep_d, h["kthunk_enc"] ^ DEBUG_KT_KEY
        else:
            h["build"], h["entry"], h["kthunk"] = "unknown", None, None
        self.h = h

        # Sections
        self.sections = []
        so = h["sections_addr"] - self.base
        for i in range(h["num_sections"]):
            o = so + i * 56
            flags, va, vsz, raw, rsz, name_addr = struct.unpack_from("<6I", d, o)
            self.sections.append({"flags": flags, "va": va, "vsize": vsz, "raw": raw,
                                  "raw_size": rsz, "name_addr": name_addr})
        for s in self.sections:
            s["name"] = self.cstr(s["name_addr"], 16)
            s["flag_str"] = ",".join(v for k, v in SEC_FLAGS.items() if s["flags"] & k)

        # Certificate
        co = h["cert_addr"] - self.base
        title = d[co + 0x0C: co + 0x0C + 80].decode("utf-16-le").split("\0")[0]
        cert = {
            "size": u32(d, co),
            "timedate": u32(d, co + 4),
            "title_id": u32(d, co + 8),
            "title": title,
            "media": u32(d, co + 0x9C),
            "region": u32(d, co + 0xA0),
            "ratings": u32(d, co + 0xA4),
            "disk": u32(d, co + 0xA8),
            "version": u32(d, co + 0xAC),
        }
        tid = cert["title_id"]
        cert["title_id_str"] = "%c%c-%03d" % ((tid >> 24) & 0xFF, (tid >> 16) & 0xFF, tid & 0xFFFF)
        self.cert = cert

        # Library versions
        self.libs = []
        lo_ = h["libs_addr"] - self.base
        for i in range(h["num_libs"]):
            o = lo_ + i * 16
            name = d[o:o + 8].split(b"\0")[0].decode("latin-1")
            major, minor, build, flags = struct.unpack_from("<4H", d, o + 8)
            self.libs.append({"name": name, "version": "%d.%d.%d" % (major, minor, build),
                              "qfe": flags & 0x1FFF, "approved": (flags >> 13) & 3, "debug": bool(flags >> 15)})

        # Kernel thunk table
        self.kimports = []
        if h["kthunk"]:
            o = self.va2off(h["kthunk"])
            while o is not None:
                v = u32(d, o)
                if v == 0:
                    break
                self.kimports.append(v & 0x7FFFFFFF if v & 0x80000000 else v)
                o += 4

        # TLS
        self.tls = None
        if h["tls_addr"]:
            o = self.va2off(h["tls_addr"])
            if o is not None:
                ds, de, idx, cb, zf, ch = struct.unpack_from("<6I", d, o)
                self.tls = {"data_start": ds, "data_end": de, "index_addr": idx, "callbacks": cb,
                            "zero_fill": zf, "characteristics": ch}
        return self


def fmt_time(t):
    import datetime
    return datetime.datetime.fromtimestamp(t, datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out_json = None
    if "--json" in sys.argv:
        out_json = sys.argv[sys.argv.index("--json") + 1]
        args = [a for a in args if a != out_json]
    path = args[0]
    kdef = args[1] if len(args) > 1 else None
    knames = load_kernel_names(kdef)
    x = XBE(open(path, "rb").read()).parse()
    h, c = x.h, x.cert

    print("== Header ==")
    print(f"  Build type     : {h['build']}")
    print(f"  Base address   : 0x{h['base']:08X}")
    print(f"  Image size     : 0x{h['size_image']:X} ({h['size_image']/1048576:.2f} MB)")
    print(f"  Link time      : {fmt_time(h['timedate'])}")
    print(f"  Entry point    : 0x{h['entry']:08X}" if h["entry"] else "  Entry point    : ?")
    print(f"  Kernel thunk   : 0x{h['kthunk']:08X}" if h["kthunk"] else "  Kernel thunk   : ?")
    print(f"  Init flags     : 0x{h['init_flags']:08X}")
    print(f"  Stack commit   : 0x{h['stack_commit']:X}")
    print(f"  Debug path     : {x.cstr(h['debug_path_addr'])}")
    print(f"  Non-kernel imp : 0x{h['nonkernel_import_dir']:08X}")
    print("== Certificate ==")
    print(f"  Title          : {c['title']}")
    print(f"  Title ID       : 0x{c['title_id']:08X} ({c['title_id_str']})")
    print(f"  Region         : 0x{c['region']:08X} "
          f"({', '.join(v for k, v in REGIONS.items() if c['region'] & k) or 'none'})")
    print(f"  Media          : {', '.join(v for k, v in MEDIA.items() if c['media'] & k)}")
    print(f"  Version        : 0x{c['version']:08X}   Disk: {c['disk']}   Ratings: 0x{c['ratings']:X}")
    print(f"  Cert time      : {fmt_time(c['timedate'])}")
    print("== Sections ==")
    print(f"  {'name':<10} {'VA':>10} {'vsize':>9} {'raw':>9} {'rawsize':>9}  flags")
    for s in x.sections:
        print(f"  {s['name'] or '?':<10} 0x{s['va']:08X} {s['vsize']:>9X} {s['raw']:>9X} {s['raw_size']:>9X}  {s['flag_str']}")
    print("== XDK libraries ==")
    for l in x.libs:
        print(f"  {l['name']:<8} {l['version']:<12} qfe={l['qfe']} approved={l['approved']} debug={l['debug']}")
    print(f"== Kernel imports ({len(x.kimports)}) ==")
    for o in x.kimports:
        print(f"  {o:4d}  {knames.get(o, '?')}")
    if x.tls:
        print("== TLS ==")
        print("  " + ", ".join(f"{k}=0x{v:X}" for k, v in x.tls.items()))

    if out_json:
        json.dump({"header": h, "cert": c, "sections": x.sections, "libs": x.libs,
                   "kernel_imports": [{"ordinal": o, "name": knames.get(o)} for o in x.kimports],
                   "tls": x.tls}, open(out_json, "w"), indent=1)


if __name__ == "__main__":
    main()
