#!/usr/bin/env python
"""Research helper (not shipped): check a rewritten `.bse`/`.map` against its original.

    python tools/bse_verify.py <original> <rewritten>

Prints the EAR header and RefPack op counts of the rewritten file (a clean
stream ends on a stop code with no trailing bytes), asserts that the header,
string table and chunk list are unchanged, and lists each top-level chunk
whose bytes differ, with a byte diff. `ObjectsList` children must keep their
ids, versions and count.
"""

from __future__ import annotations

import argparse
import difflib
import struct
import sys
from collections import Counter
from pathlib import Path

from mapinfo import Reader, load_map


def refpack_ops(src: bytes) -> Counter:
    pos, ops = 5, Counter()
    while pos < len(src):
        b0 = src[pos]
        if b0 < 0x80:
            b1 = src[pos + 1]
            pos += 2
            lit, n, off, k = b0 & 3, ((b0 >> 2) & 7) + 3, ((b0 & 0x60) << 3) + b1 + 1, "short"
        elif b0 < 0xC0:
            b1, b2 = src[pos + 1], src[pos + 2]
            pos += 3
            lit, n, off, k = b1 >> 6, (b0 & 0x3F) + 4, ((b1 & 0x3F) << 8) + b2 + 1, "medium"
        elif b0 < 0xE0:
            b1, b2, b3 = src[pos + 1], src[pos + 2], src[pos + 3]
            pos += 4
            lit, n, off, k = b0 & 3, ((b0 & 0x0C) << 6) + b3 + 5, ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1, "long"
        elif b0 < 0xFC:
            pos += 1
            lit, n, off, k = ((b0 & 0x1F) + 1) << 2, 0, 0, "literal"
        else:
            pos += 1 + (b0 & 3)
            ops["stop"] += 1
            break
        ops[k] += 1
        if n and off < n:
            ops["overlap"] += 1
        pos += lit
    ops["trailing_bytes"] = len(src) - pos
    return ops


def show(b: bytes) -> str:
    return b.decode("latin1") if all(32 <= c < 127 for c in b) else b.hex(" ")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("original", type=Path)
    ap.add_argument("rewritten", type=Path)
    args = ap.parse_args()
    old = load_map(args.original)
    new = load_map(args.rewritten)
    raw = args.rewritten.read_bytes()
    print("EAR size field", struct.unpack_from("<I", raw, 4)[0], "decoded", len(new),
          "refpack size", int.from_bytes(raw[10:13], "big"), "flags", raw[8:10].hex())
    print("stream ops:", dict(refpack_ops(raw[8:])))

    ro, rn = Reader(old), Reader(new)
    co = list(ro.chunks(ro.body_start, len(old)))
    cn = list(rn.chunks(rn.body_start, len(new)))
    assert old[:ro.body_start] == new[:rn.body_start], "header/string table changed"
    assert [c[:2] for c in co] == [c[:2] for c in cn], "chunk list changed"
    for (n, v, a0, a1), (_, _, b0, b1) in zip(co, cn):
        if old[a0 - 10:a1] != new[b0 - 10:b1]:
            print(f"changed chunk {n}: size {a1 - a0} -> {b1 - b0}")
            sm = difflib.SequenceMatcher(None, old[a0 - 10:a1], new[b0 - 10:b1], autojunk=False)
            for tag, i0, i1, j0, j1 in sm.get_opcodes():
                if tag != "equal":
                    print(f"    {tag:7} old[{i0:4}:{i1:4}] {show(old[a0 - 10 + i0:a0 - 10 + i1])!r:40}"
                          f" -> new {show(new[b0 - 10 + j0:b0 - 10 + j1])!r}")
        if n == "ObjectsList":
            for (_, ov, o0, o1), (_, nv, p0, p1) in zip(ro.chunks(a0, a1), rn.chunks(b0, b1)):
                assert ov == nv
                assert struct.unpack_from("<I", new, p0 - 10)[0] == struct.unpack_from("<I", old, o0 - 10)[0]
            assert len(list(ro.chunks(a0, a1))) == len(list(rn.chunks(b0, b1)))
    print("all other chunks byte-identical; every chunk parses within its parent")
    return 0


if __name__ == "__main__":
    sys.exit(main())
