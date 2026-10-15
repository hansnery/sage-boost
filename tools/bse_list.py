#!/usr/bin/env python
"""Research helper (not shipped): the objects placed in a SAGE `.bse` or `.map`.

    python tools/bse_list.py <file.bse | file.map>

Prints the top-level chunk names, then one line per `ObjectsList` object:
template, position, angle, flags and a few properties (initial health,
owner, name, uniqueID, selectable). `SidesList`, `WorldInfo` and
`HeightMapData` are shown by size only.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

from mapinfo import Reader, load_map

KEEP = ("objectInitialHealth", "originalOwner", "objectName", "uniqueID", "objectSelectable")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", type=Path)
    args = ap.parse_args()
    d = load_map(args.path)
    r = Reader(d)
    print("chunks:", [n for n, _, _, _ in r.chunks(r.body_start, len(d))])
    for name, ver, b0, b1 in r.chunks(r.body_start, len(d)):
        if name == "ObjectsList":
            for _, _, o0, _ in r.chunks(b0, b1):
                x, y, z, ang = struct.unpack_from("<ffff", d, o0)
                flags = struct.unpack_from("<I", d, o0 + 16)[0]
                tname, pos = r.ascii(o0 + 20)
                props, _ = r.dict(pos)
                keep = {k: v for k, v in props.items() if k in KEEP}
                print(f"{tname:40s} ({x:7.1f},{y:7.1f},{z:5.1f}) ang={ang:5.2f} flags={flags} {keep}")
        elif name in ("SidesList", "WorldInfo", "HeightMapData"):
            print(name, "v", ver, "bytes", b1 - b0)
    return 0


if __name__ == "__main__":
    sys.exit(main())
