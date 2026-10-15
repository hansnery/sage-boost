#!/usr/bin/env python
"""Research helper (not shipped): which object templates a `.bse`/`.map` names, and how often.

    python tools/bse_objects.py <file.bse | base-folder> ...

A folder argument means `<folder>/<folder name>.bse`, the layout of the
game's `bases\\` folder. Finds names heuristically (a capitalised identifier
of 6+ characters after a control byte), so it also catches names in
properties and scripts; use `bse_list.py` for the placed objects only.
"""

from __future__ import annotations

import argparse
import collections
import re
import sys
from pathlib import Path

from mapinfo import load_map


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", type=Path, nargs="+")
    args = ap.parse_args()
    for p in args.paths:
        f = p / f"{p.name}.bse" if p.is_dir() else p
        d = load_map(f)
        names = re.findall(rb"(?<=[\x00-\x1f])([A-Z][A-Za-z0-9_]{5,})", d)
        c = collections.Counter(n.decode() for n in names)
        print("==", f.name, len(d))
        print(", ".join(f"{k} x{v}" for k, v in c.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
