#!/usr/bin/env python
"""Verification harness for the BFME2 / ROTWK faction unlocker (not shipped).

For every supported game it loads the clean baseline image, locates every
patch site through its wildcard signature, asserts the original bytes and
RVAs, disassembles each site before/after patching, and reads the game's Side
name table the way the DLL does. Images listed in a game's `also_verify` (for
example Age of the Ring's modified ROTWK game.dat) must accept the same
signatures at the same sites. It then (re)generates the C tables the DLL is
built from:

    dll/src/patch_table.c        signature tables compiled into the DLL
    dll/tests/baseline_expect.h  baseline RVAs/bytes the C test harness checks

Usage:
    python tools/verify_sites.py                 # verify + check headers are current
    python tools/verify_sites.py --write         # verify + regenerate headers
    python tools/verify_sites.py --game PATH     # verify one more image (no header I/O)

A baseline that is not on this machine is reported and skipped, but headers
are only checked/written when every baseline was verified. Exit code 0 only
if every check passes. Read-only on every image.
"""

from __future__ import annotations

import argparse
import dataclasses
import re
import struct
import sys
from pathlib import Path

import capstone
import pefile

ROOT = Path(__file__).resolve().parent.parent
PATCH_TABLE_H = ROOT / "dll" / "src" / "patch_table.c"
BASELINE_EXPECT_H = ROOT / "dll" / "tests" / "baseline_expect.h"

KIND_JCC8_TO_JMP = "FU_KIND_JCC8_TO_JMP"    # 7x rel8        -> EB rel8
KIND_JCC8_NOP = "FU_KIND_JCC8_NOP"          # 7x rel8        -> 90 90
KIND_JCC32_NOP = "FU_KIND_JCC32_NOP"        # 0F 8x rel32    -> 90 x6
KIND_JCC32_TO_JMP = "FU_KIND_JCC32_TO_JMP"  # 0F 8x rel32    -> E9 rel32+1 90
KIND_IMM8_CLEAR = "FU_KIND_IMM8_CLEAR"      # imm8           -> 00
KIND_ENTRY_HOOK = "FU_KIND_ENTRY_HOOK"      # prologue       -> E9 stub, CC pad (stolen bytes -> trampoline)
KIND_CALL_HOOK = "FU_KIND_CALL_HOOK"        # E8 rel32       -> E8 stub (stub continues at the old callee)
HOOK_KINDS = (KIND_ENTRY_HOOK, KIND_CALL_HOOK)

HOOK_NONE = "FU_HOOK_NONE"
HOOK_SIDE_ALIAS = "FU_HOOK_SIDE_ALIAS"  # swap a `const AsciiString &side` argument for its donor
HOOK_SKIRMISH_THIS = "FU_HOOK_SKIRMISH_THIS"  # entry hook: `this+0x58` is the Side; swap `this`
HOOK_SKIRMISH_ARG = "FU_HOOK_SKIRMISH_ARG"    # swap a `const AsciiString &side` argument for the resolved Side


@dataclasses.dataclass(frozen=True)
class Game:
    key: str                      # C identifier suffix
    title: str
    version: tuple[int, int, int, int]
    timestamp: int
    baseline: Path
    native_sides: tuple[str, ...]  # the baseline's Side table, in order
    also_verify: tuple[Path, ...] = ()


GAMES = [
    Game(
        key="bfme2_106",
        title="BFME2 1.06",
        version=(1, 6, 2429, 30210),
        timestamp=0x00564544,
        baseline=Path(r"C:\repositories\Open-BFME-2\baselines\bfme2\workshop-vanilla-1.06\files\game.dat"),
        native_sides=("Men", "Elves", "Dwarves", "Isengard", "Mordor", "Wild", "Neutral", "Goblins"),
    ),
    Game(
        key="rotwk_201",
        title="ROTWK 2.01",
        version=(2, 1, 2614, 37001),
        timestamp=0x460DA09E,
        # The unprotected 2.01 executable Age of the Ring ships as game820.dat.
        # Retail game.dat is SecuROM-encrypted on disk and cannot be patched.
        baseline=Path(r"C:\Jogos\AgeoftheRing\rotwk\game820.dat"),
        native_sides=("Men", "Elves", "Dwarves", "Isengard", "Mordor", "Wild", "Angmar", "Arnor",
                      "Neutral", "Goblins"),
        # Age of the Ring 2.02's own patched game.dat (14-entry Side table, ~25 hooks).
        also_verify=(Path(r"C:\Jogos\AgeoftheRing\rotwk\game.dat"),),
    ),
]
GAME_BY_KEY = {g.key: g for g in GAMES}


@dataclasses.dataclass(frozen=True)
class Site:
    kind: str
    signature: str          # wildcard pattern, '|' marks the patch site
    expect: str             # wildcard pattern the site must match before writing
    rva: int                # where the signature lands in the baseline image
    original: str           # exact baseline bytes at the site
    patched: str = ""       # exact bytes after patching the baseline ("" for hooks)
    target: int | None = None        # call hooks: original callee RVA
    doc_rva: int | None = None       # RVA in side-string-lifecycle.md §3, if it differs
    doc_patched: str | None = None   # patched bytes in the doc, if they differ


@dataclasses.dataclass(frozen=True)
class Patch:
    id: str
    name: str
    default_on: bool
    summary: str
    sites: dict[str, Site]           # game key -> site
    hook: str = HOOK_NONE
    arg_slot: int = 0                # hook argument: dword index from the entry esp (1 = first)
    steal: int = 0                   # entry hooks: prologue bytes moved to the trampoline
    needs_sides: bool = False        # installed only when [Sides] has entries
    group: int = 0                   # nonzero: installed only if every enabled member of the group is


SIG_C = ("FF 76 10 E8 ?? ?? ?? ?? 85 C0 74 ?? 8D 8D ?? ?? ?? ?? 51 8D 48 18 E8 ?? ?? ?? ?? 85 C0 "
         "| 0F 84 ?? ?? ?? ?? 56 E8 ?? ?? ?? ?? 59 8B 0D ?? ?? ?? ?? 8B F0 3B 71 18 75 ??")
