# Skirmish AI for new Sides: Implementation Plan (patches S2 + S4)

> **For agentic workers:** execute task by task; steps use checkbox (`- [ ]`)
> syntax. Findings this plan builds on: `DISCREPANCIES.md` section 8.

**Goal:** an AI player whose Side has no skirmish player on the map gets a
working skirmish AI, by resolving its Side to one the map has (own Side, then
the `[Sides]` donor, then the first game Side the map has).

**Architecture:** one resolver (`skirmish.c`) used by two hooks:

- **S2:** an entry hook on `findSkirmishSide`. It steals the 5-byte
  `mov eax, imm32` prologue and swaps the saved `ecx` for a stand-in whose
  +0x58 is the resolved Side.
- **S4:** a call hook on the inline compare in `Player::initFromDict`. It
  swaps the Side argument.

The resolver asks the engine "does this map have a player for Side X" by
calling the unhooked `findSkirmishSide` (S2's trampoline) with a stand-in
`this`. S2 and S4 form one group, installed together or not at all.

**Tech Stack:** C (freestanding, 32-bit MinGW, kernel32 only), Python 3 with
`pefile`/`capstone` for the site verifier.

## Global Constraints

- No hardcoded absolute addresses in the install path; sites are found by
  signature, refused unless found exactly once with the expected bytes.
- Verified on BFME2 1.06, ROTWK 2.01 (`game820.dat`) and Age of the Ring's
  `game.dat`; nothing is written outside this repository.
- No code from SAGE-Unlocked or bfme2-accelerator. No game files in the repo.
- Conventional commits; branch `feat/native-sides`.

## Sites

| Patch | Kind | 1.06 RVA | 2.01 RVA | Original | Notes |
|---|---|---|---|---|---|
| S2 | entry hook, steal 5 | 0x002AC4D4 | 0x002ACFA3 | `B8 imm32` | `findSkirmishSide`; reads `this` only as `this+0x58` |
| S4 | call hook, arg 1 | 0x002AFE61 | 0x002B096A | `E8 rel32` → compare | inline match in `initFromDict`; arg is `&player->side` |

Shared signatures (both games):

```
SIG_S2 = "| B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 EC 0C A1 ?? ?? ?? ?? 53 56 8B B0 C0 07 00 00 83 C1 58 57 51 8D 4D EC 89 75 E8 E8"
SIG_S4 = "8B 0D ?? ?? ?? ?? 50 E8 ?? ?? ?? ?? ?? ?? 74 ?? 8D 4E 58 51 8D 48 18 | E8 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ??"
```

## Resolution rule

`resolve(P)`, P = the player's Side:

1. the map has a player for P → P (unchanged, no hit counted);
2. P has a `[Sides]` donor D and the map has a player for D → D;
3. the first game Side N (Side table order, then `Goblins`; never `Neutral`,
   `Civilian`, `Observer`) the map has a player for → N;
4. otherwise P (vanilla behavior: the AI gets no skirmish slot).

Logging: S2 logs each resolved player once (S2 runs once per AI player per
game): `skirmish: Arthedain has no skirmish player on this map, using Men's`.
Step 4 is logged by S4 once per Side per session:
`skirmish: Arthedain has no skirmish player on this map and no other Side's to use`.

---

### Task 1: Site definitions and verifier

**Files:** Modify `tools/verify_sites.py` (S2 redefined, S4 added, hook kinds
`HOOK_SKIRMISH_THIS`, `HOOK_SKIRMISH_ARG`, `Patch.group`); regenerate
`dll/src/patch_table.c` and `dll/tests/baseline_expect.h` with `--write`.

- [ ] Step 1: change S2 to `KIND_ENTRY_HOOK`, `SIG_S2`, `steal=5`,
  `hook=HOOK_SKIRMISH_THIS`, `needs_sides=False`, `group=1`; add S4
  (`KIND_CALL_HOOK`, `SIG_S4`, `arg_slot=1`, `hook=HOOK_SKIRMISH_ARG`,
  `group=1`, `target` = the compare: 1.06 0x000069D6, 2.01 0x000065AA).
- [ ] Step 2: `python tools/verify_sites.py`; expect both sites found once
  in 1.06, 2.01 and Age of the Ring, the 5 stolen bytes position-independent
  with no branch into them; `OK: all checks passed`.
- [ ] Step 3: `python tools/verify_sites.py --write`; commit
  `feat: S2 entry hook and S4 site definitions`.

### Task 2: Patch groups in the engine

**Files:** Modify `dll/src/engine.h` (`unsigned group` in `fu_patch_def`,
status `FU_ST_GROUP_REFUSED`), `dll/src/engine.c` (after locating, a group
with any enabled member not located is not written at all), test in
`dll/tests/test_engine.c`.

- [ ] Step 1: test: on the 1.06 baseline copy, corrupt S4's site bytes; expect
  S4 `VERIFY_FAILED`, S2 `GROUP_REFUSED`, S2's bytes unchanged.
- [ ] Step 2: run, expect fail; implement; run, expect pass; commit
  `feat: install grouped patches all-or-nothing`.

### Task 3: Resolver

**Files:** Create `dll/src/skirmish.c`, `dll/src/skirmish.h`; modify
`dll/src/sides.c/.h` (native Sides as AsciiString objects:
`const void *fu_sides_native_string(unsigned i)`,
`int fu_sides_is_reserved(const char *name)`); add the sources to the Makefile.

Interface:

```c
typedef int (*fu_skirmish_query)(const void *side);    /* map has a player for side? */
void fu_skirmish_set_query(fu_skirmish_query q);
int fu_skirmish_engine_query(const void *side);       /* calls the S2 trampoline */
void fu_skirmish_set_s2(unsigned k);                  /* which def is S2 */
const void *fu_skirmish_resolve(const void *side, int *step); /* step: 1..4 */
const char *fu_ascii_cstr(const void *side);          /* "" for NULL/empty */
```

- [ ] Step 1: tests in `dll/tests/test_hooks.c` with a mock query (a set of
  Side names the "map" has): own present → own, step 1; own absent and donor
  present → donor, step 2; both absent → first native present skipping
  `Neutral`, step 3; nothing present → own, step 4; case-insensitive.
- [ ] Step 2: run, expect fail; implement; run, expect pass.

### Task 4: Dispatcher

**Files:** Modify `dll/src/engine.h` (`FU_HOOK_SKIRMISH_THIS`,
`FU_HOOK_SKIRMISH_ARG`), `dll/src/hooks.c`.

- `SKIRMISH_THIS`: `this` is the saved `ecx` (`kslot[-3]`: below the k slot
  are EFLAGS, then `pushal`'s EAX, ECX). Side object = `this + 0x58`; if
  resolved to R ≠ own, `kslot[-3] = (char *)R - 0x58`, count a hit, log.
- `SKIRMISH_ARG`: `entry_sp[arg_slot]` is the Side object pointer; swap with
  R if different, count a hit.

- [ ] Step 1: execution tests on copies of 1.06 code (the existing harness):
  S2: copy the signature region, apply, then overwrite from site+5 with
  `mov eax, [ecx+0x58]; ret 4`; call it as thiscall with a stand-in player
  whose Side the mock map lacks; expect the returned Side object to be the
  resolved one. S4: copy the signature region, thunk the callee; expect the
  callee's argument to be the resolved Side; an unresolved Side passes
  through.
- [ ] Step 2: run, expect fail; implement; run, expect pass; commit
  `feat: skirmish Side resolution for S2 and S4`.

### Task 5: Runtime log, wiring, docs

**Files:** Modify `dll/src/log.c/.h` (`fu_log_event_path`, `fu_log_event`:
open-append-write-close, dropped on failure), `dll/src/unlocker.c` (S2/S4
no longer masked by an empty `[Sides]`; set the engine query; event log
path), `dll/dinput8.ini`, `README.md`, `dll/src/unlocker.h` + `version.rc`
(0.4.0), `DISCREPANCIES.md` (section 8 status), spec status.

- [ ] Step 1: `test_engine` startup scenario: empty `[Sides]` → S2 and S4
  applied; `S4=0` → S2 `GROUP_REFUSED`/off and logged.
- [ ] Step 2: run `dll/build/run_tests.sh`; expect all suites OK, the DLL
  importing only KERNEL32.
- [ ] Step 3: README: S2/S4 rows, fallback rule, the corrected S2 claim,
  the in-game checklist for Realms' Arthedain. Commit, push.
