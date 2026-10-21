#!/usr/bin/env python3
"""make_xiso.py <folder> <out.iso> [--whole-disc] [--split BYTES]

An Xbox DVD file system image (XDVDFS) of a folder, for testing the port's disc image reader:
  - sectors 0-31 empty, the volume descriptor at sector 32 ("MICROSOFT*XBOX*MEDIA", root table sector and size,
    FILETIME, the magic again at +0x7EC), then the folder tables and the files, each from a sector of its own
  - a folder table is a binary tree of entries ordered by upper case name, its root at offset 0: +0 left and +2
    right (offset / 4, 0 for none), +4 sector, +8 size, +0xC attributes (0x10 folder, 0x20 file), +0xD name
    length, +0xE name, padded to 4 bytes with 0xFF; no entry crosses a sector, the rest of a sector is 0xFF
--whole-disc puts the image behind an empty 0x18300000-byte video partition, as a full dump of an original Xbox
disc has it (written sparse). --split writes name.1.iso, name.2.iso, ... of at most BYTES each (a multiple of 2048).
"""
import os
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
VIDEO = 0x18300000


def entry_size(name):
    return (0xE + len(name) + 3) & ~3


class Node:
    def __init__(self, path, name):
        self.path, self.name = path, name
        self.is_dir = os.path.isdir(path)
        self.children = []
        self.size = 0 if self.is_dir else os.path.getsize(path)
        self.sector = 0
        self.table = b""


def scan(path, name=b""):
    n = Node(path, name)
    if n.is_dir:
        for c in sorted(os.listdir(path)):
            n.children.append(scan(os.path.join(path, c), c.encode("ascii")))
    return n


def balanced(items):
    """the tree's nodes in the order they are written (root first), with their children"""
    if not items:
        return None
    mid = len(items) // 2
    return (items[mid], balanced(items[:mid]), balanced(items[mid + 1:]))


def build_table(node):
    """the folder's entry table, with every child's sector already set"""
    kids = sorted(node.children, key=lambda c: c.name.upper())
    if not kids:
        return b"\xff" * SECTOR
    tree = balanced(kids)
    order = []

    def walk(t):
        if t:
            order.append(t)
            walk(t[1])
            walk(t[2])
    walk(tree)
    # offsets: entries in order, none crossing a sector
    offs, at = {}, 0
    for t in order:
        size = entry_size(t[0].name)
        if at // SECTOR != (at + size - 1) // SECTOR:
            at = (at // SECTOR + 1) * SECTOR
        offs[id(t)] = at
        at += size
    table = bytearray(b"\xff" * ((at + SECTOR - 1) // SECTOR * SECTOR))
    for t in order:
        c = t[0]
        left = offs[id(t[1])] // 4 if t[1] else 0
        right = offs[id(t[2])] // 4 if t[2] else 0
        size = c.table_size if c.is_dir else c.size
        e = struct.pack("<HHIIBB", left, right, c.sector, size, 0x10 if c.is_dir else 0x20, len(c.name)) + c.name
        o = offs[id(t)]
        table[o:o + len(e)] = e
    return bytes(table)


def layout(root):
    """sectors for every table and file: tables first, breadth first, then the files"""
    nodes, queue = [], [root]
    while queue:
        n = queue.pop(0)
        nodes.append(n)
        queue.extend(n.children)
    dirs = [n for n in nodes if n.is_dir]
    # a table's size depends only on its children's names
    for d in dirs:
        d.table_size = len(build_table_sizes(d))
    sector = 33
    for d in dirs:
        d.sector = sector
        sector += d.table_size // SECTOR
    for n in nodes:
        if not n.is_dir:
            n.sector = sector if n.size else 0
            sector += (n.size + SECTOR - 1) // SECTOR
    for d in dirs:
        d.table = build_table(d)
        assert len(d.table) == d.table_size
    return dirs, [n for n in nodes if not n.is_dir], sector


def build_table_sizes(node):
    # the same packing as build_table, without sectors
    kids = sorted(node.children, key=lambda c: c.name.upper())
    if not kids:
        return b"\xff" * SECTOR
    at = 0
    order = []

    def walk(t):
        if t:
            order.append(t)
            walk(t[1])
            walk(t[2])
    walk(balanced(kids))
    for t in order:
        size = entry_size(t[0].name)
        if at // SECTOR != (at + size - 1) // SECTOR:
            at = (at // SECTOR + 1) * SECTOR
        at += size
    return b"\0" * ((at + SECTOR - 1) // SECTOR * SECTOR)


class Out:
    """a sparse file, or parts of at most split bytes"""
    def __init__(self, path, split):
        self.path, self.split, self.files = path, split, {}

    def part(self, i):
        if i not in self.files:
            if self.split:
                base, ext = os.path.splitext(self.path)
                name = f"{base}.{i + 1}{ext}"
            else:
                name = self.path
            self.files[i] = open(name, "wb")
        return self.files[i]

    def write(self, at, data):
        while data:
            i = at // self.split if self.split else 0
            within = at - i * self.split if self.split else at
            room = self.split - within if self.split else len(data)
            f = self.part(i)
            f.seek(within)
            f.write(data[:room])
            data, at = data[room:], at + min(room, len(data))

    def close(self, total):
        # every part to its full length (a sparse end where nothing was written)
        if self.split:
            for i in range((total + self.split - 1) // self.split):
                f = self.part(i)
                f.truncate(min(self.split, total - i * self.split))
        else:
            self.part(0).truncate(total)
        for f in self.files.values():
            f.close()


def main():
    args = sys.argv[1:]
    whole = "--whole-disc" in args
    split = 0
    if "--split" in args:
        split = int(args[args.index("--split") + 1])
        assert split % SECTOR == 0
    src, dst = [a for a in args if not a.startswith("--") and not a.isdigit()][:2]
    root = scan(src)
    dirs, files, end = layout(root)
    base = VIDEO if whole else 0
    out = Out(dst, split)
    vol = bytearray(SECTOR)
    vol[0:20] = MAGIC
    struct.pack_into("<IIQ", vol, 0x14, root.sector, root.table_size, 0x01C4F3E0A0D2C000)
    vol[0x7EC:0x7EC + 20] = MAGIC
    out.write(base + 32 * SECTOR, bytes(vol))
    for d in dirs:
        out.write(base + d.sector * SECTOR, d.table)
    for n in files:
        with open(n.path, "rb") as f:
            at = base + n.sector * SECTOR
            while True:
                chunk = f.read(16 << 20)
                if not chunk:
                    break
                out.write(at, chunk)
                at += len(chunk)
    total = base + end * SECTOR
    out.close(total)
    print(f"{dst}: {len(files)} files, {len(dirs)} folders, {total} bytes{' (whole disc)' if whole else ''}"
          f"{f', parts of {split}' if split else ''}")


if __name__ == "__main__":
    main()