SIG_C2 = ("A1 ?? ?? ?? ?? 8B 80 D4 09 00 00 85 C0 | 7E ?? 83 F8 02 7F ?? "
          "68 ?? ?? ?? ?? 8D 8D ?? ?? ?? ?? E8 ?? ?? ?? ?? 85 C0 74 ?? 68")
SIG_E = "8B 4B 10 6B C9 14 8D 44 01 54 8D 70 08 39 7E 04 | 0F 84 ?? ?? ?? ?? 8D 45 ?? 50 8D 4D ?? E8"
# The Side -> faction id lookup. The table size is wildcarded: 7 in BFME2,
# 9 in ROTWK, 14 in Age of the Ring.
SIG_S1 = ("| 8B 4C 24 04 68 ?? ?? ?? ?? E8 ?? ?? ?? ?? 85 C0 75 04 6A 05 58 C3 56 33 F6 "
          "FF 34 B5 ?? ?? ?? ?? 8B 4C 24 0C E8 ?? ?? ?? ?? 85 C0 74 0B 46 83 FE ?? 7C E6 "
          "6A ?? 58 5E C3")
# findSkirmishSide: `mov eax, imm32; call __EH_prolog`, then this+0x58 (the
# Side) is copied once; the loop over TheSidesList uses globals only.
SIG_S2 = ("| B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 EC 0C A1 ?? ?? ?? ?? 53 56 8B B0 C0 07 00 00 "
          "83 C1 58 57 51 8D 4D EC 89 75 E8 E8")
# Player::initFromDict's inline Side match: template Side (+0x18) vs this+0x58.
SIG_S4 = ("8B 0D ?? ?? ?? ?? 50 E8 ?? ?? ?? ?? ?? ?? 74 ?? 8D 4E 58 51 8D 48 18 "
          "| E8 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ??")
SIG_S3 = ("| 53 56 8B F1 8B 0D ?? ?? ?? ?? 85 C9 57 74 ?? E8 ?? ?? ?? ?? 84 C0 74 ?? "
          "8B 46 18 83 F8 FF 0F 84 ?? ?? ?? ?? E9 ?? ?? ?? ?? 8B 5C 24 10 68")

# Where the DLL reads the Side table, relative to the start of SIDE_TABLE_PATCH's
# signature: the `push [esi*4 + table]` operand, the `cmp esi, n` count, and
# the special-cased name (`push "Goblins"`, faction id 5).
SIDE_TABLE_PATCH = "S1"
SIDE_TABLE_OFF = 0x1C
SIDE_COUNT_OFF = 0x30
SIDE_SPECIAL_OFF = 0x05
SIDE_SPECIAL_ID = 5
SIDE_MAX = 32

