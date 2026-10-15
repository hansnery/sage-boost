#!/usr/bin/env python
"""Research helper (not shipped): reports from `radar_audit.py`'s JSON.

    python tools/radar_report.py [mode] [args] [--json radar_audit.json]

Modes: `bad` (default) resource buildings not resolving to
`RadarPriority = STRUCTURE`, or with a hiding `KindOf`, or in an unbalanced
block; `disagree` parsed vs plain-text values differ; `dupes` templates
defined more than once; `fac` each faction's resource buildings against
`GondorFarm`'s `KindOf`; `gaps` new-Side buildings no faction walk reached;
`hide` hiding flags of every reached building; `path FACTION OBJECT` how a
faction reaches an object.
"""
import argparse, json, sys
_ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
_ap.add_argument("mode", nargs="?", default="bad")
_ap.add_argument("rest", nargs="*")
_ap.add_argument("--json", default="radar_audit.json")
_args = _ap.parse_args()
d = json.load(open(_args.json, encoding="utf-8"))
res = d["all_res"]
unb = {u[0] for u in d["unbalanced"]}
mode = _args.mode
HIDE = {"IGNORED_IN_GUI", "NO_RADAR", "UNATTACKABLE", "DRAWABLE_ONLY", "INERT", "OPTIMIZED_PROP", "CLEARED_BY_BUILD",
        "DISGUISER", "NO_COLLIDE", "MOVE_ONLY"}
if mode == "disagree":
    for n, v in sorted(res.items()):
        if v["radar"] != v["raw_radar"] or sorted(v["kindof"]) != sorted(v["raw_kindof"]):
            print(n, "| parsed:", v["radar"], v["radar_src"], "| raw:", v["raw_radar"], v["raw_radar_src"],
                  "| kindof differs" if sorted(v["kindof"]) != sorted(v["raw_kindof"]) else "")
elif mode == "bad":
    for n, v in sorted(res.items()):
        if v["radar"] != "STRUCTURE" or set(v["kindof"]) & HIDE or any(c in unb for c in v["chain"]):
            print(n, "|", v["radar"], "|", v["radar_src"], "|", sorted(set(v["kindof"]) & HIDE), "|", v["why"], "|", v["file"], v["line"], "|", v["chain"])
elif mode == "dupes":
    for n, v in d["dupes"].items():
        print(n, "RES" if n in res else "", v)
elif mode == "fac":
    ref = set(res["GondorFarm"]["kindof"])
    print("GondorFarm KindOf:", sorted(ref))
    for f, v in d["factions"].items():
        print("==", f, v["upgrades"], "reach", v["reach_count"])
        for x in v["res"]:
            r = res[x]
            k = set(r["kindof"])
            print("   ", x, "|", r["radar"], "<-", r["radar_src"], "|", "VR=" + str(r["vision"]), "|", ",".join(r["why"]),
                  "| +" + " +".join(sorted(k - ref)), "-" + " -".join(sorted(ref - k)))
elif mode == "gaps":
    sides = {"Arthedain", "Numenor", "Ithilien", "DolAmroth", "Hornburg", "EredLuin", "Lindon", "Shire", "Dorwinion",
             "Morgul", "Annatar", "WhiteHand", "Gundabad", "Moria", "Rhudaur", "CarnDum", "Rhun"}
    dirs = ["arthedain", "numenor", "ithilien", "dolamroth", "shire", "dorwinion", "morgul", "annatar", "whitehand",
            "gundabad", "moria", "carndum", "rhun", "lindon", "eredluin", "hornburg"]
    reached = set()
    for f, v in d["factions"].items():
        reached |= set(v["res"])
    for n, v in sorted(res.items()):
        if n in reached:
            continue
        if v.get("side") in sides or any("\\" + x in v["file"].lower() for x in dirs):
            print(n, "|", v.get("side"), "|", v["radar"], "|", v["file"], "|", sorted(set(v["kindof"]) & HIDE))
elif mode == "hide":
    reached = set()
    for f, v in d["factions"].items():
        if f != "FactionMen":
            reached |= set(v["res"])
    for n in sorted(reached):
        v = res[n]
        print(n, v["radar"], v["radar_raw"], "IGNORED_IN_GUI" in v["kindof"], sorted(set(v["kindof"]) & HIDE))
elif mode == "path":
    f, x = _args.rest[0], _args.rest[1]
    print(d["factions"][f]["paths"].get(x))
