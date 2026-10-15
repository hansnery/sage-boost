#!/usr/bin/env python
"""Research helper (not shipped): find files by name in `.big` archives.

    python tools/big_find.py <substring> <archive.big> ...

Prints `archive: path  size` for every entry whose path contains the
substring (case-insensitive). Reads only the archive index; non-BIG files
are skipped.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pattern")
    ap.add_argument("archives", type=Path, nargs="+")
    args = ap.parse_args()
    pat = args.pattern.lower()
    for big in args.archives:
        with open(big, "rb") as f:
            head = f.read(16)
            if head[:4] not in (b"BIGF", b"BIG4"):
                continue
            count, hsize = struct.unpack(">II", head[8:16])
            idx = f.read(hsize - 16)
        pos = 0
        for _ in range(count):
            off, size = struct.unpack(">II", idx[pos:pos + 8])
            end = idx.index(b"\0", pos + 8)
            name = idx[pos + 8:end].decode("latin-1")
            pos = end + 1
            if pat in name.lower():
                print(f"{big.name}: {name}  {size}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
