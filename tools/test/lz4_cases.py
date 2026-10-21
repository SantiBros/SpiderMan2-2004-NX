# records for lz4_check: u32 raw size, u32 block size, block, raw
import os, random, struct, sys
import lz4.block
iso, out = sys.argv[1], sys.argv[2]
random.seed(5)
size = os.path.getsize(iso)
cases = []
with open(iso, 'rb') as f:
    for _ in range(6000):
        f.seek(random.randrange(0, size // 2048) * 2048)
        cases.append(f.read(2048))
cases += [b'\0' * 2048, b'\xff' * 2048, os.urandom(2048), bytes(range(256)) * 8, b'ab' * 1024, b'abc' * 682 + b'ab',
          os.urandom(700) + b'\0' * 1348, b'x' * 15 + os.urandom(2033), (b'Q' * 19 + os.urandom(5)) * 85 + b'Q' * 8]
n = 0
with open(out, 'wb') as o:
    for raw in cases:
        for mode in ('fast', 'hc9', 'hc12', 'acc'):
            if mode == 'fast': b = lz4.block.compress(raw, store_size=False)
            elif mode == 'acc': b = lz4.block.compress(raw, mode='fast', acceleration=50, store_size=False)
            else: b = lz4.block.compress(raw, mode='high_compression', compression=int(mode[2:]), store_size=False)
            o.write(struct.pack('<II', len(raw), len(b)) + b + raw); n += 1
print(n, 'blocks')
