#!/usr/bin/env python
"""Research helper (not shipped): which AI bases have farms, compared with the parent mod's copy.

    python tools/bse_farmdiff.py <mod bases folder> <parent mod bases folder>

For every `ai base*/*.bse` in the first folder that places one of FARMS,
prints the farms it places and, when the second folder has the same base,
what the mod added (other than farms) or removed relative to it. Used on
2026-10-14 to find the Realms in Exile AI bases that differ from Age of the
Ring's only by added farms.
"""

from __future__ import annotations

import argparse
import struct
import sys
from collections import Counter
from pathlib import Path

from mapinfo import Reader, load_map

FARMS = {"RivendellOrchard_ForAI", "GuldurSlaveMill_ForAI", "DwarvenMineShaft_ForAI", "ElvenMallornTree_ForAI",
         "HaradwaithBazaar_ForAI", "IsengardFurnace_ForAI", "MirkwoodVineyard_ForAI", "MordorSlaughterHouse_ForAI",
         "GondorFarm_ForAI", "RohanFarmForAI", "WildMineShaft_ForAI", "ShireHobbitFarm",
         "ArnorFarm_ForAI", "DorwinionVineyard_ForAI", "RhunSlaughterHouse_ForAI", "AngmarMill_ForAI",
         "IthilienFarm_ForAI", "DaleShoremansCottage_ForAI", "DolAmrothFarm_ForAI", "EreborForge_ForAI"}


def objects(f: Path) -> list[tuple[str, float, float]]:
    d = load_map(f)
    r = Reader(d)
    out = []
    for name, _, b0, b1 in r.chunks(r.body_start, len(d)):
        if name == "ObjectsList":
            for _, _, o0, _ in r.chunks(b0, b1):
                x, y = struct.unpack_from("<ff", d, o0)
                t, _ = r.ascii(o0 + 20)
                out.append((t, x, y))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mod", type=Path)
    ap.add_argument("parent", type=Path)
    args = ap.parse_args()
    for f in sorted(args.mod.glob("ai base*/*.bse")):
        objs = objects(f)
        farms = [o for o in objs if o[0] in FARMS]
        if not farms:
            continue
        a = args.parent / f.parent.name / f.name
        if a.exists():
            ac = Counter(t for t, _, _ in objects(a))
            rc = Counter(t for t, _, _ in objs)
            nonfarm_extra = {k: v for k, v in (rc - ac).items() if k not in FARMS}
            missing = ac - rc
            note = (f"parent objs={sum(ac.values())}; other additions={dict(nonfarm_extra) or '-'}; "
                    f"removed vs parent={dict(missing) or '-'}")
        else:
            note = "not in parent"
        kinds = Counter(t for t, _, _ in farms)
        print(f"{f.parent.name}: farms {dict(kinds)}  | {note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
