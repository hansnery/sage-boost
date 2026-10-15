# Skirmish AI for new Sides: Phase R (research) Plan

> **For agentic workers:** execute task by task; steps use checkbox (`- [ ]`)
> syntax. This plan ends at the spec's decision gate. The implementation plan
> (patch S4) is written afterwards from the findings, because its sites and
> mechanism are this plan's output.

**Goal:** answer R1-R5 of `docs/specs/2026-10-12-native-sides-design.md` and
decide whether the automatic skirmish-player fallback gets built.

**Architecture:** two read-only Python tools (a map reader, a disassembly
helper) plus findings written to `DISCREPANCIES.md`. No DLL code changes.

**Tech Stack:** Python 3, `pefile`, `capstone` (already used by
`tools/verify_sites.py`).

## Global Constraints

- Read-only outside this repository: nothing is written to
  `C:\Jogos\AgeoftheRing\`, `C:\Program Files (x86)\Realms in Exile Nightly\`,
  `D:\RotWK\` or `C:\repositories\Open-BFME-2\`.
- No game file, map or EA asset is copied into the repository; tests build
  their own synthetic inputs.
- No code from SAGE-Unlocked or bfme2-accelerator.
- Conventional commits.

## Inputs

| Name | Path |
|---|---|
| AotR exe | `C:\Jogos\AgeoftheRing\rotwk\game.dat` |
| 2.01 exe | `C:\Jogos\AgeoftheRing\rotwk\game820.dat` |
| Realms maps | `C:\Program Files (x86)\Realms in Exile Nightly\realms_nightly\maps\*\*.map` |
| ROTWK maps | inside the `.big` archives in `D:\RotWK\` |

## Map format (as observed)

- File: `EAR\0` + u32 size + RefPack stream, or raw. Raw starts with `CkMp`,
  u32 name count, then entries `u8 len, chars, u32 id`.
- Chunks: `u32 id, u16 version, u32 size, body`. Top level includes
  `SidesList` (v6), `Teams`, `PlayerScriptsList`, `BuildLists`.
- `SidesList` v6 body: `u8` flag, `u32` side count, then per side a Dict and
  `u32` build-list count (0 in the sample).
- Dict: `u16` pair count; per pair `u32` (low byte type, upper 24 bits name
  id); type 0 bool `u8`, 1 int `u32`, 2 real `f32`, 3 ASCII `u16 len + bytes`,
  4 Unicode `u16 len + UTF-16LE`.

---

### Task 1: Map reader

**Files:**
- Create: `tools/mapinfo.py`
- Test: `tools/test_mapinfo.py`

**Interfaces:**
- Produces: `load_map(path) -> bytes` (RefPack-decoded `CkMp` bytes);
  `parse(data) -> MapInfo` with `sides: list[Side]`, where `Side` has `name`,
  `faction`, `is_human`, `dict` (all pairs), `scripts: list[Script]`;
  `Script` has `name`, `strings: list[str]` (every string parameter of its
  conditions and actions); `BigArchive(path).maps() -> dict[str, bytes]`.
- CLI: `python tools/mapinfo.py <map-or-big> [--scripts]` prints one line per
  side (`name faction human scripts=N`) and, with `--scripts`, each script
  name with its string parameters.

- [ ] Step 1: write `tools/test_mapinfo.py`: it builds a synthetic raw `CkMp`
  file (name table, `SidesList` v6 with two sides `SkirmishMen`/`FactionMen`
  and `Player_1`/`FactionCivilian`, a `PlayerScriptsList` with one script
  named `T_Spawn` with one string parameter `GondorSoldier` on the first side)
  and a RefPack stream of literal-only blocks. It asserts the parsed names,
  factions, script name and string parameter, and that the RefPack decode of
  the literal stream returns the input.
- [ ] Step 2: run `python tools/test_mapinfo.py`; expect it to fail with
  `ModuleNotFoundError: mapinfo`.
- [ ] Step 3: implement `tools/mapinfo.py` (RefPack decoder, chunk walker,
  Dict reader, `SidesList` and `PlayerScriptsList` readers, BIG reader).
- [ ] Step 4: run `python tools/test_mapinfo.py`; expect `OK`.
- [ ] Step 5: run the CLI on `map mp adorn river`; expect 19 sides including
  `SkirmishMen FactionMen` and no `Arthedain`.
- [ ] Step 6: commit `feat(tools): map reader for skirmish player research`.

### Task 2: Map survey (R4)

**Files:** none committed besides the findings in Task 5.

- [ ] Step 1: run `python tools/mapinfo.py --scripts` on five Realms maps
  (`map mp adorn river`, `map mp aldamori`, and three more picked from
  different authors/sizes) and two ROTWK maps from the `.big` archives.
- [ ] Step 2: for every skirmish player record: script count, script names,
  and string parameters that name object templates, teams or factions.
- [ ] Step 3: classify each skirmish player's content as map-generic (same
  scripts on every skirmish player, no faction-specific objects) or
  faction-specific. Gate condition 2 depends on this table.

### Task 3: The matching loop and the miss path (R1, R5)

**Files:**
- Create: `tools/disasm.py`

**Interfaces:**
- CLI: `python tools/disasm.py <image> <rva> [--len N] [--callers]
  [--refs VA]` disassembles from an RVA, lists rel32 callers of an RVA, or
  finds code that references an absolute VA. Works on any image in Inputs.

- [ ] Step 1: implement `tools/disasm.py` on top of `verify_sites.Image`
  (reuse its loader and capstone setup).
- [ ] Step 2: in `game820.dat`, start from S2's site 0x002AD02E: find the
  enclosing function (2.01's `Player::rva002AC4D4`), its callers, and in the
  caller the loop over map players, the predicate deciding which map players
  are candidates, the match branch and the miss branch.
- [ ] Step 3: record what the match branch copies into the player (scripts,
  teams, build list, base marker, AI type) and what the miss branch does
  (R5: is an AI object still created; with which type).
- [ ] Step 4: check the same bytes at the same RVAs in AotR's `game.dat`.

### Task 4: Base and AIData lookups (R2, R3)

- [ ] Step 1: find the code referencing the `AIBase` INI block name, the
  `AIBase.cpp` path string and `PrimaryAIBaseMarker`; identify where an
  `AIBase` is chosen for a player and which Side string the choice compares.
- [ ] Step 2: find the `SkirmishBuildList` and `SideInfo` lookups; record
  which Side string each uses (player's Side, matched map player's, or the
  template's).
- [ ] Step 3: record whether these run for every AI player or only for
  skirmish ones (from Task 3's AI type).

### Task 5: Findings and decision

**Files:**
- Modify: `DISCREPANCIES.md` (append section 8)
- Modify: `docs/specs/2026-10-12-native-sides-design.md` (status line only)

- [ ] Step 1: write section 8: answers to R1-R5 with RVAs for 2.01 and the
  AotR check, the Task 2 table, and the gate decision with reasons.
- [ ] Step 2: set the spec status to "gate passed: implementation plan next"
  or "gate failed: approach 2 next".
- [ ] Step 3: commit `docs: skirmish player research findings`, push
  `feat/native-sides`.
