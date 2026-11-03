#!/usr/bin/env python3
"""lock_nro.py: the release's .nro, locked with the game's own default.xbe, and unlocked again.

    python3 tools/lock_nro.py lock   <SpiderMan2Xbox.nro> <default.xbe> <out.nro.locked>
    python3 tools/lock_nro.py unlock <in.nro.locked> <default.xbe> <out.nro>

The .nro holds code made from the game, so the release never carries it as it is: it is encrypted with AES-256-GCM
under a key made from the whole default.xbe (SHA-256 of a label and the file), and only someone with the game can
make it again. The installer page (installer/index.html) unlocks it in the browser the same way.

The locked file: a 64 byte header, then the ciphertext with GCM's 16 byte tag at its end. The header (all of it the
authenticated data) is "SM2NXLK1", the 12 byte nonce, 4 zero bytes, the .nro's size (8 bytes, little endian), the
SHA-1 of the default.xbe it needs (20 bytes) and 12 zero bytes.
"""
import hashlib
import os
import struct
import sys

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

MAGIC = b"SM2NXLK1"
LABEL = b"SpiderMan2-2004-NX lock 1\0"
SUPPORTED_SHA1 = "853bcb253c5baea2d7e2d9b59e5888b4c87bb402"


def key_of(xbe: bytes) -> bytes:
    return hashlib.sha256(LABEL + xbe).digest()


def lock(nro_path, xbe_path, out_path):
    nro = open(nro_path, "rb").read()
    xbe = open(xbe_path, "rb").read()
    sha1 = hashlib.sha1(xbe).digest()
    if sha1.hex() != SUPPORTED_SHA1:
        sys.exit("this default.xbe is not the supported release (SHA-1 %s)" % sha1.hex())
    nonce = os.urandom(12)
    header = MAGIC + nonce + b"\0" * 4 + struct.pack("<Q", len(nro)) + sha1 + b"\0" * 12
    assert len(header) == 64
    sealed = AESGCM(key_of(xbe)).encrypt(nonce, nro, header)
    with open(out_path, "wb") as f:
        f.write(header + sealed)
    print("%s: %d bytes locked into %s" % (nro_path, len(nro), out_path))


def unlock(in_path, xbe_path, out_path):
    data = open(in_path, "rb").read()
    xbe = open(xbe_path, "rb").read()
    header, sealed = data[:64], data[64:]
    if header[:8] != MAGIC:
        sys.exit("%s is not a locked .nro" % in_path)
    if hashlib.sha1(xbe).digest() != header[32:52]:
        sys.exit("this default.xbe is not the one the .nro needs")
    nro = AESGCM(key_of(xbe)).decrypt(header[8:20], sealed, header)
    if len(nro) != struct.unpack("<Q", header[24:32])[0]:
        sys.exit("the unlocked .nro has the wrong size")
    with open(out_path, "wb") as f:
        f.write(nro)
    print("%s unlocked into %s (%d bytes)" % (in_path, out_path, len(nro)))


if __name__ == "__main__":
    if len(sys.argv) != 5 or sys.argv[1] not in ("lock", "unlock"):
        sys.exit(__doc__)
    (lock if sys.argv[1] == "lock" else unlock)(*sys.argv[2:])
