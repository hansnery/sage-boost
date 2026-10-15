# HANDOFF — brief for the implementing model

> **Status: historical, completed.** This is the original brief the faction
> unlocker (`dll/`) was built from; it is done, and the repository has grown
> since (the performance plugin in `perf/`). For current rules, build steps
> and branches see [AGENTS.md](AGENTS.md); for what is installed and pending
> see [docs/STATUS.md](docs/STATUS.md). Where this brief and the code differ,
> see *Deviations from `HANDOFF.md`* in the README.

You are being asked to **create a runtime faction-unlocker DLL** for
*The Lord of the Rings: The Battle for Middle-earth II* (vanilla 1.06,
`game.dat`). This document is self-contained: it gives you the mission, the
environment, verified technical facts, the decisions already made, a phased
plan, and the rules. Two research documents in this folder carry the full
detail — read them first, then verify everything yourself from the retail
image before relying on it.

---

## 1. Mission

Build a DLL in the style of these two reference projects (study their
*approach*; do **not** copy their code — see §8):

- https://github.com/admiraly/bfme2-accelerator — injected 32-bit DLL,
  wildcarded byte-signature hooks, fail-inactive if a signature is missing,
  feature env-var toggles.
- https://github.com/TheeHorse/SAGE-Unlocked — `dinput8.dll` proxy DLL copied
  into the game folder, patches found by behavior/signature rather than
  hardcoded addresses, INI-configurable.

The DLL patches the game's memory at startup so that factions added via
`Playertemplate.ini` (mods) actually work: they appear in the lobby faction
dropdown, selecting them keeps the chosen faction, and the AI can pick them.
Nothing on disk is modified; checksummed launchers keep working.

**Why this can't be upstream code:** the upstream project
(`C:\repositories\Open-BFME-2`) reverse-engineers retail byte-identically and
refuses any behavioral divergence in its sources. Every patch below changes
retail behavior, so it can only live in this runtime DLL.

## 2. Read first

1. `faction-limit-findings.md` — how factions work in the reversed code:
   `PlayerTemplateStore` layout, the three gates (playable-set dedup +
   selection fallback, map AllowedFactions filters, fixed 7-name table), and
   what is *not* a limit.
2. `side-string-lifecycle.md` — how a `Side` string is created and flows (INI
   → `PlayerTemplate+0x18` → player / AI base / hero masks / stats), the
   recovered 62-row field table, the retail disassembly of every patch site,
   the byte-level patch map (§3 there), and the two implementation strategies
   (§4 there). **The patch map there is your primary input.**

## 3. Environment and assets