PATCHES = [
    Patch(
        id="A",
        name="KeepSelectedFaction",
        default_on=True,
        summary="GameSlot::setPlayerTemplate keeps a faction index that is not in the playable set",
        sites={
            "bfme2_106": Site(
                kind=KIND_JCC8_TO_JMP,
                signature="8B 1E 8D 45 08 50 8B CE E8 ?? ?? ?? ?? 3B C3 8B 4D FC | 75 06 8B 43 08 8B 78 10 5B",
                expect="75 06", rva=0x00400E65, original="75 06", patched="EB 06"),
            "rotwk_201": Site(
                kind=KIND_JCC8_TO_JMP,
                signature="8B 3E 8D 45 08 50 8B CE E8 ?? ?? ?? ?? 3B C7 | 75 09 8B 47 08 8B 40 10 89 45 08 A1",
                expect="75 09", rva=0x00402C95, original="75 09", patched="EB 09"),
        },
    ),
    Patch(
        id="B",
        name="NoRandomFallback",
        default_on=False,
        summary="GameSlot::setPlayerTemplate no longer turns Random into the first playable faction",
        sites={
            "bfme2_106": Site(
                kind=KIND_IMM8_CLEAR,
                signature="5B A1 ?? ?? ?? ?? F6 80 D4 09 00 00 | 03 74 ?? 83 FF FF 75 ?? 8B 06 8B 40 08 8B 78 10 89 79 18",
                expect="03", rva=0x00400E79, original="03", patched="00"),
            "rotwk_201": Site(
                kind=KIND_IMM8_CLEAR,
                signature=("A1 ?? ?? ?? ?? F6 80 D4 09 00 00 | 03 74 ?? 83 7D 08 FF 75 ?? "
                           "8B 06 8B 40 08 8B 40 10 89 45 08"),
                expect="03", rva=0x00402CAB, original="03", patched="00"),
        },
    ),
    Patch(
        id="C",
        name="AllowDuplicateSides",
        default_on=True,
        summary="PlayerTemplate parse keeps playable templates whose Side is already in the playable set",
        sites={
            "bfme2_106": Site(kind=KIND_JCC32_NOP, signature=SIG_C, expect="0F 84 ?? ?? ?? ??",
                              rva=0x001FF1F9, original="0F 84 42 FF FF FF", patched="90 90 90 90 90 90"),
            "rotwk_201": Site(kind=KIND_JCC32_NOP, signature=SIG_C, expect="0F 84 ?? ?? ?? ??",
                              rva=0x001FEA5F, original="0F 84 42 FF FF FF", patched="90 90 90 90 90 90"),
        },
    ),
    Patch(
        id="C2",
        name="NoSideRestrictionMode",
        default_on=False,
        summary="PlayerTemplate parse ignores the Dwarves/Wild-only restriction mode (GlobalData+0x9D4 = 1..2)",
        sites={
            "bfme2_106": Site(kind=KIND_JCC8_TO_JMP, signature=SIG_C2, expect="7E ??",
                              rva=0x001FF151, original="7E 30", patched="EB 30"),
            "rotwk_201": Site(kind=KIND_JCC8_TO_JMP, signature=SIG_C2, expect="7E ??",
                              rva=0x001FE9B7, original="7E 30", patched="EB 30"),
        },
    ),
    Patch(
        id="D",
        name="LobbyIgnoreMapFactions",
        default_on=True,
        summary="lobby faction dropdown ignores the start position's AllowedFactions",
        sites={
            "bfme2_106": Site(
                kind=KIND_JCC8_NOP,
                signature=("80 BE 51 01 00 00 00 74 ?? 80 BE 50 01 00 00 00 75 ?? 83 7D ?? 00 74 ?? "
                           "8D 46 18 50 FF 75 ?? E8 ?? ?? ?? ?? 84 C0 59 59 | 74 ?? 8D 45 ?? 50 8B CE E8"),
                expect="74 ??", rva=0x004426DA, original="74 4F", patched="90 90",
                doc_rva=0x00441E62, doc_patched="EB 04"),
            # ROTWK: same helper (0x00444548 = 1.06 0x00441E4D), a long je, and an
            # extra check before it that can skip the helper.
            "rotwk_201": Site(
                kind=KIND_JCC32_NOP,
                signature=("80 BE 51 01 00 00 00 0F 84 ?? ?? ?? ?? 80 BE 50 01 00 00 00 0F 85 ?? ?? ?? ?? "
                           "83 7D ?? 00 74 ?? 8B 4D ?? E8 ?? ?? ?? ?? 84 C0 75 ?? "
                           "8D 46 18 50 FF 75 ?? E8 ?? ?? ?? ?? 84 C0 59 59 | 0F 84 ?? ?? ?? ?? 8D 45 ?? 50 8B CE E8"),
                expect="0F 84 ?? ?? ?? ??", rva=0x00444DFA, original="0F 84 BE 00 00 00",
                patched="90 90 90 90 90 90"),
        },
    ),
    Patch(
        id="E",
        name="RandomIgnoreMapFactions",
        default_on=True,
        summary="AI/Random faction pick ignores the start position's AllowedFactions",
        sites={
            "bfme2_106": Site(kind=KIND_JCC32_TO_JMP, signature=SIG_E, expect="0F 84 ?? ?? ?? ??",
                              rva=0x00244632, original="0F 84 8B 00 00 00", patched="E9 8C 00 00 00 90"),
            "rotwk_201": Site(kind=KIND_JCC32_TO_JMP, signature=SIG_E, expect="0F 84 ?? ?? ?? ??",
                              rva=0x0022D5C0, original="0F 84 8B 00 00 00", patched="E9 8C 00 00 00 90"),
        },
    ),
    # --- [Sides] aliases: a new Side behaves like its donor where the engine keys off the name.
    Patch(
        id="S1",
        name="SideFactionId",
        default_on=True,
        summary="faction id of a Side (Create-A-Hero masks, rank icons, stats): new Sides get the donor's id",
        hook=HOOK_SIDE_ALIAS, arg_slot=1, steal=9, needs_sides=True,
        sites={
            "bfme2_106": Site(kind=KIND_ENTRY_HOOK, signature=SIG_S1, expect="8B 4C 24 04 68 ?? ?? ?? ??",
                              rva=0x0033A3F4, original="8B 4C 24 04 68 CC EB C0 00"),
            "rotwk_201": Site(kind=KIND_ENTRY_HOOK, signature=SIG_S1, expect="8B 4C 24 04 68 ?? ?? ?? ??",
                              rva=0x0033BDA3, original="8B 4C 24 04 68 24 52 C2 00"),
        },
    ),
    Patch(
        id="S2",
        name="SkirmishSlot",
        default_on=True,
        summary=("which map skirmish player an AI copies its build list and teams from: "
                 "own Side, else the [Sides] donor, else the first game Side the map has"),
        hook=HOOK_SKIRMISH_THIS, steal=5, group=1,
        sites={
            "bfme2_106": Site(kind=KIND_ENTRY_HOOK, signature=SIG_S2, expect="B8 ?? ?? ?? ??",
                              rva=0x002AC4D4, original="B8 E6 59 B7 00"),
            "rotwk_201": Site(kind=KIND_ENTRY_HOOK, signature=SIG_S2, expect="B8 ?? ?? ?? ??",
                              rva=0x002ACFA3, original="B8 45 9E B8 00"),
        },
    ),
    Patch(
        id="S3",
        name="SideRankPoints",
        default_on=True,
        summary="per-Side rank skill-point thresholds: new Sides use the donor's values",
        hook=HOOK_SIDE_ALIAS, arg_slot=1, steal=10, needs_sides=True,
        sites={
            "bfme2_106": Site(kind=KIND_ENTRY_HOOK, signature=SIG_S3, expect="53 56 8B F1 8B 0D ?? ?? ?? ??",
                              rva=0x00200157, original="53 56 8B F1 8B 0D 8C E7 DF 00"),
            "rotwk_201": Site(kind=KIND_ENTRY_HOOK, signature=SIG_S3, expect="53 56 8B F1 8B 0D ?? ?? ?? ??",
                              rva=0x001FFA44, original="53 56 8B F1 8B 0D 2C 41 DE 00"),
        },
    ),
    Patch(
        id="S4",
        name="SkirmishAI",
        default_on=True,
        summary=("whether an AI player gets a skirmish AI: a Side the map has no skirmish player for "
                 "is matched like S2 instead of being made nonskirmish"),
        hook=HOOK_SKIRMISH_ARG, arg_slot=1, group=1,
        sites={
            "bfme2_106": Site(kind=KIND_CALL_HOOK, signature=SIG_S4, expect="E8 ?? ?? ?? ??",
                              rva=0x002AFE61, original="E8 70 6B D5 FF", target=0x000069D6),
            "rotwk_201": Site(kind=KIND_CALL_HOOK, signature=SIG_S4, expect="E8 ?? ?? ?? ??",
                              rva=0x002B096A, original="E8 3B 5C D5 FF", target=0x000065AA),
        },
    ),
]


