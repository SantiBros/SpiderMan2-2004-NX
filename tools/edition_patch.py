#!/usr/bin/env python3
"""edition_patch.py: another edition's default.xbe made into the supported one, for the editions built apart.

    python3 tools/edition_patch.py make  <edition default.xbe> <supported default.xbe> <out.patch> <name>
                                         (also <out.patch>.locked, for the installer page)
    python3 tools/edition_patch.py apply <edition default.xbe> <in.patch> <out default.xbe>

The port is recompiled from one default.xbe (the US edition's, version 4). The European French, German and Spanish
edition's is another build of it (version 5, a few days later), so its code is not the one the port runs. Its disc's
data is the US disc's but for game.ini, the voices and the 50 Hz movies, which the port takes from it as they are; so
the port runs it with the supported default.xbe, made from the edition's own by this patch. It does nothing without
the edition's default.xbe, and is only published locked with that xbe's game code (tools/lock_nro.py), as the .nro
is; the .nro carries it inside (switch/build.sh).

The patch: a 128 byte header, then an LZ4 block (no frame). The header: "SM2NXPD1", the edition xbe's size and the
supported one's (4 bytes each, little endian), the edition xbe's SHA-1 and the supported one's (20 bytes each), the
LZ4 block's size once decompressed, the number of steps (4 bytes each), the edition's name (UTF-8, up to 64 bytes,
NUL padded). The block: the steps (three 32-bit little endian numbers each: x, y, z), then the added bytes, then the
new bytes. Each step, as bsdiff's: x bytes, each the edition's next byte plus the next added byte (modulo 256), then y
new bytes, then the place in the edition's xbe moved by z. Made with bsdiff4; read by sdk/runtime/xbe/patch.h and
installer/index.html.
"""
import hashlib
import os
import struct
import sys

MAGIC = b"SM2NXPD1"


def make(edition_path, supported_path, out_path, name):
    import bsdiff4.core
    import bsdiff4.format
    import lz4.block

    src = open(edition_path, "rb").read()
    dst = open(supported_path, "rb").read()
    control, added, new = bsdiff4.core.diff(src, dst)
    steps = list(bsdiff4.format.unpack_control_block(control))
    body = b"".join(struct.pack("<iii", x, y, z) for x, y, z in steps) + added + new
    packed = lz4.block.compress(body, mode="high_compression", compression=12, store_size=False)
    label = name.encode("utf-8")[:64].ljust(64, b"\0")
    header = MAGIC + struct.pack("<II", len(src), len(dst)) + hashlib.sha1(src).digest() + hashlib.sha1(dst).digest()
    header += struct.pack("<II", len(body), len(steps)) + label
    assert len(header) == 128
    open(out_path, "wb").write(header + packed)
    if apply_bytes(src, header + packed) != dst:
        sys.exit("the patch does not make the supported default.xbe")
    # and locked with the edition's game code, for the installer page (tools/make_installer_site.sh)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import lock_nro
    open(out_path + ".locked", "wb").write(lock_nro.lock_bytes(header + packed, src, 2))
    print("%s (and .locked): %s, %d steps, %d bytes" % (out_path, name, len(steps), len(header) + len(packed)))


def apply_bytes(src, patch):
    import lz4.block

    if patch[:8] != MAGIC or len(patch) < 128:
        raise ValueError("not an edition patch")
    src_size, dst_size = struct.unpack_from("<II", patch, 8)
    src_sha1, dst_sha1 = patch[16:36], patch[36:56]
    body_size, count = struct.unpack_from("<II", patch, 56)
    if len(src) != src_size or hashlib.sha1(src).digest() != src_sha1:
        raise ValueError("this default.xbe is not the edition the patch is for")
    body = lz4.block.decompress(patch[128:], uncompressed_size=body_size)
    steps = struct.unpack_from("<%di" % (3 * count), body, 0)
    added_at = 12 * count
    new_at = added_at + sum(steps[0::3])
    out = bytearray()
    at = 0
    for i in range(count):
        x, y, z = steps[3 * i:3 * i + 3]
        out += bytes((a + b) & 0xFF for a, b in zip(body[added_at:added_at + x], src[at:at + x]))
        added_at += x
        at += x
        out += body[new_at:new_at + y]
        new_at += y
        at += z
    if len(out) != dst_size or hashlib.sha1(out).digest() != dst_sha1:
        raise ValueError("the patch did not make the supported default.xbe")
    return bytes(out)


if __name__ == "__main__":
    if len(sys.argv) == 6 and sys.argv[1] == "make":
        make(*sys.argv[2:])
    elif len(sys.argv) == 5 and sys.argv[1] == "apply":
        out = apply_bytes(open(sys.argv[2], "rb").read(), open(sys.argv[3], "rb").read())
        open(sys.argv[4], "wb").write(out)
        print("%s: the supported default.xbe, SHA-1 %s" % (sys.argv[4], hashlib.sha1(out).hexdigest()))
    else:
        sys.exit(__doc__)
