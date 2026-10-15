#!/usr/bin/env python
"""Tests for mapinfo.py on synthetic inputs (no game files needed).

    python tools/test_mapinfo.py
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import mapinfo  # noqa: E402

NAMES = ["SidesList", "PlayerScriptsList", "ScriptList", "Script", "ScriptAction", "Teams",
         "playerName", "playerIsHuman", "playerFaction", "teamName", "teamOwner"]
ID = {n: i + 1 for i, n in enumerate(NAMES)}


def ascii_(s: str) -> bytes:
    return struct.pack("<H", len(s)) + s.encode()


def chunk(name: str, version: int, body: bytes) -> bytes:
    return struct.pack("<IHI", ID[name], version, len(body)) + body


def dict_(pairs: list[tuple[str, int, object]]) -> bytes:
    out = struct.pack("<H", len(pairs))
    for key, typ, value in pairs:
        out += struct.pack("<I", (ID[key] << 8) | typ)
        if typ == 0:
            out += struct.pack("<B", value)
        elif typ == 3:
            out += ascii_(value)
        else:
            raise ValueError(typ)
    return out


def side(name: str, faction: str, human: bool) -> bytes:
    return dict_([("playerName", 3, name), ("playerIsHuman", 0, int(human)),
                  ("playerFaction", 3, faction)]) + struct.pack("<I", 0)


def script(name: str, strings: list[str]) -> bytes:
    action = struct.pack("<II", 7, len(strings))
    for s in strings:
        action += struct.pack("<I", 21) + struct.pack("<if", 0, 0.0) + ascii_(s)
    body = ascii_(name) + ascii_("") + ascii_("") + ascii_("") + bytes(6) + struct.pack("<I", 0)
    return chunk("Script", 2, body + chunk("ScriptAction", 3, action))


def synthetic_map() -> bytes:
    table = b"".join(struct.pack("<B", len(n)) + n.encode() + struct.pack("<I", ID[n]) for n in NAMES)
    sides = chunk("SidesList", 6, b"\x01" + struct.pack("<I", 2)
                  + side("SkirmishMen", "FactionMen", False)
                  + side("Player_1", "FactionCivilian", True))
    teams = chunk("Teams", 1, struct.pack("<I", 1)
                  + dict_([("teamName", 3, "SkirmishMen_Attack"), ("teamOwner", 3, "SkirmishMen")]))
    scripts = chunk("PlayerScriptsList", 1,
                    chunk("ScriptList", 1, script("T_Spawn", ["GondorSoldier"]))
                    + chunk("ScriptList", 1, b""))
    return b"CkMp" + struct.pack("<I", len(NAMES)) + table + sides + teams + scripts


def refpack_literals(data: bytes) -> bytes:
    """A RefPack stream made of literal blocks only (valid, uncompressed)."""
    out = bytearray(b"\x10\xfb" + len(data).to_bytes(3, "big"))
    pos = 0
    while len(data) - pos >= 4:
        n = min(112, (len(data) - pos) // 4 * 4)
        out.append(0xE0 + n // 4 - 1)
        out += data[pos:pos + n]
        pos += n
    out.append(0xFC + (len(data) - pos))
    out += data[pos:]
    return bytes(out)


def main() -> int:
    failures = []

    def check(cond: bool, what: str) -> None:
        if not cond:
            failures.append(what)
            print("FAIL:", what)

    raw = synthetic_map()
    info = mapinfo.parse(raw)
    check([s.name for s in info.sides] == ["SkirmishMen", "Player_1"], "side names")
    check([s.faction for s in info.sides] == ["FactionMen", "FactionCivilian"], "side factions")
    check([s.is_human for s in info.sides] == [False, True], "playerIsHuman")
    check(len(info.sides[0].scripts) == 1 and info.sides[0].scripts[0].name == "T_Spawn", "script name")
    check("GondorSoldier" in info.sides[0].scripts[0].strings, "script string parameter")
    check(info.sides[1].scripts == [], "second side has no scripts")
    check(info.sides[0].teams == ["SkirmishMen_Attack"], "team owned by the side")

    packed = b"EAR\0" + struct.pack("<I", len(raw)) + refpack_literals(raw)
    check(mapinfo.decode(packed) == raw, "RefPack literal stream round trip")

    print("FAILED" if failures else "OK")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
