#!/usr/bin/env python3
"""Theocracy text/config cipher — decrypt/encrypt the `RSA4096`-marked files.

The `RSA4096` header is a joke: the actual cipher is a symmetric XOR against two
short repeating keys (periods 13 and 17). Ported verbatim from the original
`XorBuff` (tools/crypt/TheocracyEncDec.cpp), which was recovered from the game
binary. Only `.cfg`/`.txt`/`.idx`-style config files are encrypted; binary
assets (`.raw`, sprites, anims, sounds) are plaintext and must NOT be touched.

    file layout:  "RSA4096" (7 bytes) + XOR(plaintext)
    XOR:          out[i] = in[i] ^ key2[i % 17] ^ key1[i % 13]   (i over body)

Usage:
    theocracy_crypt.py <file> [-o out]      # decrypt one file (stdout if no -o)
    theocracy_crypt.py --tree <dir>         # decrypt every RSA4096 file in-place
    theocracy_crypt.py --encrypt <file> -o out
"""
import os
import sys

HEADER = b"RSA4096"
KEY1 = b"theocracy sux"        # period 0x0D = 13
KEY2 = b"mutant technology"    # period 0x11 = 17


def xor(body: bytes) -> bytes:
    """The core cipher — its own inverse. `body` excludes the header."""
    return bytes(b ^ KEY2[i % 17] ^ KEY1[i % 13] for i, b in enumerate(body))


def is_encrypted(blob: bytes) -> bool:
    return blob[:7] == HEADER


def decrypt(blob: bytes) -> bytes:
    """Encrypted file bytes -> plaintext. Pass-through if unmarked."""
    return xor(blob[7:]) if is_encrypted(blob) else blob


def encrypt(plain: bytes) -> bytes:
    """Plaintext -> encrypted file bytes (header + ciphertext)."""
    return HEADER + xor(plain)


def _decrypt_tree(root: str) -> None:
    n = 0
    for dirpath, _, files in os.walk(root):
        for name in files:
            p = os.path.join(dirpath, name)
            with open(p, "rb") as f:
                blob = f.read()
            if is_encrypted(blob):
                with open(p, "wb") as f:
                    f.write(xor(blob[7:]))
                n += 1
    print(f"decrypted {n} RSA4096 files under {root}")


def main() -> None:
    a = sys.argv[1:]
    if not a:
        raise SystemExit(__doc__)
    if a[0] == "--tree":
        _decrypt_tree(a[1])
        return
    do_encrypt = "--encrypt" in a
    a = [x for x in a if x != "--encrypt"]
    out = None
    if "-o" in a:
        i = a.index("-o"); out = a[i + 1]; del a[i:i + 2]
    blob = open(a[0], "rb").read()
    result = encrypt(blob) if do_encrypt else decrypt(blob)
    if out:
        open(out, "wb").write(result)
    else:
        sys.stdout.buffer.write(result)


if __name__ == "__main__":
    main()