# LAN slot colour/template codec. Local offsets and ESI/+0C/+18 accesses
# are fixed deliberately: the adapters depend on these exact caller layouts.
_NETWORK_SITES = [
    ("NW1", "NetworkWriteHuman", "WRITE",
     "8B 4E 0C 6A FE 89 45 0C 8B 46 18 6A FF 50 51 E8 ?? ?? ?? ?? 88 45 DC 57 FF 75 DC FF 75 0C | E8 ?? ?? ?? ?? 8B 4E 10",
     (0x447E91, "E8 FB F9 FF FF", 0x447891), (0x44AB5E, "E8 D4 F9 FF FF", 0x44A537)),
    ("NW2", "NetworkWriteAI", "WRITE",
     "8B 4E 0C 6A FE 89 45 0C 8B 46 18 6A FF 50 51 E8 ?? ?? ?? ?? 88 45 D4 57 FF 75 D4 FF 75 0C | E8 ?? ?? ?? ?? 8B 4E 10",
     (0x447F3F, "E8 4D F9 FF FF", 0x447891), (0x44AC0C, "E8 26 F9 FF FF", 0x44A537)),
    ("NR1", "NetworkReadHuman", "READ",
     "FF 75 F0 8D 45 98 50 57 | E8 ?? ?? ?? ?? 8B D8 83 C4 0C 3B 5D EC",
     (0x44877C, "E8 19 F2 FF FF", 0x44799A), (0x44B445, "E8 F6 F1 FF FF", 0x44A640)),
    ("NR2", "NetworkReadAI", "READ",
     "FF 75 F0 8D 45 9C 50 57 | E8 ?? ?? ?? ?? 8B D8 83 C4 0C 3B 5D EC",
     (0x448A73, "E8 22 EF FF FF", 0x44799A), (0x44B73C, "E8 FF EE FF FF", 0x44A640)),
    ("NS1", "NetworkSplitHuman", "SPLIT",
     "8D 45 E6 50 8D 45 E4 50 6A FE 6A FF FF 75 98 89 5D EC | E8 ?? ?? ?? ?? 83 C4 14",
     (0x4487AA, "E8 F6 35 12 00", 0x56BDA5), (0x44B473, "E8 41 84 12 00", 0x5738B9)),
    ("NS2", "NetworkSplitAI", "SPLIT",
     "8D 45 E5 50 8D 45 E3 50 6A FE 6A FF FF 75 9C 89 5D EC | E8 ?? ?? ?? ?? 83 C4 14",
     (0x448AA1, "E8 FF 32 12 00", 0x56BDA5), (0x44B76A, "E8 4A 81 12 00", 0x5738B9)),
    ("NW3", "NetworkWriteHumanStart", "WRITE_START",
     "8B 4E 10 83 C4 40 57 51 50 | E8 ?? ?? ?? ?? 8B 4E 1C 57 51 50 E8 ?? ?? ?? ?? 8B 4E 20",
     (0x447E9F, "E8 ED F9 FF FF", 0x447891), (0x44AB6C, "E8 C6 F9 FF FF", 0x44A537)),
    ("NW4", "NetworkWriteAIStart", "WRITE_START",
     "8B 4E 10 57 51 50 | E8 ?? ?? ?? ?? 8B 4E 1C 57 51 50 E8 ?? ?? ?? ?? 8B 4E 20 83 C4 40",
     (0x447F4A, "E8 42 F9 FF FF", 0x447891), (0x44AC17, "E8 1B F9 FF FF", 0x44A537)),
    ("NR3", "NetworkReadHumanStart", "READ_START",
     "FF 75 F0 8D 45 E8 50 53 | E8 ?? ?? ?? ?? 83 C4 0C 3B 45 EC 0F 86 ?? ?? ?? ?? 3B 45 F0",
     (0x44881A, "E8 7B F1 FF FF", 0x44799A), (0x44B4E3, "E8 58 F1 FF FF", 0x44A640)),
    ("NR4", "NetworkReadAIStart", "READ_START",
     "FF 75 F0 8D 45 CC 50 53 | E8 ?? ?? ?? ?? 83 C4 0C 3B 45 EC 0F 86 ?? ?? ?? ?? 3B 45 F0",
     (0x448B11, "E8 84 EE FF FF", 0x44799A), (0x44B7DA, "E8 61 EE FF FF", 0x44A640)),

]
for ident, name, handler, signature, bfme, rotwk in _NETWORK_SITES:
    PATCHES.append(Patch(
        id=ident, name=name, default_on=True,
        summary="LAN protocol 1: extended faction/colour/start encoding; all players need unlocker 0.5.0 or compatible",
        hook="FU_HOOK_NETWORK_" + handler, arg_slot=1, group=2,
        sites={key: Site(kind=KIND_CALL_HOOK, signature=signature, expect="E8 ?? ?? ?? ??",
                         rva=row[0], original=row[1], target=row[2])
               for key, row in (("bfme2_106", bfme), ("rotwk_201", rotwk))},
    ))


# --------------------------------------------------------------------------- patterns


def parse_pattern(text: str) -> tuple[list[int | None], int]:
    """Returns (bytes-with-None-wildcards, site offset). Offset is 0 if no '|'."""
    out: list[int | None] = []
    site = None
    for tok in text.split():
        if tok == "|":
            if site is not None:
                raise ValueError(f"two site markers in {text!r}")
            site = len(out)
        elif tok == "??":
            out.append(None)
        else:
            out.append(int(tok, 16))
    return out, (site or 0)


def strip_marker(text: str) -> str:
    return " ".join(t for t in text.split() if t != "|")


def matches_at(buf: bytes, pos: int, pat: list[int | None]) -> bool:
    if pos < 0 or pos + len(pat) > len(buf):
        return False
    return all(p is None or buf[pos + i] == p for i, p in enumerate(pat))


def find_all(buf: bytes, start: int, end: int, pat: list[int | None]) -> list[int]:
    anchor = next(i for i, p in enumerate(pat) if p is not None)
    needle = bytes([pat[anchor]])
    hits = []
    pos = buf.find(needle, start + anchor, end)
    while pos != -1:
        cand = pos - anchor
        if cand + len(pat) <= end and matches_at(buf, cand, pat):
            hits.append(cand)
        pos = buf.find(needle, pos + 1, end)
    return hits


def hexbytes(b: bytes) -> str:
    return " ".join(f"{x:02X}" for x in b)


