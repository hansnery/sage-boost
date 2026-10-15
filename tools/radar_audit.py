#!/usr/bin/env python
"""Research helper (not shipped): minimap and inflation-filter audit of a mod's resource buildings.

    python tools/radar_audit.py <mod data folder> [--out radar_audit.json]
    python tools/radar_report.py [bad|disagree|dupes|fac|gaps|hide|path F X] [--json radar_audit.json]

Parses every `.ini` under `<data>/ini` (inlining `#include`, resolving
`#define`, `ChildObject`/`ObjectReskin` inheritance, `RemoveModule`/
`ReplaceModule` and `KindOf = +X -Y`), finds resource buildings
(`AutoDepositUpdate`, `CashOverTime`, `KindOf FS_CASH_PRODUCER`, or a `+Name`
in `RESOURCE_MODIFIER_OBJECT_FILTER`), and for each faction in CHECK walks
what its starting units and buildings can build (command sets, buttons,
OCLs, castle unpacks; modules keyed on another faction's `InitialUpgrades`
are skipped). Writes, per resource building, the resolved `RadarPriority`,
`KindOf`, `VisionRange` and where each was set, and per faction the
buildings it reaches with the path. A second plain-text scan of each
object's own lines is stored next to the parsed values (`raw_*`) as a
cross-check. CHECK and the Side lists in `radar_report.py` are Realms in
Exile's split factions (2026-10-14).
"""
import argparse, os, re, sys, json, collections

_ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
_ap.add_argument("data", help="the mod's data folder (the one holding ini\\)")
_ap.add_argument("--out", default="radar_audit.json")
_args = _ap.parse_args()
ROOT = os.path.abspath(_args.data)
INI = os.path.join(ROOT, "ini")

TOPLEVEL = re.compile(r"^(Object|ChildObject|ObjectReskin|CommandSet|CommandButton|ObjectCreationList|"
                      r"PlayerTemplate|Upgrade|Weapon|Armor|Locomotor|FXList|SpecialPower|Science|"
                      r"AudioEvent|MappedImage|ModifierList|ExperienceLevel|CrateData|Animation|"
                      r"Multisound|MusicTrack|DialogEvent|StreamedSound|LivingWorld\w*|\w+)\b")

def strip_comment(line):
    i = line.find(";")
    j = line.find("//")
    cut = [k for k in (i, j) if k >= 0]
    return line[:min(cut)] if cut else line

defines = {}
define_src = {}
include_missing = []

def expand(path, depth=0, chain=()):
    """Return list of (file, lineno, text) with #include inlined and #define collected."""
    out = []
    try:
        with open(path, "rb") as f:
            raw = f.read().decode("latin-1")
    except FileNotFoundError:
        include_missing.append((chain[-1] if chain else None, path))
        return out
    in_script = False
    for n, line in enumerate(raw.splitlines(), 1):
        s = strip_comment(line).strip()
        if not s:
            continue
        if in_script:
            if s.lower().startswith("endscript"):
                in_script = False
            continue
        if s.lower().startswith("beginscript"):
            in_script = True
            continue
        m = re.match(r'#include\s+"([^"]+)"', s, re.I)
        if m:
            inc = os.path.normpath(os.path.join(os.path.dirname(path), m.group(1).lstrip("\\/")))
            out.extend(expand(inc, depth + 1, chain + ((path, n),)))
            continue
        m = re.match(r"#define\s+(\S+)\s*(.*)$", s, re.I)
        if m:
            defines[m.group(1)] = m.group(2).strip()
            define_src[m.group(1)] = (path, n)
            continue
        out.append((path, n, s))
    return out

def resolve(val, seen=None):
    toks = val.split()
    res = []
    for t in toks:
        k = 0
        while t in defines and k < 10:
            t = defines[t]; k += 1
        res.append(t)
    return " ".join(res)

# ---------------------------------------------------------------- load
files = []
for d, _, fs in os.walk(INI):
    for fn in fs:
        if fn.lower().endswith(".ini"):
            files.append(os.path.join(d, fn))
files.sort(key=lambda p: p.lower())
# default dir first (engine loads Default before the rest)
files.sort(key=lambda p: 0 if os.sep + "default" + os.sep in p.lower() else 1)

included = set()
# files that are only ever #included (not loaded on their own) still parse fine when loaded alone;
# we parse every .ini as loaded (the engine loads ini\object recursively + ini\*.ini).

