#!/usr/bin/env python
"""Research helper (not shipped): read a SAGE `asset.dat` (`ALAE`) index.

    python tools/asset_dat.py <asset.dat> [name-substring]

Prints the header counts; with a substring, every file entry whose name
contains it (lower-case match), with its asset count and first assets.

Layout as observed (BFME2/ROTWK, Age of the Ring, Realms in Exile):
`ALAE`, u32 version, u32 file count, u32 dependency count; per file a u8
length-prefixed name, a u64 file time, a u16 asset count, then per asset a
u8-prefixed name, a 4-byte type tag (stored reversed, NUL-padded) and u32
offset and size; then per dependency record the file and asset names and a
u16-counted list of names. Parsing stops with an error at the first record
that doesn't look printable.
"""

from __future__ import annotations

import argparse
import struct
import sys


def parse(path: str):
    b = open(path, "rb").read()
    if b[:4] != b"ALAE":
        raise ValueError(f"{path}: not an ALAE asset.dat ({b[:4]!r})")
    ver, nfiles, nassets = struct.unpack_from("<III", b, 4)
    p = 16
    files = []
    for i in range(nfiles):
        start = p
        n = b[p]
        p += 1
        name = b[p:p + n].decode("latin-1")
        p += n
        ft, cnt = struct.unpack_from("<QH", b, p)
        p += 10
        assets = []
        for _ in range(cnt):
            n = b[p]
            p += 1
            an = b[p:p + n].decode("latin-1")
            p += n
            typ = b[p:p + 4][::-1].decode("latin-1").strip("\x00")
            p += 4
            if not (typ.isalnum() and an.replace("\t", " ").isprintable() and name.isprintable()):
                raise ValueError(f"bad at file {i} {name!r} asset {an!r} typ {typ!r} pos {p}: "
                                 f"{b[start:start + 120]!r} prev={files[-1][0] if files else None}")
            off, size = struct.unpack_from("<II", b, p)
            p += 8
            assets.append((an, typ, off, size))
        files.append((name, ft, assets, start, p))
    files_end = p

    def s() -> str:
        nonlocal p
        n = b[p]
        p += 1
        v = b[p:p + n].decode("latin-1")
        p += n
        return v

    deps = []
    for _ in range(nassets):
        start = p
        f, a = s(), s()
        cnt, = struct.unpack_from("<H", b, p)
        p += 2
        d = [s() for _ in range(cnt)]
        deps.append((f, a, d, start, p))
    return b, ver, nfiles, nassets, files, files_end, deps, p


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path")
    ap.add_argument("name", nargs="?")
    args = ap.parse_args()
    b, ver, nf, na, files, fend, deps, end = parse(args.path)
    print(f"ver={ver:#x} files={nf} deps={na} sum_assets={sum(len(f[2]) for f in files)} "
          f"files_end={fend} end={end} len={len(b)}")
    if args.name:
        for name, ft, assets, s, e in files:
            if args.name.lower() in name.lower():
                print(name, len(assets), [a[:2] for a in assets][:6])
    return 0


if __name__ == "__main__":
    sys.exit(main())
