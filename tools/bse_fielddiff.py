#!/usr/bin/env python
"""Research helper (not shipped): field-by-field diff of two castle `.bse` files.

    python tools/bse_fielddiff.py <a.bse> <b.bse>

Splits each file's `ObjectsList` (position, angle, flags, type, every
property) and `CastleTemplates` (name, template, offset, angle, priority,
phase) into named fields and prints the ones that differ. Both files must
have the same objects and template entries in the same order; a byte diff
of such files is unreadable because similar objects realign.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

from bse_patch_carndum import dict_entries
from mapinfo import Reader, load_map


def fields(path: Path) -> dict[str, object]:
    d = load_map(path)
    r = Reader(d)
    out: dict[str, object] = {}
    for n, ver, b0, b1 in r.chunks(r.body_start, len(d)):
        if n == "ObjectsList":
            for k, (_, ov, o0, o1) in enumerate(r.chunks(b0, b1)):
                pre = f"obj{k}"
                out[f"{pre}.chunk"] = (struct.unpack_from("<I", d, o0 - 10)[0], ov)
                for name, v in zip(("x", "y", "z", "angle", "flags"), struct.unpack_from("<ffffI", d, o0)):
                    out[f"{pre}.{name}"] = v
                out[f"{pre}.type"], p = r.ascii(o0 + 20)
                entries, end = dict_entries(r, p)
                out[f"{pre}.dictkeys"] = [(e[0], e[1]) for e in entries]
                for key, typ, vs, ve in entries:
                    out[f"{pre}.{key}"] = d[vs:ve]
                out[f"{pre}.rest"] = d[end:o1]
        elif n == "CastleTemplates":
            out["ct.head"] = d[b0:b0 + 8]
            count = struct.unpack_from("<I", d, b0 + 4)[0]
            p = b0 + 8
            for k in range(count):
                out[f"ct{k}.name"], p = r.ascii(p)
                out[f"ct{k}.template"], p = r.ascii(p)
                for name, v in zip(("x", "y", "z", "angle", "prio", "phase"), struct.unpack_from("<ffffII", d, p)):
                    out[f"ct{k}.{name}"] = v
                p += 24
            out["ct.tail"] = d[p:b1]
    return out


def show(v: object) -> object:
    return v[2:].decode("latin1") if isinstance(v, bytes) else v


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a", type=Path)
    ap.add_argument("b", type=Path)
    args = ap.parse_args()
    a, b = fields(args.a), fields(args.b)
    if a.keys() != b.keys():
        print("different objects or template entries:", sorted(set(a) ^ set(b)))
        return 1
    changed = [(k, a[k], b[k]) for k in a if a[k] != b[k]]
    for k, old, new in changed:
        print(f"  {k:26} {show(old)!r:40} -> {show(new)!r}")
    print(f"{len(changed)} of {len(a)} fields changed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