OBJ_OPENERS = {"draw", "body", "behavior", "clientupdate", "clientbehavior", "armorset", "weaponset",
               "locomotorset", "unitspecificsounds", "unitspecificfx", "prerequisites", "replacemodule",
               "addmodule", "inheritablemodule", "overrideablebylikekind", "threatbreakdown",
               "autoresolvearmor", "autoresolveweapon", "autoresolvebody", "crowdresponse"}
EQ_INNER_OPENERS = {"conditionstate", "defaultconditionstate", "transitionstate", "animationstate",
                    "idleanimationstate", "modelconditionstate", "defaultmodelconditionstate",
                    "animation", "soundstate", "lodoptions"}
SINGLE_NOEQ = {"removemodule", "end", "particlesysbone", "weaponfirefxbone", "weaponlaunchbone",
               "weaponrecoilbone", "weaponmuzzleflash", "pristineshowsubobject", "pristinehidesubobject",
               "damagedshowsubobject", "damagedhidesubobject", "reallydamagedshowsubobject",
               "reallydamagedhidesubobject", "meshnamematches", "geometrycontactpoint", "weathertexture"}

def key_of(s):
    return re.split(r"[\s=]", s, maxsplit=1)[0]

class Obj:
    def __init__(s, kind, name, parent, path, line):
        s.kind, s.name, s.parent, s.path, s.line = kind, name, parent, path, line
        s.fields = {}          # top-level key -> list of (value, path, line)
        s.modules = []         # list of dict(type, tag, lines, path, line, via)
        s.removes = []         # RemoveModule tags
        s.replaces = {}        # tag -> module
        s.unbalanced = False

objects = collections.defaultdict(list)   # name -> [Obj in load order]
blocks = collections.defaultdict(lambda: collections.defaultdict(list))  # type -> name -> [lines]
templates = {}

def parse_object(lines, i, kind, name, parent, path, line):
    o = Obj(kind, name, parent, path, line)
    i += 1
    cur = None   # current module
    depth = 1
    while i < len(lines):
        p, n, s = lines[i]
        k = key_of(s).lower()
        if k == "end":
            depth -= 1
            if depth == 0:
                return o, i + 1
            if depth == 1 and cur is not None:
                cur = None
            elif depth == 2 and cur is not None and cur.get("_inrepl"):
                cur["_inrepl"] = False
            i += 1
            continue
        if depth == 1 and TOPLEVEL.match(s) and k in ("object", "childobject", "objectreskin"):
            o.unbalanced = True
            return o, i
        if depth == 1:
            if k in OBJ_OPENERS:
                rest = s.split("=", 1)[1].split() if "=" in s else s.split()[1:]
                mod = {"key": k, "type": rest[0] if rest else "", "tag": rest[1] if len(rest) > 1 else "",
                       "lines": [], "path": p, "line": n}
                if k == "replacemodule":
                    mod["tag"] = rest[0] if rest else ""
                    mod["type"] = None
                    mod["_repl"] = True
                    o.replaces[mod["tag"]] = mod
                elif k in ("addmodule",):
                    mod["_add"] = True
                    o.modules.append(mod)
                else:
                    o.modules.append(mod)
                cur = mod
                depth += 1
            elif k == "removemodule":
                o.removes.append(s.split()[1] if len(s.split()) > 1 else "")
            elif "=" not in s and k not in SINGLE_NOEQ:
                mod = {"key": k, "type": k, "tag": "", "lines": [], "path": p, "line": n}
                o.modules.append(mod)
                cur = mod
                depth += 1
            else:
                if "=" in s:
                    key, val = s.split("=", 1)
                    o.fields.setdefault(key.strip().lower(), []).append((val.strip(), p, n))
                else:
                    o.fields.setdefault(k, []).append((s[len(k):].strip(), p, n))
            i += 1
            continue
        # depth >= 2
        if cur is not None:
            cur["lines"].append((s, p, n))
            if cur.get("_repl") and cur["type"] is None and depth == 2:
                # first line inside ReplaceModule is the replacement module header
                kk = key_of(s).lower()
                if kk in OBJ_OPENERS:
                    rest = s.split("=", 1)[1].split() if "=" in s else s.split()[1:]
                    cur["type"] = rest[0] if rest else ""
                    cur["newtag"] = rest[1] if len(rest) > 1 else ""
                    depth += 1
                    i += 1
                    continue
        if k in SINGLE_NOEQ:
            pass
        elif "=" not in s or k in EQ_INNER_OPENERS:
            depth += 1
        i += 1
    o.unbalanced = True
    return o, i