def compute_patch(kind: str, orig: bytes) -> bytes:
    """Python mirror of build_patch() in dll/src/engine.c."""
    if kind == KIND_JCC8_TO_JMP:
        assert len(orig) == 2 and 0x70 <= orig[0] <= 0x7F
        return bytes([0xEB, orig[1]])
    if kind == KIND_JCC8_NOP:
        assert len(orig) == 2 and 0x70 <= orig[0] <= 0x7F
        return b"\x90\x90"
    if kind == KIND_JCC32_NOP:
        assert len(orig) == 6 and orig[0] == 0x0F and 0x80 <= orig[1] <= 0x8F
        return b"\x90" * 6
    if kind == KIND_JCC32_TO_JMP:
        assert len(orig) == 6 and orig[0] == 0x0F and 0x80 <= orig[1] <= 0x8F
        rel = struct.unpack_from("<i", orig, 2)[0]
        return b"\xE9" + struct.pack("<i", rel + 1) + b"\x90"
    if kind == KIND_IMM8_CLEAR:
        assert len(orig) == 1
        return b"\x00"
    raise ValueError(kind)


# Hooks point into the DLL, whose address is only known at runtime. For the
# rescan, simulate them with a destination far outside any game section.
SIMULATED_STUB_RVA = 0x7FF00000


def simulate_hook(kind: str, site: int, orig: bytes) -> bytes:
    rel = struct.pack("<i", SIMULATED_STUB_RVA - (site + 5))
    if kind == KIND_ENTRY_HOOK:
        return b"\xE9" + rel + b"\xCC" * (len(orig) - 5)
    if kind == KIND_CALL_HOOK:
        return b"\xE8" + rel
    raise ValueError(kind)


RELATIVE_MNEMONICS = ("call", "jmp", "loop", "jcxz", "jecxz")


def is_relative(ins) -> bool:
    return ins.mnemonic in RELATIVE_MNEMONICS or (ins.mnemonic.startswith("j") and ins.mnemonic != "jmp")


def branches_into(img: "Image", lo: int, hi: int, scan_from: int, scan_len: int = 0x800) -> list[int]:
    """Sources of branches landing strictly inside (lo, hi): rel32 calls/jumps
    anywhere in an executable section (raw scan; this includes code other
    tools added to the image) plus every branch decoded linearly from
    `scan_from` (the hooked function body)."""
    hits = set()
    mem = bytes(img.mem)
    for start, end in img.exec_ranges:
        for m in re.finditer(rb"(?=[\xE8\xE9].{4})", mem[start:end], re.S):
            i = start + m.start()
            tgt = i + 5 + struct.unpack_from("<i", mem, i + 1)[0]
            if lo < tgt < hi:
                hits.add(i)
        for m in re.finditer(rb"(?=\x0F[\x80-\x8F].{4})", mem[start:end], re.S):
            i = start + m.start()
            tgt = i + 6 + struct.unpack_from("<i", mem, i + 2)[0]
            if lo < tgt < hi:
                hits.add(i)
    for ins in img.md.disasm(mem[scan_from:scan_from + scan_len], scan_from):
        if is_relative(ins) and ins.op_str.startswith("0x"):
            tgt = int(ins.op_str, 16)
            if lo < tgt < hi:
                hits.add(ins.address)
        if ins.mnemonic in ("int3",):
            break
    return sorted(hits)


# --------------------------------------------------------------------------- image


class Image:
    def __init__(self, path: Path):
        self.path = path
        self.pe = pefile.PE(str(path))
        self.mem = bytearray(self.pe.get_memory_mapped_image())
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        text = next(s for s in self.pe.sections if s.Name.rstrip(b"\0") == b".text")
        self.text_start = text.VirtualAddress
        self.text_end = text.VirtualAddress + text.Misc_VirtualSize
        self.exec_ranges = [(s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize)
                            for s in self.pe.sections if s.Characteristics & 0x20000000]
        self.section_names = [s.Name.rstrip(b"\0").decode("latin1") for s in self.pe.sections]
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    def version(self) -> tuple[tuple[int, int, int, int] | None, str | None]:
        ver = None
        internal = None
        if hasattr(self.pe, "VS_FIXEDFILEINFO"):
            f = self.pe.VS_FIXEDFILEINFO[0]
            ver = (f.FileVersionMS >> 16, f.FileVersionMS & 0xFFFF,
                   f.FileVersionLS >> 16, f.FileVersionLS & 0xFFFF)
        for fi in getattr(self.pe, "FileInfo", [[]])[0]:
            for st in getattr(fi, "StringTable", []):
                v = st.entries.get(b"InternalName")
                if v is not None:
                    internal = v.decode("utf-8", "replace").strip()
        return ver, internal

    def c_string(self, va: int, limit: int = 31) -> str | None:
        """The NUL-terminated printable ASCII string at a VA, as the DLL reads it."""
        rva = va - self.base
        if not 0 <= rva < len(self.mem):
            return None
        end = self.mem.find(b"\0", rva, rva + limit + 1)
        if end <= rva:
            return None
        raw = bytes(self.mem[rva:end])
        return raw.decode("ascii") if all(0x21 <= c <= 0x7E for c in raw) else None

    def disasm_site(self, buf: bytes, insn_start: int, site: int, length: int) -> str:
        """Instructions overlapping [site, site+length), decoding from an
        instruction boundary at or before the site (signatures start on one)."""
        code = bytes(buf[insn_start:site + length + 16])
        out = []
        for ins in self.md.disasm(code, self.base + insn_start):
            rva = ins.address - self.base
            if rva + ins.size <= site:
                continue
            if rva >= site + length:
                break
            out.append(f"{ins.mnemonic} {ins.op_str}".strip())
        return " ; ".join(out)


# --------------------------------------------------------------------------- checks


class Checker:
    def __init__(self) -> None:
        self.failures: list[str] = []

    def check(self, cond: bool, msg: str) -> bool:
        if not cond:
            self.failures.append(msg)
            print(f"    FAIL: {msg}")
        return cond


def identify(ver, internal) -> Game | None:
    if internal != "BFME2" or ver is None:
        return None
    return next((g for g in GAMES if g.version[:2] == ver[:2]), None)


