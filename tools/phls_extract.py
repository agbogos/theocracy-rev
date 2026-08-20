#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Adam Bogos
"""Extract a Philos "PHLS" archive (Theocracy's *.pck data packs).

Format (reverse-engineered from the CD packs; see docs/reference/phls-format.md):

    "PHLS"                      4 bytes  magic
    uint32  dir_block_bytes     little-endian; size of the record block

    record block (dir_block_bytes / 64 records), a flattened depth-first
    traversal of the directory tree.  Each record is 64 bytes:
        char    name[60]        NUL-padded
        uint32  size            little-endian
    If (size & 0x80000000): `name` is a subdirectory to descend into.
    A record named ".." pops back up to the parent directory.
    Otherwise the record is a file of `size` bytes.

    file data                   all file payloads concatenated after the
                                record block, in record (depth-first) order.

The .pck files on the CD are gzip-wrapped; this handles both gzip and raw.
Usage:  python3 tools/phls_extract.py <archive.pck> <outdir> [--list]
"""
import gzip
import os
import struct
import sys

import theocracy_crypt  # sibling module: RSA4096 XOR cipher

DIR_FLAG = 0x80000000
REC = 64


def load(path: str) -> bytes:
    with open(path, "rb") as f:
        head = f.read(2)
        f.seek(0)
        if head == b"\x1f\x8b":               # gzip
            return gzip.decompress(f.read())
        return f.read()


def extract(path: str, outdir: str, list_only: bool = False,
            decrypt: bool = False) -> None:
    data = load(path)
    if data[:4] != b"PHLS":
        raise SystemExit(f"{path}: not a PHLS archive (magic {data[:4]!r})")
    dir_bytes = struct.unpack_from("<I", data, 4)[0]
    if dir_bytes % REC:
        raise SystemExit(f"dir block {dir_bytes} not a multiple of {REC}")
    nrec = dir_bytes // REC
    data_base = 8 + dir_bytes
    cursor = data_base                        # running offset into file data
    stack = [outdir]                          # current directory path stack
    nfiles = ndirs = 0

    for i in range(nrec):
        off = 8 + i * REC
        name = data[off:off + 60].split(b"\0", 1)[0].decode("latin1")
        size = struct.unpack_from("<I", data, off + 60)[0]

        if name == "..":
            if len(stack) > 1:
                stack.pop()
            continue
        if size & DIR_FLAG:
            ndirs += 1
            path_here = os.path.join(stack[-1], name)
            stack.append(path_here)
            if not list_only:
                os.makedirs(path_here, exist_ok=True)
            continue

        # a file
        nfiles += 1
        dst = os.path.join(stack[-1], name)
        if list_only:
            rel = os.path.relpath(dst, outdir)
            print(f"  {size:>10}  {rel}")
        else:
            os.makedirs(stack[-1], exist_ok=True)
            payload = data[cursor:cursor + size]
            if decrypt and theocracy_crypt.is_encrypted(payload):
                payload = theocracy_crypt.decrypt(payload)
            with open(dst, "wb") as out:
                out.write(payload)
        cursor += size

    leftover = len(data) - cursor
    print(f"{os.path.basename(path)}: {nfiles} files, {ndirs} dirs, "
          f"data {data_base:#x}..{cursor:#x}  (leftover {leftover} bytes)")
    if leftover != 0:
        print(f"  WARNING: {leftover} trailing bytes unaccounted for — "
              f"format assumption may be off", file=sys.stderr)


def main() -> None:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    list_only = "--list" in sys.argv
    decrypt = "--decrypt" in sys.argv
    if len(args) < 1:
        raise SystemExit(__doc__)
    archive = args[0]
    outdir = args[1] if len(args) > 1 else "phls_out"
    extract(archive, outdir, list_only, decrypt)


if __name__ == "__main__":
    main()
