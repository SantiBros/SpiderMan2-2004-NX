#!/usr/bin/env python3
"""Disassemble XBE functions: python3 -I dis.py default.xbe addr[,maxinsns] ..."""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xbe_info import XBE
import capstone
x = XBE(open(sys.argv[1], 'rb').read()).parse()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
syms = {}
symfile = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'out', 'xbsymbols.txt')
if os.path.exists(symfile):
    for line in open(symfile):
        m = re.match(r"([A-Z0-9]+)__(?:FUN|VAR)__(?:\w+?__)?(\w+?)(?:\(.*)?\s*= (0x[0-9a-fA-F]+)", line)
        if m: syms[int(m.group(3), 16)] = m.group(2)
for arg in sys.argv[2:]:
    a, _, n = arg.partition(',')
    va, n = int(a, 16), int(n or 80)
    print('==== %08X %s' % (va, syms.get(va, '')))
    o = x.va2off(va)
    for i, ins in enumerate(md.disasm(x.d[o:o + 0x1000], va)):
        if i >= n: break
        note = ''
        for h in re.findall(r'0x[0-9a-f]{5,8}', ins.op_str):
            if int(h, 16) in syms: note += '  ; ' + syms[int(h, 16)]
        print('%08X  %-7s %s%s' % (ins.address, ins.mnemonic, ins.op_str, note))
        if ins.mnemonic == 'ret': break