def verify_hook(img: Image, p: Patch, s: Site, site: int, orig: bytes, ck: Checker,
                is_baseline: bool) -> None:
    uses_this = p.hook == HOOK_SKIRMISH_THIS
    ck.check(p.hook != HOOK_NONE and (p.arg_slot >= 1) != uses_this, f"{p.id}: hook without handler/argument")
    ck.check(not uses_this or s.kind == KIND_ENTRY_HOOK, f"{p.id}: a `this` hook must be an entry hook")
    if s.kind == KIND_ENTRY_HOOK:
        # The stolen prologue is replayed from the trampoline at another address:
        # it must be whole instructions with no relative operands.
        ck.check(5 <= p.steal <= 16 and len(orig) == p.steal, f"{p.id}: bad steal length {p.steal}")
        decoded, size = [], 0
        for ins in img.md.disasm(orig, img.base + site):
            decoded.append(ins)
            size += ins.size
        ck.check(size == p.steal, f"{p.id}: stolen bytes end mid-instruction ({size} != {p.steal})")
        for ins in decoded:
            ck.check(not is_relative(ins), f"{p.id}: stolen instruction is relative: {ins.mnemonic} {ins.op_str}")
        print(f"    stolen   : {' ; '.join(f'{i.mnemonic} {i.op_str}'.strip() for i in decoded)}"
              f"  -> trampoline, then jmp RVA 0x{site + p.steal:08X}")
        into = branches_into(img, site, site + p.steal, site)
        ck.check(not into, f"{p.id}: branches land inside the stolen bytes from "
                           f"{', '.join(f'0x{x:08X}' for x in into)}")
        print(f"    patched  : E9 <stub> {'CC ' * (p.steal - 5)}  (no branch lands inside the stolen bytes)")
    else:
        target = site + 5 + struct.unpack_from("<i", orig, 1)[0]
        ck.check(img.text_start <= target < img.text_end, f"{p.id}: call target outside .text")
        print(f"    callee   : RVA 0x{target:08X} (" +
              ("replaced" if p.hook.startswith("FU_HOOK_NETWORK_") else "the stub continues there") + ")")
        print("    patched  : E8 <stub>")
        if is_baseline:
            ck.check(target == s.target, f"{p.id}: callee 0x{target:08X} != baseline 0x{s.target or 0:08X}")
    if p.hook.startswith("FU_HOOK_NETWORK_"):
        print("    handler  : " + p.hook + "; cdecl replacement; packet size unchanged")
    elif uses_this:
        print("    this     : ecx at entry; this+0x58 is the Side (the hook may substitute `this`)")
    else:
        print(f"    argument : dword {p.arg_slot} above the return address is the `const AsciiString &` Side")


def read_side_table(img: Image, sig_start: int) -> list[str | None] | None:
    """Python mirror of fu_sides_read_native() in dll/src/sides.c."""
    table = struct.unpack_from("<I", img.mem, sig_start + SIDE_TABLE_OFF)[0]
    count = img.mem[sig_start + SIDE_COUNT_OFF]
    special = struct.unpack_from("<I", img.mem, sig_start + SIDE_SPECIAL_OFF)[0]
    if not 1 <= count <= SIDE_MAX:
        return None
    names = []
    for i in range(count):
        rva = table - img.base + 4 * i
        if not 0 <= rva < len(img.mem) - 4:
            return None
        names.append(img.c_string(struct.unpack_from("<I", img.mem, rva)[0]))
    names.append(img.c_string(special))
    return names


def verify_side_table(img: Image, game: Game, sig_start: int, ck: Checker, is_baseline: bool) -> None:
    pat, _ = parse_pattern(next(p for p in PATCHES if p.id == SIDE_TABLE_PATCH).sites[game.key].signature)
    for off, n in ((SIDE_TABLE_OFF, 4), (SIDE_COUNT_OFF, 1), (SIDE_SPECIAL_OFF, 4)):
        ck.check(all(pat[off + i] is None for i in range(n)), f"side table operand at +0x{off:X} not wildcarded")
    count, count2 = img.mem[sig_start + SIDE_COUNT_OFF], img.mem[sig_start + SIDE_COUNT_OFF + 4]
    ck.check(count == count2, f"side table: loop bound {count} != miss id {count2}")
    names = read_side_table(img, sig_start)
    print(f"    sides    : {', '.join(str(n) for n in names) if names else 'unreadable'}"
          f"  ({len(names) - 1 if names else 0} in the table + special id {SIDE_SPECIAL_ID})")
    if not ck.check(names is not None and None not in names, "side table unreadable"):
        return
    if is_baseline:
        ck.check(tuple(names) == game.native_sides,
                 f"side table {names} != expected {list(game.native_sides)}")
    ck.check(names[-1] == "Goblins", f"special Side is {names[-1]!r}, not Goblins")


