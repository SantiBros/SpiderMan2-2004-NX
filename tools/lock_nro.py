#!/usr/bin/env python3
"""lock_nro.py: the release's .nro, locked with the game's own default.xbe, and unlocked again.

    python3 tools/lock_nro.py lock   <SpiderMan2Xbox.nro> <default.xbe> <out.nro.locked>   (version 2, from 0.4.0)
    python3 tools/lock_nro.py lock1  <SpiderMan2Xbox.nro> <default.xbe> <out.nro.locked>   (version 1, up to 0.3.1)
    python3 tools/lock_nro.py unlock <in.nro.locked> <default.xbe> <out.nro>               (either version)
    python3 tools/lock_nro.py code   <default.xbe>                                         (its game code's SHA-1)

The .nro holds code made from the game, so the release never carries it as it is: it is encrypted with AES-256-GCM
under a key made from the game's default.xbe, and only someone with the game can make it again. The installer page
(installer/index.html) and the port's own updater (switch/update.cpp) unlock it the same way.

Version 1's key is the SHA-256 of a label and the whole default.xbe, so only the release it was made from opens it.
Version 2's is the SHA-256 of another label and the xbe's game code (sdk/runtime/xbe/code.h: its loaded sections, entry
point and kernel imports, not its header and certificate), so any edition of the game built from the same code opens
it: the European discs, if theirs is the US one's.

The locked file: a 64 byte header, then the ciphertext with GCM's 16 byte tag at its end. The header (all of it the
authenticated data) is "SM2NXLK1" or "SM2NXLK2", the 12 byte nonce, 4 zero bytes, the .nro's size (8 bytes, little
endian), the SHA-1 of what the key is made from (version 1 the default.xbe, 2 its game code; 20 bytes) and 12 zero
bytes.
"""
import hashlib
import os
import struct
import sys

FORMATS = {
    1: (b"SM2NXLK1", b"SpiderMan2-2004-NX lock 1\0"),
    2: (b"SM2NXLK2", b"SpiderMan2-2004-NX lock 2\0"),
}
SUPPORTED_SHA1 = "853bcb253c5baea2d7e2d9b59e5888b4c87bb402"
SUPPORTED_CODE = "b3d6d36a707840a9dcc047f0302b41edc61dfee2"  # its game code's (code_bytes)


def code_bytes(xbe: bytes) -> bytes:
    """the xbe's game code, as sdk/runtime/xbe/code.h makes it"""
    if xbe[:4] != b"XBEH" or len(xbe) < 0x178:
        sys.exit("this is not an xbe")
    u32 = lambda at: struct.unpack_from("<I", xbe, at)[0]
    base, count, headers = u32(0x104), u32(0x11C), u32(0x120)
    out = bytearray()
    for i in range(count):
        at = headers - base + i * 56
        flags, va, vsize, raw, rsize = struct.unpack_from("<IIIII", xbe, at)
        if flags & 8:
            continue
        out += struct.pack("<III", va, vsize, rsize) + xbe[raw:raw + rsize]
    out += struct.pack("<II", u32(0x128) ^ 0xA8FC57AB, u32(0x158) ^ 0x5B6D40B6)
    return bytes(out)


def key_material(version: int, xbe: bytes) -> bytes:
    return xbe if version == 1 else code_bytes(xbe)


def lock_bytes(data: bytes, xbe: bytes, version=2) -> bytes:
    """data locked with that default.xbe (version 1) or its game code (version 2)"""
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    magic, label = FORMATS[version]
    material = key_material(version, xbe)
    nonce = os.urandom(12)
    header = magic + nonce + b"\0" * 4 + struct.pack("<Q", len(data)) + hashlib.sha1(material).digest() + b"\0" * 12
    assert len(header) == 64
    return header + AESGCM(hashlib.sha256(label + material).digest()).encrypt(nonce, data, header)


def lock(nro_path, xbe_path, out_path, version=2):
    nro = open(nro_path, "rb").read()
    xbe = open(xbe_path, "rb").read()
    if hashlib.sha1(xbe).hexdigest() != SUPPORTED_SHA1:
        sys.exit("this default.xbe is not the supported release (SHA-1 %s)" % hashlib.sha1(xbe).hexdigest())
    with open(out_path, "wb") as f:
        f.write(lock_bytes(nro, xbe, version))
    print("%s: %d bytes locked (version %d) into %s" % (nro_path, len(nro), version, out_path))


def unlock(in_path, xbe_path, out_path):
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    data = open(in_path, "rb").read()
    xbe = open(xbe_path, "rb").read()
    header, sealed = data[:64], data[64:]
    version = next((v for v, (magic, _) in FORMATS.items() if header[:8] == magic), None)
    if version is None:
        sys.exit("%s is not a locked .nro" % in_path)
    material = key_material(version, xbe)
    if hashlib.sha1(material).digest() != header[32:52]:
        sys.exit("this default.xbe is not one the .nro opens with" + (" (another game code)" if version == 2 else ""))
    nro = AESGCM(hashlib.sha256(FORMATS[version][1] + material).digest()).decrypt(header[8:20], sealed, header)
    if len(nro) != struct.unpack("<Q", header[24:32])[0]:
        sys.exit("the unlocked .nro has the wrong size")
    with open(out_path, "wb") as f:
        f.write(nro)
    print("%s (version %d) unlocked into %s (%d bytes)" % (in_path, version, out_path, len(nro)))


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "code" and len(sys.argv) == 3:
        print(hashlib.sha1(code_bytes(open(sys.argv[2], "rb").read())).hexdigest())
    elif cmd in ("lock", "lock1") and len(sys.argv) == 5:
        lock(*sys.argv[2:], version=2 if cmd == "lock" else 1)
    elif cmd == "unlock" and len(sys.argv) == 5:
        unlock(*sys.argv[2:])
    else:
        sys.exit(__doc__)
