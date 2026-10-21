#!/usr/bin/env python3
"""Recursive-descent function discovery for an original-Xbox XBE (x86-32).

Seeds: entry point, XbSymbolDatabase function addresses, direct call targets,
and (pass 2) code pointers found in data sections (vtables, callbacks).
Reports function counts, instruction mix (x87 / MMX / SSE), indirect control flow,
jump tables, hardware MMIO references, and undiscovered-code gaps.

Usage: python3 -I xbe_funcs.py default.xbe xbsymbols.txt out_dir
"""
import collections
import json
import os
import re
import struct
import sys

import capstone
from capstone import x86

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xbe_info import XBE  # noqa: E402

CODE_SECTIONS = [".text", "D3D", "D3DX", "XGRPH", "XMV", "DSOUND", "XPP"]
DATA_SECTIONS = [".rdata", ".data", "D3D", "D3DX", "XGRPH", "XMV", "DSOUND", "XPP", ".text"]
MMIO = {  # physical/virtual windows the Xbox hardware lives at
    "NV2A GPU (0xFD000000)": (0xFD000000, 0xFE000000),
    "APU audio (0xFE800000)": (0xFE800000, 0xFE880000),
    "AC97 (0xFEC00000)": (0xFEC00000, 0xFEC01000),
    "USB OHCI (0xFED00000)": (0xFED00000, 0xFED09000),
    "Contiguous mem (0x80000000)": (0x80000000, 0x84000000),
    "Flash/BIOS (0xFF000000)": (0xFF000000, 0xFFFFFFFF),
}

X87_GROUP = x86.X86_GRP_FPU if hasattr(x86, "X86_GRP_FPU") else None


def classify_simd(insn):
    """Return 'x87', 'mmx', 'sse', 'sse2', '3dnow' or None from mnemonic + operands."""
    m = insn.mnemonic
    if m.startswith("f") and m not in ("femms",):
        return "x87"
    regs = insn.op_str
    if "xmm" in regs:
        if m.endswith("pd") or m in ("movq2dq", "movdq2q", "movdqa", "movdqu", "cvtdq2ps", "cvtps2dq",
                                     "cvttps2dq", "paddq", "psubq", "pmuludq", "pshufd", "pshufhw",
                                     "pshuflw", "punpcklqdq", "punpckhqdq", "movsd", "cvtsd2ss",
                                     "cvtss2sd", "comisd", "ucomisd"):
            return "sse2"
        return "sse"
    if "mm" in regs and re.search(r"\bmm\d\b", regs):
        return "mmx"
    if m in ("emms", "femms"):
        return "mmx"
    if m.startswith("prefetch") or m in ("sfence", "ldmxcsr", "stmxcsr", "movntq", "maskmovq"):
        return "sse"
    return None