def verify(img: Image, game_hint: Game | None, is_baseline: bool, reference: Image | None) -> Checker:
    """`reference`: the clean baseline of the same game; a modified image must
    keep the baseline's bytes at every patch site."""
    ck = Checker()
    ver, internal = img.version()
    stamp = img.pe.FILE_HEADER.TimeDateStamp
    print(f"image   : {img.path}")
    print(f"version : {'.'.join(map(str, ver)) if ver else '?'}  InternalName={internal!r}"
          f"  TimeDateStamp=0x{stamp:08X}  ImageBase=0x{img.base:08X}")
    print(f".text   : RVA 0x{img.text_start:08X}..0x{img.text_end:08X}")
    if reference is not None:
        added = [n for n in img.section_names if n not in reference.section_names]
        print(f"modified: sections added to the clean baseline: {', '.join(added) or 'none'}")
    game = identify(ver, internal)
    if not ck.check(game is not None, "not a supported game (InternalName BFME2, FileVersion 1.6 or 2.1)"):
        return ck
    if game_hint is not None:
        ck.check(game is game_hint, f"identified as {game.title}, expected {game_hint.title}")
    print(f"game    : {game.title}")
    if is_baseline:
        ck.check(ver == game.version, "baseline FileVersion changed")
        ck.check(stamp == game.timestamp, "baseline TimeDateStamp changed")

    sites: dict[str, int] = {}
    for p in PATCHES:
        s = p.sites[game.key]
        print(f"\n[{p.id}] {p.name}{'' if p.default_on else '  (optional, default off)'}")
        print(f"    {p.summary}")
        pat, off = parse_pattern(s.signature)
        exp, _ = parse_pattern(s.expect)
        ck.check(off + len(exp) <= len(pat), f"{p.id}: expect runs past signature end")
        ck.check(all(e is None or pat[off + i] in (None, e) for i, e in enumerate(exp)),
                 f"{p.id}: expect contradicts signature")
        hits = find_all(img.mem, img.text_start, img.text_end, pat)
        print(f"    signature: {len(pat)} bytes, {sum(x is None for x in pat)} wildcards, "
              f"{len(hits)} match(es) in .text")
        if not ck.check(len(hits) == 1, f"{p.id}: signature matches {len(hits)} times, need exactly 1"):
            continue
        site = hits[0] + off
        sites[p.id] = site
        orig = bytes(img.mem[site:site + len(exp)])
        print(f"    site     : RVA 0x{site:08X}  (VA 0x{img.base + site:08X})")
        print(f"    original : {hexbytes(orig):<20} {img.disasm_site(img.mem, hits[0], site, len(orig))}")
        ck.check(matches_at(img.mem, site, exp), f"{p.id}: expect pattern mismatch at site")
        if reference is not None:
            ck.check(site == s.rva and orig == bytes(reference.mem[site:site + len(orig)]),
                     f"{p.id}: site moved or changed relative to the clean baseline")
            region = slice(hits[0], hits[0] + len(pat))
            diff = [i for i in range(region.start, region.stop) if img.mem[i] != reference.mem[i]]
            if diff:
                print(f"    modified : {len(diff)} byte(s) of the signature differ from the clean baseline "
                      f"(wildcarded), at RVA {', '.join(f'0x{i:08X}' for i in diff)}")
        if p.id == SIDE_TABLE_PATCH:
            verify_side_table(img, game, hits[0], ck, is_baseline)
        if s.kind in HOOK_KINDS:
            verify_hook(img, p, s, site, orig, ck, is_baseline)
            if is_baseline:
                ck.check(site == s.rva, f"{p.id}: site 0x{site:08X} != baseline 0x{s.rva:08X}")
                ck.check(hexbytes(orig) == s.original, f"{p.id}: original {hexbytes(orig)} != baseline {s.original}")
            continue
        patched = compute_patch(s.kind, orig)
        after = bytearray(img.mem)
        after[site:site + len(patched)] = patched
        print(f"    patched  : {hexbytes(patched):<20} {img.disasm_site(after, hits[0], site, len(patched))}")
        if s.kind in (KIND_JCC32_NOP, KIND_JCC32_TO_JMP):
            target = site + 6 + struct.unpack_from("<i", orig, 2)[0]
            ck.check(img.text_start <= target < img.text_end, f"{p.id}: branch target outside .text")
            if s.kind == KIND_JCC32_TO_JMP:
                new_target = site + 5 + struct.unpack_from("<i", patched, 1)[0]
                ck.check(new_target == target, f"{p.id}: rewritten jmp changes the branch target")
                print(f"    target   : RVA 0x{target:08X} (unchanged by the rewrite)")
        if is_baseline:
            ck.check(site == s.rva, f"{p.id}: site 0x{site:08X} != baseline 0x{s.rva:08X}")
            ck.check(hexbytes(orig) == s.original, f"{p.id}: original {hexbytes(orig)} != baseline {s.original}")
            ck.check(hexbytes(patched) == s.patched, f"{p.id}: patched {hexbytes(patched)} != baseline {s.patched}")
            if s.doc_rva is not None or s.doc_patched is not None:
                print(f"    NOTE     : differs from side-string-lifecycle.md section 3 "
                      f"(doc: RVA 0x{(s.doc_rva or site):08X} -> {s.doc_patched}); see DISCREPANCIES.md")

    # Every site byte range must be distinct, and once patched no signature may
    # match again (re-running the DLL on a patched image is a no-op).
    print("\npost-patch rescan:")
    ranges = []
    for p in PATCHES:
        if p.id in sites:
            n = len(parse_pattern(p.sites[game.key].expect)[0])
            ranges.append((sites[p.id], sites[p.id] + n, p.id))
    ranges.sort()
    for (a0, a1, ai), (b0, _b1, bi) in zip(ranges, ranges[1:]):
        ck.check(a1 <= b0, f"sites {ai} and {bi} overlap")
    patched_img = bytearray(img.mem)
    for start, end, pid in ranges:
        s = next(x for x in PATCHES if x.id == pid).sites[game.key]
        orig = bytes(img.mem[start:end])
        patched_img[start:end] = (simulate_hook(s.kind, start, orig) if s.kind in HOOK_KINDS
                                  else compute_patch(s.kind, orig))
    for p in PATCHES:
        s = p.sites[game.key]
        pat, off = parse_pattern(s.signature)
        found = find_all(patched_img, img.text_start, img.text_end, pat)
        if s.kind == KIND_CALL_HOOK and len(found) == 1:
            # The rel32 is wildcarded, so the signature still matches; the
            # engine refuses because the callee now lies outside .text.
            at = found[0] + off
            tgt = at + 5 + struct.unpack_from("<i", patched_img, at + 1)[0]
            ok = not (img.text_start <= tgt < img.text_end)
            print(f"    [{p.id}] matches after all patches applied: 1, callee outside .text -> refused")
            ck.check(ok, f"{p.id}: a hooked call site would be hooked again")
            continue
        print(f"    [{p.id}] matches after all patches applied: {len(found)}")
        ck.check(not found, f"{p.id}: signature still matches a patched image")
    return ck


# --------------------------------------------------------------------------- headers