def parse_generic(lines, i):
    """Generic block (CommandSet/CommandButton/OCL/PlayerTemplate): return (body lines, next i)."""
    body = []
    depth = 1
    i += 1
    while i < len(lines):
        p, n, s = lines[i]
        k = key_of(s).lower()
        if k == "end":
            depth -= 1
            if depth == 0:
                return body, i + 1
        elif ("=" not in s) and k not in SINGLE_NOEQ:
            depth += 1
        body.append((s, p, n))
        i += 1
    return body, i

stray = []
raw_span = collections.defaultdict(list)   # name -> [ {radar:[..], kindof:[..], parent} ] in load order
HDR = re.compile(r"^(Object|ChildObject|ObjectReskin)\s+(\S+)(?:\s+(\S+))?", re.I)
ANYHDR = re.compile(r"^(Object|ChildObject|ObjectReskin|CommandSet|CommandButton|ObjectCreationList|PlayerTemplate|"
                    r"Upgrade|Weapon|Armor|Locomotor|FXList|SpecialPower|Science|AudioEvent|MappedImage|ModifierList|"
                    r"ExperienceLevel|CrateData|DialogEvent|Multisound|StreamedSound|MusicTrack|FXParticleSystem|"
                    r"ParticleSystem|EvaEvent|LivingWorld\w*)\s+\S+\s*$", re.I)
for f in files:
    lines = expand(f)
    cur_raw = None
    for p, n, s in lines:
        m = HDR.match(s)
        if m and (m.group(1).lower() == "object" or m.group(3)):
            cur_raw = {"radar": [], "kindof": [], "parent": m.group(3) if m.group(1).lower() != "object" else None,
                       "path": p, "line": n}
            raw_span[m.group(2)].append(cur_raw)
            continue
        if ANYHDR.match(s):
            cur_raw = None
            continue
        if cur_raw is not None and "=" in s:
            k, v = s.split("=", 1)
            k = k.strip().lower()
            if k == "radarpriority":
                cur_raw["radar"].append((resolve(v.strip()), os.path.relpath(p, ROOT), n))
            elif k == "kindof":
                cur_raw["kindof"].append((resolve(v.strip()), os.path.relpath(p, ROOT), n))
    i = 0
    prev_obj_end = False
    last_obj = None
    while i < len(lines):
        p, n, s = lines[i]
        toks = s.split()
        k = toks[0]
        kl = k.lower()
        if kl == "object" and len(toks) >= 2:
            o, i = parse_object(lines, i, "Object", toks[1], None, p, n)
            objects[o.name].append(o)
        elif kl in ("childobject", "objectreskin") and len(toks) >= 3:
            o, i = parse_object(lines, i, k, toks[1], toks[2], p, n)
            objects[o.name].append(o)
        elif kl in ("commandset", "commandbutton", "objectcreationlist", "playertemplate") and len(toks) >= 2:
            body, i = parse_generic(lines, i)
            blocks[kl][toks[1]].append((body, p, n))
        else:
            if prev_obj_end and "\\object\\" in f.lower():
                stray.append((os.path.relpath(p, ROOT), n, s[:60], last_obj))
            i += 1
            prev_obj_end = False
            continue
        prev_obj_end = kl in ("object", "childobject", "objectreskin")
        if prev_obj_end:
            last_obj = toks[1]

# ---------------------------------------------------------------- inheritance
def latest(name):
    lst = objects.get(name)
    return lst[-1] if lst else None

resolved_cache = {}
def resolved(name, stack=()):
    if name in resolved_cache:
        return resolved_cache[name]
    o = latest(name)
    if o is None:
        return None
    if name in stack:
        return None
    if o.parent:
        par = resolved(o.parent, stack + (name,))
        if par is None:
            fields, mods = {}, []
        else:
            fields = {k: list(v) for k, v in par["fields"].items()}
            mods = [dict(m, via=m.get("via", par["name"])) for m in par["modules"]]
    else:
        fields, mods = {}, []
    for t in o.removes:
        mods = [m for m in mods if m["tag"] != t]
    for t, rm in o.replaces.items():
        new = dict(rm, tag=rm.get("newtag") or t, via=name)
        mods = [new if m["tag"] == t else m for m in mods]
    for k, v in o.fields.items():
        if k == "kindof":
            cur = set(resolve(fields["kindof"][-1][0]).split()) if fields.get("kindof") else set()
            for val, p, n in v:
                toks = resolve(val).split()
                if toks and all(t[0] in "+-" for t in toks):
                    for t in toks:
                        (cur.add if t[0] == "+" else cur.discard)(t[1:])
                else:
                    cur = set(toks)
            fields[k] = [(" ".join(sorted(cur)), v[-1][1], v[-1][2])]
            continue
        fields[k] = list(v)       # child's value overrides
    for m in o.modules:
        mm = dict(m, via=name)
        if m["tag"]:
            mods = [x for x in mods if x["tag"] != m["tag"]]
        mods.append(mm)
    r = {"name": name, "obj": o, "fields": fields, "modules": mods,
         "chain": [name] + (resolved(o.parent)["chain"] if o.parent and resolved(o.parent) else ([o.parent] if o.parent else []))}
    resolved_cache[name] = r
    return r