class Analyzer:
    def __init__(self, xbe, symbols):
        self.x = xbe
        self.symbols = symbols  # addr -> (lib, name)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True
        self.ranges = []  # (start, end, file_off, name)
        for s in xbe.sections:
            if s["name"] in CODE_SECTIONS:
                self.ranges.append((s["va"], s["va"] + s["raw_size"], s["raw"], s["name"]))
        self.insns = {}       # addr -> (size, func)
        self.funcs = {}       # start -> info dict
        self.func_src = {}    # start -> how discovered
        self.call_edges = collections.Counter()
        self.stats = collections.Counter()
        self.simd = collections.Counter()
        self.simd_by_sec = collections.defaultdict(collections.Counter)
        self.mmio = collections.Counter()
        self.mmio_sites = collections.defaultdict(list)
        self.jump_tables = []
        self.indirect_calls = 0
        self.indirect_jumps_unresolved = []
        self.io_ports = 0
        self.io_sites = []
        self.bad_decode = []

    def section_of(self, va):
        for a, b, _, n in self.ranges:
            if a <= va < b:
                return n
        return None

    def in_code(self, va):
        return self.section_of(va) is not None

    def read(self, va, n):
        for a, b, off, _ in self.ranges:
            if a <= va < b:
                return self.x.d[off + (va - a): off + min(b - a, va - a + n)]
        o = self.x.va2off(va)
        return self.x.d[o:o + n] if o is not None else b""

    def u32(self, va):
        b = self.read(va, 4)
        return struct.unpack("<I", b)[0] if len(b) == 4 else None

    def decode_one(self, va):
        code = self.read(va, 16)
        for i in self.md.disasm(code, va, 1):
            return i
        return None

    def jump_table(self, insn, block_insns):
        """Resolve `jmp [reg*4 + table]`; returns list of targets."""
        op = insn.operands[0]
        if op.type != x86.X86_OP_MEM or op.mem.scale != 4 or op.mem.index == 0 or op.mem.base != 0:
            return None
        table = op.mem.disp & 0xFFFFFFFF
        idx_reg = insn.reg_name(op.mem.index)
        bound = None
        # look back for `cmp idx, imm` + `ja` inside this block
        for prev in reversed(block_insns[-8:]):
            if prev.mnemonic == "cmp" and len(prev.operands) == 2 and prev.operands[1].type == x86.X86_OP_IMM:
                if prev.operands[0].type == x86.X86_OP_REG and prev.reg_name(prev.operands[0].reg) == idx_reg:
                    bound = prev.operands[1].imm + 1
                    break
        limit = bound if bound and 0 < bound <= 4096 else 512
        targets = []
        for k in range(limit):
            t = self.u32(table + 4 * k)
            if t is None or not self.in_code(t):
                break
            if bound is None and (table + 4 * k) in self.insns:
                break
            targets.append(t)
        self.jump_tables.append({"at": insn.address, "table": table, "entries": len(targets),
                                 "bounded": bound is not None})
        return targets

    def explore(self, start, src):
        if start in self.funcs or not self.in_code(start):
            return []
        if start in self.insns and self.insns[start][1] != start:
            # Lands inside an existing function's body: record as an extra entry, don't re-walk.
            self.func_src.setdefault(start, src)
            self.funcs[start] = {"size": 0, "insns": 0, "blocks": 0, "inner_entry": True}
            return []
        self.func_src[start] = src
        new_calls = []
        work = [start]
        seen_blocks = set()
        n_insn = 0
        lo, hi = start, start
        while work:
            b = work.pop()
            if b in seen_blocks:
                continue
            seen_blocks.add(b)
            if b in self.insns:
                if self.insns[b][1] != start:
                    self.stats["cross_function_jumps"] += 1
                continue
            va = b
            block = []
            while True:
                if va in self.insns and va != b:
                    break  # fell into already-decoded code (this or another function)
                insn = self.decode_one(va)
                if insn is None:
                    self.bad_decode.append(va)
                    break
                self.insns[va] = (insn.size, start)
                n_insn += 1
                block.append(insn)
                lo, hi = min(lo, va), max(hi, va + insn.size)
                sec = self.section_of(va)
                kind = classify_simd(insn)
                if kind:
                    self.simd[kind] += 1
                    self.simd_by_sec[sec][kind] += 1
                if insn.mnemonic in ("in", "out", "insb", "insd", "outsb", "outsd", "insw", "outsw"):
                    self.io_ports += 1
                    self.io_sites.append((va, sec, src))
                for op in insn.operands:
                    val, is_imm = None, False
                    if op.type == x86.X86_OP_IMM:
                        val, is_imm = op.imm & 0xFFFFFFFF, True
                    elif op.type == x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                        val = op.mem.disp & 0xFFFFFFFF
                    if val is not None and val >= 0x80000000:
                        for name, (a, z) in MMIO.items():
                            if is_imm and not name.startswith(("NV2A", "APU", "USB", "AC97")):
                                continue  # negative ints / floats look like these windows
                            if a <= val < z:
                                self.mmio[name] += 1
                                if len(self.mmio_sites[name]) < 50:
                                    self.mmio_sites[name].append((va, sec))
                m = insn.mnemonic
                nxt = va + insn.size
                if m == "call":
                    op = insn.operands[0]
                    if op.type == x86.X86_OP_IMM:
                        t = op.imm & 0xFFFFFFFF
                        self.call_edges[(start, t)] += 1
                        new_calls.append(t)
                    else:
                        self.indirect_calls += 1
                    va = nxt
                    continue
                if m in ("ret", "retf", "iretd", "hlt", "int3", "ud2"):
                    break
                if m == "jmp":
                    op = insn.operands[0]
                    if op.type == x86.X86_OP_IMM:
                        t = op.imm & 0xFFFFFFFF
                        if t in self.funcs and t != start:
                            self.stats["tail_calls"] += 1
                        else:
                            work.append(t)
                    else:
                        tg = self.jump_table(insn, block)
                        if tg:
                            work.extend(tg)
                        else:
                            if op.type == x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                                self.stats["import_style_jumps"] += 1
                            else:
                                self.indirect_jumps_unresolved.append(va)
                    break
                if m.startswith("j") or m in ("loop", "loope", "loopne", "jecxz", "jcxz"):
                    op = insn.operands[0]
                    if op.type == x86.X86_OP_IMM:
                        work.append(op.imm & 0xFFFFFFFF)
                    va = nxt
                    continue
                va = nxt
        self.funcs[start] = {"size": hi - lo, "insns": n_insn, "blocks": len(seen_blocks)}
        return new_calls

    def run_queue(self, seeds, src):
        q = collections.deque(seeds)
        while q:
            f = q.popleft()
            if f in self.funcs:
                continue
            for t in self.explore(f, src):
                if t not in self.funcs:
                    q.append(t)

    def scan_data_pointers(self):
        """Code pointers stored in data (vtables, callback tables)."""
        cands = collections.Counter()
        for s in self.x.sections:
            if s["name"] not in DATA_SECTIONS:
                continue
            d = self.x.d[s["raw"]: s["raw"] + s["raw_size"]]
            for k in range(0, len(d) - 3, 4):
                v = struct.unpack_from("<I", d, k)[0]
                if self.in_code(v) and v not in self.insns:
                    cands[v] += 1
        good = []
        for v in cands:
            # plausibility: previous byte is padding/ret, and first insns decode
            prev = self.read(v - 1, 1)
            if not prev:
                continue
            # This build pads functions with 0xCC to 16-byte boundaries (93% of call-graph functions).
            if not (prev[0] == 0xCC or (v % 16 == 0 and prev[0] in (0xC3, 0x90, 0x00))):
                continue
            ok = True
            va = v
            for _ in range(4):
                i = self.decode_one(va)
                if (i is None or i.mnemonic in ("int3", "hlt", "in", "out", "insb", "insd", "outsb", "outsd",
                                                "arpl", "into", "lock", "bound", "les", "lds")
                        or i.bytes[:2] == b"\x00\x00" or va in self.insns):
                    ok = False
                    break
                if i.mnemonic in ("ret", "jmp"):
                    break
                va += i.size
            if ok:
                good.append(v)
        return good

    def gaps(self):
        res = {}
        for a, b, off, name in self.ranges:
            covered = 0
            pad = 0
            unknown_runs = []
            va = a
            run_start = None
            while va < b:
                if va in self.insns:
                    if run_start is not None:
                        unknown_runs.append((run_start, va))
                        run_start = None
                    covered += self.insns[va][0]
                    va += self.insns[va][0]
                    continue
                byte = self.x.d[off + (va - a)]
                if byte in (0xCC, 0x90, 0x00):
                    pad += 1
                    if run_start is not None:
                        unknown_runs.append((run_start, va))
                        run_start = None
                else:
                    if run_start is None:
                        run_start = va
                va += 1
            if run_start is not None:
                unknown_runs.append((run_start, b))
            unk = sum(e - s for s, e in unknown_runs)
            big = sorted(unknown_runs, key=lambda r: r[0] - r[1])[:10]
            res[name] = {"size": b - a, "covered": covered, "padding": pad, "unknown": unk,
                         "largest_unknown": [(hex(s), e - s) for s, e in big]}
        return res