def c_str(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def render_patch_table() -> str:
    lines = [
        "/* Generated by tools/verify_sites.py --write. Do not edit by hand. */",
        '#include "engine.h"',
        "",
        f"_Static_assert({len(PATCHES)} <= FU_MAX_PATCHES, \"raise FU_MAX_PATCHES\");",
        "",
    ]
    for g in GAMES:
        lines.append(f"static const fu_patch_def defs_{g.key}[{len(PATCHES)}] = {{")
        for p in PATCHES:
            s = p.sites[g.key]
            _, off = parse_pattern(s.signature)
            lines += [
                "    {",
                f"        {c_str(p.id)}, {c_str(p.name)}, {int(p.default_on)}, {s.kind},",
                f"        {c_str(strip_marker(s.signature))},",
                f"        {off}, {c_str(s.expect)}, 0x{s.rva:08X}u,",
                f"        {c_str(p.summary)},",
                f"        {p.hook}, {p.arg_slot}, {p.steal}, {int(p.needs_sides)}, {p.group},",
                "    },",
            ]
        lines += ["};", ""]
    side_k = next(i for i, p in enumerate(PATCHES) if p.id == SIDE_TABLE_PATCH)
    lines.append(f"const fu_game fu_games[{len(GAMES)}] = {{")
    for g in GAMES:
        lines.append(f"    {{ {c_str(g.title)}, {g.version[0]}, {g.version[1]}, 0x{g.timestamp:08X}u, "
                     f"defs_{g.key}, {len(PATCHES)},")
        lines.append(f"      {side_k}, 0x{SIDE_TABLE_OFF:02X}, 0x{SIDE_COUNT_OFF:02X}, "
                     f"0x{SIDE_SPECIAL_OFF:02X} }},")
    lines += [
        "};",
        f"const unsigned fu_game_count = {len(GAMES)};",
        "",
        f"const fu_patch_def *fu_patch_defs = defs_{GAMES[0].key};",
        f"unsigned fu_patch_count = {len(PATCHES)};",
        "",
    ]
    return "\n".join(lines)


def render_baseline_expect() -> str:
    def arr(s: str) -> str:
        return "{ " + ", ".join(f"0x{b}" for b in s.split()) + " }"

    lines = [
        "/* Generated by tools/verify_sites.py --write. Do not edit by hand. */",
        "#ifndef FU_BASELINE_EXPECT_H",
        "#define FU_BASELINE_EXPECT_H",
        "",
        "typedef struct {",
        "    const char *id;",
        "    unsigned rva;",
        "    unsigned len;",
        "    unsigned char original[16];",
        "    unsigned char patched[16];  /* all zero for hooks: runtime addresses */",
        "    int is_hook;",
        "    unsigned callee_rva;        /* call hooks: the original callee */",
        "} fu_baseline_site;",
        "",
        "typedef struct {",
        "    const char *title;          /* matches fu_games[i].title */",
        "    unsigned version[4];",
        "    const fu_baseline_site *sites;",
        "    const char *const *native_sides; /* the Side table, then the special Side */",
        "    unsigned native_count;",
        "} fu_baseline_game;",
        "",
    ]
    for g in GAMES:
        lines.append(f"static const fu_baseline_site fu_baseline_sites_{g.key}[{len(PATCHES)}] = {{")
        for p in PATCHES:
            s = p.sites[g.key]
            n = len(s.original.split())
            patched = arr(s.patched) if s.patched else "{ 0 }"
            lines.append(f"    {{ {c_str(p.id)}, 0x{s.rva:08X}u, {n}, {arr(s.original)}, {patched}, "
                         f"{int(s.kind in HOOK_KINDS)}, 0x{s.target or 0:08X}u }},")
        lines += ["};", ""]
        names = ", ".join(c_str(n) for n in g.native_sides)
        lines += [f"static const char *const fu_baseline_sides_{g.key}[] = {{ {names} }};", ""]
    lines.append(f"static const fu_baseline_game fu_baseline_games[{len(GAMES)}] = {{")
    for g in GAMES:
        ver = ", ".join(str(x) for x in g.version)
        lines.append(f"    {{ {c_str(g.title)}, {{ {ver} }}, fu_baseline_sites_{g.key}, "
                     f"fu_baseline_sides_{g.key}, {len(g.native_sides)} }},")
    lines += ["};", "", "#endif", ""]
    return "\n".join(lines)


def sync_header(path: Path, content: str, write: bool, ck: Checker) -> None:
    current = path.read_text(encoding="utf-8") if path.exists() else None
    if current == content:
        print(f"    {path.relative_to(ROOT)}: up to date")
    elif write:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8", newline="\n")
        print(f"    {path.relative_to(ROOT)}: written")
    else:
        ck.check(False, f"{path.relative_to(ROOT)} is stale; run with --write")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", type=Path, help="verify only this image (no header I/O)")
    ap.add_argument("--write", action="store_true", help="regenerate the C headers")
    ap.add_argument("--perf-queue", action="store_true", help="verify/generate only the performance queue insertion contract")
    ap.add_argument("--perf-sort", action="store_true", help="verify/generate only the performance queue sorting contract")
    args = ap.parse_args()
    if args.perf_queue or args.perf_sort:
        if args.game:
            ap.error("performance contracts verify both configured images; omit --game")
        if args.perf_queue and args.perf_sort:
            ap.error("select one performance contract at a time")
        if args.perf_queue:
            from queueinsert_table import run
        else:
            from queuesort_table import run
        return run(args.write)

    failures: list[str] = []
    if args.game:
        failures += verify(Image(args.game), None, False, None).failures
    else:
        all_baselines = True
        for g in GAMES:
            print(f"=================== {g.title}")
            if not g.baseline.exists():
                print(f"baseline {g.baseline} not on this machine: skipped")
                all_baselines = False
                continue
            ref = Image(g.baseline)
            failures += verify(ref, g, True, None).failures
            for extra in g.also_verify:
                print(f"\n------------------- {g.title}, modified image that must stay compatible")
                if not extra.exists():
                    print(f"{extra} not on this machine: skipped")
                    continue
                failures += verify(Image(extra), g, False, ref).failures
            print()
        print("headers:")
        ck = Checker()
        if all_baselines:
            sync_header(PATCH_TABLE_H, render_patch_table(), args.write, ck)
            sync_header(BASELINE_EXPECT_H, render_baseline_expect(), args.write, ck)
        else:
            print("    not checked: a baseline is missing")
        failures += ck.failures

    print()
    if failures:
        print(f"FAILED: {len(failures)} check(s)")
        return 1
    print("OK: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