def field(r, key):
    v = r["fields"].get(key.lower())
    if not v:
        return None
    val, p, n = v[-1]
    return resolve(val), os.path.relpath(p, ROOT), n, val

# ---------------------------------------------------------------- resource buildings
pt_body = {n: lst[-1][0] for n, lst in blocks["playertemplate"].items()}
filt = defines.get("RESOURCE_MODIFIER_OBJECT_FILTER", "")
filter_members = {t[1:] for t in filt.split() if t.startswith("+")}

RES_TYPES = {"autodepositupdate", "cashovertime"}
def is_resource(r):
    why = []
    for m in r["modules"]:
        if (m["type"] or "").lower() in RES_TYPES:
            why.append(m["type"])
    if r["name"] in filter_members:
        why.append("FILTER")
    kf = field(r, "kindof")
    if kf and "FS_CASH_PRODUCER" in kf[0].split():
        why.append("FS_CASH_PRODUCER")
    return why

all_res = {}
for name in objects:
    r = resolved(name)
    if r is None:
        continue
    w = is_resource(r)
    if w:
        all_res[name] = w

# ---------------------------------------------------------------- reachability per faction
obj_names = set(objects)
cs_names = set(blocks["commandset"])
btn_names = set(blocks["commandbutton"])
ocl_names = set(blocks["objectcreationlist"])

identity_all = set()
pt_upgrades = {}
for n, body in pt_body.items():
    ups = []
    for s, p, l in body:
        if key_of(s).lower() == "initialupgrades":
            ups += resolve(s.split("=", 1)[1]).split()
    pt_upgrades[n] = ups
    for u in ups:
        if u not in ("Upgrade_MenFaction",):
            identity_all.add(u)
identity_all |= {u + "AI" for u in list(identity_all)}
identity_all |= {"Upgrade_GondorAI", "Upgrade_ClassicAngmar", "Upgrade_ClassicAngmarAI"}

FOLLOW_KEYS = {"commandset", "commandsetalt", "replacewith", "castletounpackforfaction", "buildvariations",
               "objectnames", "spawntemplatename", "upgradeobject", "ocl", "object", "objectname",
               "creationlist", "templatename", "unpackobject", "newobject", "spawnobject",
               "deathobject", "oclname", "objecttemplate", "unittemplate"}

def module_allowed(m, mine):
    trig, conf = [], []
    for s, p, n in m["lines"]:
        k = key_of(s).lower()
        if k == "triggeredby" and "=" in s:
            trig += resolve(s.split("=", 1)[1]).split()
        if k == "conflictswith" and "=" in s:
            conf += resolve(s.split("=", 1)[1]).split()
    if set(conf) & mine:
        return False
    idt = set(trig) & identity_all
    if idt and not (idt & mine):
        return False
    return True

def refs_from_lines(lines, mine, faction):
    out = []
    for s, p, n in lines:
        if "=" not in s:
            continue
        k, v = s.split("=", 1)
        k = k.strip().lower()
        v = resolve(v)
        if k == "castletounpackforfaction":
            t = v.split()
            if len(t) >= 2 and t[0] != faction:
                continue
            out += t[1:]
            continue
        if k in FOLLOW_KEYS or re.match(r"^\d+$", k):
            for t in re.split(r"[\s:]+", v):
                out.append(t)
    return out

def walk(faction, roots):
    mine = set(pt_upgrades.get(faction, []))
    mine |= {u + "AI" for u in mine}
    seen = set()
    path = {}
    q = collections.deque((r, None) for r in roots)
    while q:
        x, frm = q.popleft()
        if x in seen:
            continue
        if "wotr" in x.lower():
            continue
        if x not in obj_names and x not in cs_names and x not in btn_names and x not in ocl_names:
            continue
        seen.add(x)
        path[x] = frm
        nxt = []
        if x in obj_names:
            r = resolved(x)
            if r:
                for k, vs in r["fields"].items():
                    if k in FOLLOW_KEYS:
                        for v, p, n in vs:
                            nxt += refs_from_lines([(f"{k} = {v}", p, n)], mine, faction)
                for m in r["modules"]:
                    if not module_allowed(m, mine):
                        continue
                    nxt += refs_from_lines(m["lines"], mine, faction)
        for typ in ("commandset", "commandbutton", "objectcreationlist"):
            if x in blocks[typ]:
                body = blocks[typ][x][-1][0]
                nxt += refs_from_lines(body, mine, faction)
        for y in nxt:
            if y not in seen:
                q.append((y, x))
    return seen, path