def load_symbols(path):
    syms = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"([A-Z0-9]+)__FUN__\w+?__(\w+)\(.*= (0x[0-9a-fA-F]+)", line)
        if m:
            syms[int(m.group(3), 16)] = (m.group(1), m.group(2))
    return syms


def main():
    xbe_path, sym_path, out_dir = sys.argv[1:4]
    x = XBE(open(xbe_path, "rb").read()).parse()
    syms = load_symbols(sym_path)
    a = Analyzer(x, syms)

    a.run_queue([x.h["entry"]], "entry")
    a.run_queue(sorted(syms), "xdk-symbol")
    n_pass1 = len(a.funcs)
    for rnd in range(4):
        ptrs = a.scan_data_pointers()
        if not ptrs:
            break
        a.run_queue(ptrs, "data-pointer")
    gaps = a.gaps()

    by_sec = collections.Counter()
    insn_by_sec = collections.Counter()
    for f, info in a.funcs.items():
        by_sec[a.section_of(f)] += 1
        insn_by_sec[a.section_of(f)] += info["insns"]
    src = collections.Counter(a.func_src.values())
    total_insns = sum(i["insns"] for i in a.funcs.values())
    sizes = sorted((i["size"], f) for f, i in a.funcs.items())

    # Which XDK functions does game code (.text) call, and how often?
    xdk_calls = collections.Counter()
    for (caller, callee), n in a.call_edges.items():
        if callee in syms and a.section_of(caller) == ".text":
            xdk_calls["%s:%s" % syms[callee]] += n

    report = {
        "functions_total": len(a.funcs),
        "functions_after_call_graph": n_pass1,
        "functions_by_discovery": dict(src),
        "functions_by_section": dict(by_sec),
        "instructions_total": total_insns,
        "instructions_by_section": dict(insn_by_sec),
        "simd": dict(a.simd),
        "simd_by_section": {k: dict(v) for k, v in a.simd_by_sec.items()},
        "indirect_calls": a.indirect_calls,
        "unresolved_indirect_jumps": len(a.indirect_jumps_unresolved),
        "jump_tables": len(a.jump_tables),
        "jump_table_entries": sum(j["entries"] for j in a.jump_tables),
        "jump_tables_unbounded": sum(1 for j in a.jump_tables if not j["bounded"]),
        "tail_calls": a.stats["tail_calls"],
        "io_port_insns": a.io_ports,
        "io_port_by_section_source": collections.Counter("%s/%s" % (sec, sr) for _, sec, sr in a.io_sites),
        "mmio_refs": dict(a.mmio),
        "mmio_sites": {k: [(hex(v), s) for v, s in vs[:15]] for k, vs in a.mmio_sites.items()},
        "decode_failures": len(a.bad_decode),
        "largest_functions": [(hex(f), s) for s, f in sizes[-10:]],
        "coverage": gaps,
        "game_calls_to_xdk_top": xdk_calls.most_common(60),
        "game_calls_to_xdk_distinct": len(xdk_calls),
    }
    os.makedirs(out_dir, exist_ok=True)
    json.dump(report, open(os.path.join(out_dir, "func_report.json"), "w"), indent=1)
    with open(os.path.join(out_dir, "functions.csv"), "w") as fh:
        fh.write("address,section,size,instructions,blocks,discovered_by,xdk_symbol\n")
        for f in sorted(a.funcs):
            i = a.funcs[f]
            s = syms.get(f)
            fh.write("0x%08X,%s,%d,%d,%d,%s,%s\n" % (f, a.section_of(f), i["size"], i["insns"], i["blocks"],
                                                    a.func_src.get(f), "%s:%s" % s if s else ""))
    print(json.dumps({k: v for k, v in report.items() if k not in ("coverage", "mmio_sites", "game_calls_to_xdk_top")}, indent=1))
    print("coverage:")
    for k, v in gaps.items():
        print("  %-7s size=%8d covered=%8d (%.1f%%) padding=%7d unknown=%7d" % (
            k, v["size"], v["covered"], 100.0 * v["covered"] / v["size"], v["padding"], v["unknown"]))


if __name__ == "__main__":
    main()