| thing | value |
|---|---|
| OS / shell | Windows, Git Bash |
| Python | `python` (not `python3`), **capstone 5.0.7 installed** — use it for all disassembly |
| Retail image (verify against this) | `C:\repositories\Open-BFME-2\baselines\bfme2\workshop-vanilla-1.06\files\game.dat` — 32-bit PE, preferred image base `0x400000`, vanilla 1.06 |
| WorldBuilder (separate exe, out of scope) | same folder, `worldbuilder.exe` |
| Reversed source (read-only reference) | `C:\repositories\Open-BFME-2\Code\` — active upstream repo with verification hooks; **never modify anything in it**; scratch scripts live in your own work folder |
| All new work goes in | `C:\repositories\sage-boost\` (this folder) |

PE mapping note: in `game.dat` the addresses below are RVAs; runtime address =
`imagebase + RVA`. The disassembly snippets in `side-string-lifecycle.md` §2
show every site with enough surrounding context to build signatures.

## 4. Established facts (already verified — re-verify before use)

Factions are `PlayerTemplate` INI blocks parsed into a vector at
`ThePlayerTemplateStore` (global pointer at `0xDFE0D0`); the store also holds
`playableIndices`, a `set<int>` at store+0x18, of playable template indexes.
The count is fully dynamic — no count constant exists. Three gates block
modded factions, and each has a verified patch site:

| # | RVA | original bytes | patched | effect |
|---|---|---|---|---|
| A | 0x00400E65 | `75 06` | `EB 06` | `GameSlot::setPlayerTemplate` 0x00400E33: a faction index outside the playable set is kept instead of being silently replaced by the first playable one. Fixes "selecting the new faction lands on another one". |
| C | 0x001FF1F9 | `0F 84 42 FF FF FF` | `90` ×6 | inside `parsePlayerTemplateDefinition` 0x001FEFEC: the Side-dedup that drops a playable template whose Side string equals an existing playable one. NOPing the `je` lets duplicates enter the set. |
| D | 0x00441E62 | `75 04` | `EB 04` | `Rva00441E4DFaction` 0x00441E4D: lobby dropdown stops filtering by the map start position's AllowedFactions (always takes the empty-set "allow" path). |
| E | 0x00244632 | `0F 84 8B 00 00 00` | `E9 8C 00 00 00 90` | `populateRandomSideAndColor` 0x002444BE: AI/Random faction pick stops narrowing the pool by map AllowedFactions. |

Optional sites (same doc, §3): B (0x00400E79, Random→first fallback), C2
(0x001FF151, Dwarves/Wild restriction mode), F (7-name table relocation for
hero masks/rank icons/stats of genuinely new Sides — the table at VA 0xDBE9B0
has a NULL terminator at slot [7] followed by unrelated data, so it must be
relocated, not grown), F2 (Goblins special-case removal).

## 5. Decisions already made — do not re-litigate

1. **Injection: `dinput8.dll` proxy** (SAGE-Unlocked style, proven for BFME 2).
   Proxy forwards to the system `dinput8` via `LoadLibrary`/`GetProcAddress`.
   Ship as a DLL mod: the user copies the proxy plus your payload DLL and an
   INI into the game folder.
2. **Signature-based site finding, fixed-address fallback denied.** Each patch
   site must be located by a wildcarded byte signature built from the
   disassembly context; if zero or >1 match → refuse to apply that patch and
   log (fail-inactive). Fixed RVAs may be used only as a cross-check in
   diagnostics, never as the install path.
3. **Verify-then-patch.** Before writing, confirm the original bytes at the
   site; on mismatch, skip and log. Never patch blind.
4. **Config via INI** next to the DLL (`<name>.ini`): master toggle + one
   toggle per patch (A, C, D, E, and later B/C2/F/F2). Defaults: A+C+D+E on,
   the rest off.
5. **MVP scope = patches A + C (+ D + E)** supporting *Strategy 1* (modded
   faction reuses an existing Side string, e.g. `Side = Men` — it then
   inherits map faction lists, AI bases, hero-mask bits and stats of that
   side, while its own block controls display name, money, spellbook,
   heroes). *Strategy 2* (genuinely new Side string) is phase 2 and needs F +
   skirmish-side work — see `side-string-lifecycle.md` §4.
6. **Singleplayer/skirmish only at first.** Patch C changes INI parsing and A
   changes slot setup: every client in a network game must run the DLL, and
   GameSpy-style online is out of scope initially. Say so in the README.
7. **License: MIT** for everything you write. No GPL/MIT mixing concerns.
8. **Language/toolchain: C, MinGW-w64 (32-bit)**, `-static` where sensible,
   no CRT surprises inside DllMain. (If you have a strong reason to deviate,
   document it in the README; do not switch to C# like SAGE-Unlocked's
   helpers.)

## 6. Phased plan

**Phase 0 — verification harness (script, not shipped).** Write a Python
script (capstone) that loads the baseline `game.dat`, disassembles all sites
in §4, and asserts the original bytes. Derive candidate wildcard signatures
(mask immediates/displacements, keep opcode skeletons). This doubles as the
regression check when signatures change.

**Phase 1 — proxy skeleton.** `dinput8.dll` exporting the dinput8 ordinals,
forwarding to the real one, loading the payload DLL (or doing the work
inline). Must be inert without the game and crash-free with `master=false`.

**Phase 2 — patch engine.** Section-scan for `.text`, signature find with
match-count enforcement, original-bytes verify, `VirtualProtect` +
memcpy + restore, all from DllMain (static code bytes only — no thread
spawning). Structured log file next to the DLL (timestamp, game version
check, per-patch: found/verified/applied/refused).

**Phase 3 — README + config + test instructions.** Manual test checklist for
a human: build a two-line test mod (clone a `PlayerTemplate` block with a new
`CommandSet`-visible name and `Side = Men`, set `PlayableSide = Yes`), launch
skirmish, confirm: the clone appears in the faction dropdown; selecting it
keeps it (slot panel + start-of-game); AI slot with Random resolves to a real
faction; no crash on map load. Include the exact INI snippet in the README.

**Phase 4 (only if asked to continue).** Strategy 2: table relocation (F),
Goblins normalize (F2), skirmish-side redirect for unknown sides (start at
`Player::rva002AC4D4` 0x002AC4D4 and its caller 0x002AFCDF), CAH mask data
location, WorldBuilder investigation.

## 7. Acceptance criteria

- `master=false` → DLL loads, forwards dinput8, patches nothing, game runs
  normally.
- Each patch: applied only when its signature matches exactly once *and* the
  original bytes verify; otherwise logged and skipped (game still runs).
- With A+C(+D+E) active and a cloned-side test mod: new faction selectable in
  the skirmish lobby and kept through game start; no crash on load; AI game
  works.
- Log identifies vanilla 1.06 vs anything else (read version resource or a
  known byte anchor) and refuses unknown versions.

## 8. Rules

- **Never modify anything under `C:\repositories\Open-BFME-2\`.** Read it,
  run read-only scripts against its baseline, but write nothing there — it is
  an upstream project with commit hooks byte-verifying every change.
- **No code from the reference projects.** SAGE-Unlocked is GPL-3,
  bfme2-accelerator is MIT; copying risks license contamination and isn't
  needed — the patch map plus the approach description are sufficient to
  implement from scratch.
- **Never redistribute `game.dat`** or any EA asset; the DLL ships alone.
- No hardcoded absolute addresses in the install path (see decision 2);
  diagnostics may print RVA equivalents.
- If a documented byte or offset disagrees with what you find in the image,
  **trust the image**, record the discrepancy in a `DISCREPANCIES.md` in this
  folder, and re-derive the patch from the disassembly — do not force it.
- Keep the work inside `C:\repositories\sage-boost\` (suggest a `dll/`
  subfolder for code, `tools/` for scripts, and leave the two research docs
  untouched except to append corrections).

## 9. Status and corrections (appended 2026-10-12)

Phases 0–3 are implemented: `tools/verify_sites.py`, `dll/` (proxy, engine,
tests) and the README. Phase 4 (Strategy 2) is implemented as donor aliases
(`[Sides] NewSide=Donor`, hooks S1–S3) instead of patch F/F2; see
[DISCREPANCIES.md](DISCREPANCIES.md) section 6. ROTWK 2.01 is supported too,
with its own signature table, and verified against Age of the Ring's patched
executable (section 7). Correction to the §4 table:
**patch D** as written (`0x00441E62 75 04 → EB 04`) is inverted, and it sits in a
helper shared with Living World. The implemented D is the lobby call site
0x004426DA `74 4F` → `90 90`. See [DISCREPANCIES.md](DISCREPANCIES.md).
