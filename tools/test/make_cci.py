#!/usr/bin/env python3
"""make_cci.py <in.iso> <out.cci> [--split BYTES] [--hc]

A CCI of an image, as XboxToolkit's ContainerUtility.ConvertContainerToCCI (Repackinator) writes one:
  header: "CCIM", header size 32, uncompressed size (u64), index offset (u64), block size 2048, version 1,
          index alignment 2, 2 bytes unused
  each sector: LZ4 compressed when the block is under 2048 - (4 + 4) bytes, as a byte with the padding count, the
          block and that many zeros to a multiple of 4; else the 2048 bytes as they are
  index after the data: per sector (position >> 2) | 0x80000000 when compressed, then the end >> 2
--split starts a new file before one would pass BYTES (each a whole CCI), named name.1.cci, name.2.cci, ...
--hc compresses at LZ4 level 12 as XboxToolkit does (slow); by default the fast mode, which reads the same.
"""
import os
import struct
import sys

import lz4.block

SECTOR = 2048
ALIGN = 2


def main():
    args = sys.argv[1:]
    hc = "--hc" in args
    split = int(args[args.index("--split") + 1]) if "--split" in args else 0
    src, dst = [a for a in args if not a.startswith("--") and not a.isdigit()][:2]
    total = os.path.getsize(src) // SECTOR
    base, ext = os.path.splitext(dst)
    inp = open(src, "rb")
    sector, part, names = 0, 0, []
    stats = [0, 0]
    while sector < total:
        name = f"{base}.{part + 1}{ext}" if split else dst
        names.append(name)
        out = open(name, "wb")
        out.write(struct.pack("<IIQQIBBH", 0x4D494343, 32, 0, 0, SECTOR, 1, ALIGN, 0))
        sizes, flags, pos = [], [], 32
        inp.seek(sector * SECTOR)
        while sector < total:
            data = inp.read(SECTOR)
            if hc:
                comp = lz4.block.compress(data, mode="high_compression", compression=12, store_size=False)
            else:
                comp = lz4.block.compress(data, store_size=False)
            if 0 < len(comp) < SECTOR - (4 + (1 << ALIGN)):
                m = 1 << ALIGN
                padding = (len(comp) + 1 + m - 1) // m * m - (len(comp) + 1)
                block = bytes([padding]) + comp + b"\0" * padding
                compressed = True
            else:
                block = data
                compressed = False
            if split and pos + len(sizes) * 8 + len(block) > split:
                break
            out.write(block)
            sizes.append(len(block))
            flags.append(compressed)
            stats[compressed] += 1
            pos += len(block)
            sector += 1
        index_offset = pos
        at = 32
        for s, c in zip(sizes, flags):
            out.write(struct.pack("<I", (at >> ALIGN) | (0x80000000 if c else 0)))
            at += s
        out.write(struct.pack("<I", at >> ALIGN))
        out.seek(8)
        out.write(struct.pack("<QQ", len(sizes) * SECTOR, index_offset))
        out.close()
        part += 1
    print(f"{', '.join(os.path.basename(n) for n in names)}: {total} sectors, {stats[1]} compressed, {stats[0]} stored")


if __name__ == "__main__":
    main()