NEW = ["FactionArthedain", "FactionNumenor", "FactionIthilien", "FactionDolAmroth", "FactionHornburg",
       "FactionEredLuin", "FactionLindon", "FactionShire", "FactionDorwinion", "FactionMorgul",
       "FactionAnnatar", "FactionWhiteHand", "FactionGundabad", "FactionMoria", "FactionGuldur",
       "FactionRhudaur", "FactionCarnDum", "FactionRhun"]
CHECK = NEW + ["FactionMen"]

def roots_of(faction):
    rs = []
    for s, p, n in pt_body[faction]:
        k = key_of(s).lower()
        if k in ("startingbuilding",) or k.startswith("startingunit") and "offset" not in k:
            if "=" in s:
                rs += resolve(s.split("=", 1)[1]).split()[:1]
    return rs

def radar_of(r):
    f = field(r, "radarpriority")
    return f

out = {"stray": stray, "factions": {}, "all_res": {}, "unbalanced": [], "dupes": {}, "include_missing": include_missing}
for name in sorted(objects):
    for o in objects[name]:
        if name in {s_[3] for s_ in stray}:
            o.unbalanced = True
        if o.unbalanced:
            out["unbalanced"].append((name, os.path.relpath(o.path, ROOT), o.line))
    if len(objects[name]) > 1:
        out["dupes"][name] = [(o.kind, os.path.relpath(o.path, ROOT), o.line) for o in objects[name]]

def raw_resolve(name, key):
    seen = set()
    x = name
    while x and x not in seen:
        seen.add(x)
        lst = raw_span.get(x)
        if not lst:
            return None, None
        d = lst[-1]
        if d[key]:
            return d[key][-1], x
        x = d["parent"]
    return None, None

def describe(name):
    r = resolved(name)
    rp = radar_of(r)
    kf = field(r, "kindof")
    vr = field(r, "visionrange")
    src_obj = None
    if rp:
        # find which object in the chain set it
        for c in r["chain"]:
            o = latest(c)
            if o and "radarpriority" in o.fields:
                src_obj = c; break
    rr, rsrc = raw_resolve(name, "radar")
    rk, _ = raw_resolve(name, "kindof")
    return {"raw_radar": rr[0] if rr else None, "raw_radar_src": f"{rsrc} @ {rr[1]}:{rr[2]}" if rr else None,
            "raw_kindof": rk[0].split() if rk else [],
            "radar": rp[0] if rp else None, "radar_src": f"{src_obj} @ {rp[1]}:{rp[2]}" if rp else None,
            "radar_raw": rp[3] if rp else None,
            "kindof": kf[0].split() if kf else [], "vision": vr[0] if vr else None,
            "chain": r["chain"], "why": all_res.get(name), "side": field(r, "side")[0] if field(r, "side") else None, "file": os.path.relpath(r["obj"].path, ROOT),
            "line": r["obj"].line, "kind": r["obj"].kind}

for name in all_res:
    out["all_res"][name] = describe(name)

for fac in CHECK:
    seen, path = walk(fac, roots_of(fac))
    res = set(x for x in seen if x in all_res)
    for y in all_res:
        ry = resolved(y)
        if ry and any(c in res for c in ry["chain"][1:]) and "wotr" not in y.lower():
            res.add(y)
            path.setdefault(y, None)
    res = sorted(res)
    out["factions"][fac] = {"roots": roots_of(fac), "upgrades": pt_upgrades.get(fac),
                            "res": res, "reach_count": len(seen),
                            "paths": {x: [] for x in res}}
    for x in res:
        chain, y = [], x
        while y is not None and len(chain) < 12:
            chain.append(y); y = path.get(y)
        out["factions"][fac]["paths"][x] = chain[::-1]

with open(_args.out, "w", encoding="utf-8") as f:
    json.dump(out, f, indent=1, default=str)
print("objects", len(objects), "res", len(all_res), "unbalanced", len(out["unbalanced"]), "stray", len(stray),
      "dupes", len(out["dupes"]), "include_missing", len(include_missing))
