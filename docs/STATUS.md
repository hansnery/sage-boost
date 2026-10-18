# Status and handoff (2026-10-17)

Where the three repositories and the installed game stand, for a model or
person picking the work up. Rules, build steps and branches are in
[AGENTS.md](../AGENTS.md); the Realms side (factions, AI, files outside the
repo, backlog) is in the Realms repo's `data\AGENTS.md`; what the docs can't
settle is in [BACKLOG.md](BACKLOG.md). Re-check anything below against the
files before relying on it.

## Next target: 60 visual FPS with shadows at 60 (2026-10-17)

- User requested committing/pushing current work, then a target of at least
  60 FPS. Clean beta history through `ace76a5` pushed to origin/codex/perf-beta.
- Explicit preference: keep shadows at 60 too; no shadow-cadence shortcut.
- Approved next-stage design:
  [60 FPS with full-rate shadows](specs/2026-10-17-60fps-full-shadows-design.md).
- Beta.2 adds extra-frame timings and the full-pass setup audit is complete.
  The saved-game capture is complete (below). The combined budget needs
  roughly 8 ms of savings in
  the captured heavy scene. This is a target, not an achieved result.
- Latest user constraint: request another gameplay test only after evidence
  of a significant improvement; no diagnostic-only install/test cycle.

## Dispatch experiment completed (2026-10-17)

- Added an offline benchmark and test-only bulk prototype, outside the ASI.
  [Results](specs/2026-10-17-dispatch-benchmark-results.md) and all paired
  samples are preserved. No production source or settings changed.
- Final repeat runs: 1.4-1.7 ms median submission savings with Detail=1,
  across native D3D9/DXVK and lit/shadow workloads at ~6,900 instanced meshes.
  The optimistic path omits member markers; even this falls short of the
  preselected ~3 ms screening threshold. Deprioritized as a standalone beta.
- Pixel/depth comparisons, reference/count/state checks and the full plugin
  suite pass. Installed beta.2 ASI and INI hashes remain unchanged. No new
  installation or gameplay test requested; 60 FPS is still unachieved.
- Development benchmark/docs committed and pushed on `codex/perf-beta`;
  no tags, stable promotion or public release.

## Performance beta 0.9.11-beta.2 (2026-10-17)

- Implements the approved diagnostic stage: separately staged extra shadow,
  reflection, views and UI timings, with safety-failure invalidation; completed
  flip-enabled End_Scene call counts and bounded gap percentiles/histogram.
- Full plugin tests and all 59 shader comparisons on both native D3D9
  and DXVK pass; final buffer-size regression checks pass. Build commit
  `cad786f` pushed to origin/codex/perf-beta and installed on 2026-10-17.
  No tag or public release.
- No rendering/cadence/quality changes. Full-rate shadows remain the target.
  60 FPS has not been achieved or claimed. Installed beta.2 ASI SHA-256:
  `05d9b381feb2b7ddc0001466ea8c23333e33ce06679786ca2bd1b4c87de94799`.
- Prior beta.1 ASI, INI, log and both CSVs backed up to
  `%TEMP%/bfme2perf-0.9.11-beta.1-installed-20261017-173715`.
  Installed INI hash verified unchanged; no game launched.
- Standing user authorization (2026-10-17): when committing this ongoing
  development work, push the development branch and install tested plugin
  builds without asking again. Continue backups and INI preservation;
  this does not request tags, stable promotion or public release.
- [Release notes and test procedure](releases/perf-0.9.11-beta.2.md),
  [full-pass audit](specs/2026-10-17-full-pass-audit.md).
- Repeated per-mesh dispatch is now benchmarked above; the standalone
  saving is insufficient. Broad merging has few candidates in this save.

## Updated source investigation (2026-10-17)

- User requested pulling Open-BFME-2; fast-forwarded its master by 6,410
  commits to `ff1544d9fef`, preserving pre-existing untracked work and leaving
  the broken BFME1 submodule and Grok checkout alone. No authored edits there.
- [Updated reference audit](specs/2026-10-17-updated-reference-performance-audit.md):
  FX PerformRendering is now recorded matched (819 bytes), enabling stronger
  investigation of group-local direct staging; stale partial comment noted.
- Native isolated event-constructor/destructor test on AotR and game820 found
  only ~0.24-0.26 ms per 7,000 pairs. Copies alone are not a strong candidate.
  Full group dispatch and conditional ambient-lightmap caching remain unmeasured.
- Installed beta.2 is unchanged. The native dispatch benchmark above did not
  justify proceeding to a runtime hook and its target-ABI proof.

## Beta.2 saved-game test (2026-10-17)

- User completed the next test. Copied INI, log and both CSVs before reading:
  `%TEMP%/bfme2perf-beta2-test-20261017-174157`.
- Confirmed runtime beta.2, MaxFPS=120, FrameSmoothing=3,
  ExtraFrameShadows=1, ShadowInterval=1 and Detail=1. No settings changed.
- Steady interval seconds 50-100 inclusive (51 rows): 1,546 game frames,
  1,539 completed flip-enabled EndScene calls (~30.18 calls/s), zero extras,
  zero invalid extra samples, zero extra lateness and no safety stop.
  Seven renders were skipped by catch-up. Completion-gap histogram p50
  upper bound 33.334 ms, p95 upper bound 50 ms (not scanout measurements).
- Frame-weighted regular CPU wall times in that interval: frame 23.039 ms,
  logic 3.574 ms, render 15.658 ms, shadows 7.482 ms, reflections 2.711 ms,
  views 5.887 ms, UI 0.543 ms. Nested scopes overlap.
- Expected extra cost stayed exactly 14.641 ms, inherited from an extra
  draw at second 28. Available budget averaged 10.763 ms; 1,425 limiter
  frames reported cost blocking. Learned floor was zero and no recovery
  probes ran. The regular-draw reference averaged 18.303 ms, so the
  existing reference-based decay could not lower the stale cost estimate.
  Beta.1's recovery is specifically gated on a nonzero learned floor and
  would not address this case; twice the reference would not fit either.
- The 40 valid extras with anim_on_screen>20 all occurred at seconds 27-29,
  with only 102-138 animated objects; subsequent rows reached hundreds.
  Their mean total was 3.835 ms, shadow marker 1.024 ms, views 1.278 ms,
  UI 0.666 ms. These are transition measurements, NOT the missing busy-scene
  extra cost. No reflection samples, consistent with extra reflections off.
- One 35.844 ms extra draw caused 22 ms lateness at second 113, after
  anim_on_screen returned to zero; existing one-second pause engaged.
  Do not attribute this menu/transition row to the sustained battle.
- Result: the measurement plumbing works, but 60 FPS remains unachieved.
  Do not infer that the busy extra draw costs 14.641 ms or that it would
  fit from the early 3.835 ms mean. Next work must address the unobserved
  busy-scene cost with a defensible sampling budget and/or reduce verified
  per-group dispatch overhead; retain multiplayer deadline safety.

## Performance beta 0.9.11-beta.1 (2026-10-17)

- User approved focused performance beta and larger render investigation.
  Baseline `bb072a9` was verified clean and pushed on `main` and
  `feat/perf-dll` before development. New work is on `codex/perf-beta`.
- Implements bounded recovery from a stale learned extra-frame cost floor,
  a 256-entry LRU declaration cache, and appended timing diagnostics.
- Full `perf/` test suite passed. All 59 existing instanced shader variants
  passed offline comparisons on native D3D9 and DXVK. The new mandatory CPU
  cache test covers 600 identities, hot reuse, negative caching and COM
  reference balance. Recovery tests cover a transient, repeated expensive
  probes, interrupted eligibility and millisecond-clock wraparound.
- Installed 0.9.11-beta.1 at the user's request on 2026-10-17. Only
  `rotwk/mss/bfme2perf.asi` replaced; SHA-256 matches the tested build
  `4dc932daa1bc9924a49d47b95ea6025620332cfb674476bbcf624abfb76928f1`.
  Installed INI hash unchanged. Previous 0.9.10 binary, INI, log and both
  CSVs backed up to
  `%TEMP%/bfme2perf-0.9.10-installed-20261017-171107`.
  The user performed the saved-game test recorded below. Beta commits
  through `ace76a5` pushed on 2026-10-17; no publication or tag.
  Stable branches remain at the baseline.
- [Release notes](releases/perf-0.9.11-beta.1.md),
  [approved design](specs/2026-10-17-perf-beta-design.md),
  [next render audit](specs/2026-10-17-render-audit.md).
- Both missing shader hashes identify normalmapped shadow-pass variants;
  a new independently verified shadow family is the next candidate.
  Skeleton threads are deferred on measured cost and current FP rules.
- User reports current sage-boost works online (2026-10-17), but mostly at
  30 FPS. The captured log's match mode is not confirmed. Beta gameplay,
  mixed performance-plugin versions and replay validation remain pending.

### First beta gameplay report (2026-10-17)

- User tested a troop-heavy saved game: felt smoother, reported no FPS
  above 30. Match/network mode and the FPS counter are not confirmed.
- Copied log, CSVs and INI before inspection to
  `%TEMP%/bfme2perf-beta1-test-20261017-171637`. Log confirms beta.1 and
  existing MaxFPS=120, ExtraFrameShadows=1, Instancing=1 settings.
- 91 CSV rows with anim_on_screen >20 and frames >=15: 2,739 game frames,
  23 extras; no safety-stop blocks, learned floor 0 and no recovery probes.
  Frame-weighted row averages: render 15.449 ms, shadows 7.021 ms,
  skeleton-in-render 1.540 ms, limiter budget 11.374 ms. These scopes
  overlap and are not additive.
- During seconds 55-114 extra cost remained about 12.46 ms, while the
  average available budget by 20-row group was about 7-12 ms. With margin
  and slack it needs roughly 17.5 ms. This trace shows insufficient room,
  not the stale learned-floor failure. Extras appear in lighter portions.
- Latest instancing summary: 30 declarations, 0 evictions/failures,
  0 mismatches, 54 verify samples and 0 bad. This scenario did not exercise
  cache exhaustion. No controlled baseline capture proves the reported
  smoothness improvement's cause.
- Prioritize reducing shadow/extra-draw cost for this save. Skeleton work
  is higher than in the earlier trace but is still only about 1.54 ms;
  missing-shader counts are small (19,470 versus 8,372,353 instanced meshes
  in the summary), so extending shader coverage alone may have little
  effect here. Do not loosen game-frame deadlines to force extras.

## Current work: unlocker 0.5.0 LAN faction fix (2026-10-17)

- User reported 17 exact faction-index wraps and an early multiplayer desync.
  The LAN nibble codec explains every mapping; the subsequent crash has not
  been reproduced independently.
- 0.5.0 replaces ten human/AI colour/faction/start call sites as one group.
  Packet length is unchanged, including eight-player games. All players need
  the updated protocol. Performance plugin unchanged.
- Source and launcher packaging instructions: [release notes](releases/unlocker-0.5.0.md).
- Full `dll/` test suite passed on 2026-10-17: site verification, engine,
  original hooks, proxy and network tests (110,592 native round trips across
  the three images plus all 65,536 field values).
- Source commit `882b6ab` pushed to `main` and `feat/perf-dll`.
- Published through Realms Launcher Beta as `0.5.0-perf.0.9.10`, release
  `3c1c2b37aedb6612cd4e`, on 2026-10-17. Only `dinput8.dll` changed; the
  previous published ASI and both INI templates are byte-identical. Signed
  player download ranges for all four files were verified against SHA-256.
- Local game installation remains unchanged; no Git tag created.
- Required user test before public rollout: two PCs, explicit high-index
  factions and Random/AI, correct colours/starts, then a sustained match.
- The installed-build details below describe the previous deployment.

## Unpushed commits

Pushing is the user's call. List them again with `git log --oneline @{u}..`
in each repo.

| repo | branch | local commits not on origin |
|---|---|---|
| sage-boost (`C:\repositories\sage-boost`) | `feat/perf-dll`, `main` | none (all pushed 2026-10-15; `main` = `feat/perf-dll`) |
| Realms in Exile (`...\realms_nightly`, [hansnery/realms-nightly](https://github.com/hansnery/realms-nightly); was `...\realms_nightly\data`, hansnery/data, until 2026-10-15) | `master` | check with `git log --oneline @{u}..`; the move and the Dorwinion fix were pushed 2026-10-15 |
| realms-worldbuilder (`C:\Jogos\AgeoftheRing\realms-worldbuilder`) | `main` | none (all pushed 2026-10-15) |

The six files that can show as modified (`docs/plans/...`, `perf/Makefile`,
`perf/src/bfme2perf.def`, `tools/port_sites.py`, `tools/test_port_sites.py`)
differ only by CRLF line endings; `git diff --ignore-cr-at-eol` is empty.
Don't commit them.

## Installed in the game

Game folder: `C:\Jogos\AgeoftheRing\rotwk` (Age of the Ring's ROTWK 2.01
`game.dat`, Realms in Exile loaded on top).

### Faction unlocker: `dinput8.dll` + `dinput8.ini`

- Build from `feat/perf-dll` at `2259a46` (installed 2026-10-15 02:09). It
  still reports version 0.4.0; the change is the `[Sides]` cap of 32 (was
  16). Previous DLL and INI: `%TEMP%\dinput8-backup-20261015-020959`.
- Last start (02:15): `the verified baseline build`, all 17 aliases
  accepted, `summary: 8 applied, 0 refused, 2 off` (B and C2 off).
- `[Sides]` as installed. The aliases and their comment block were added
  on 2026-10-14 17:21 (backup before: `%TEMP%\dinput8.ini.before-sides-20261014-172145.bak`),
  `DolAmroth` later that day, `Lorinand` and the "at most 32 entries"
  comment on 2026-10-15 02:10. Everything else is the shipped INI.

```ini
Arthedain=Men
Numenor=Men
Ithilien=Men
Hornburg=Rohan
EredLuin=Dwarves
Lindon=Arnor
Shire=Arnor
Dorwinion=Mirkwood
Morgul=Mordor
Annatar=Mordor
WhiteHand=Isengard
Gundabad=Wild
Moria=Wild
Rhudaur=Guldur
CarnDum=Guldur
DolAmroth=Men
Lorinand=Elves
```

  `Rhun` is one of Age of the Ring's own Sides and can't be aliased.

### Performance plugin: `mss\bfme2perf.asi` + `mss\bfme2perf.ini`

- Current: 0.9.11-beta.2 from `cad786f`, installed 2026-10-17; backup and
  verification above. Previously 0.9.10, built from `05420da` (pushed),
  installed 2026-10-14 18:51. The
  previous build and settings are in `%TEMP%\bfme2perf-0.9.9-installed-1852`.
- `bfme2perf.ini` (LF line endings) differs from the shipped
  `perf/bfme2perf.ini`. Keep the user's values; when a build adds a key,
  add it with its comment and leave the rest alone.
  - `[Timing] Enabled=1 Detail=1` (per-mesh timing on, for measurements).
  - `[Render] MaxFPS=120` (extra frames; 30 in the 0.9.7 backup, 120 again
    since 0.9.9),
    `FrameSmoothing=3` (units drawn between the last two game frames; the
    camera is never moved since 0.9.10), `ExtraFrameShadows=1`,
    `Instancing=1`, `InstancingVerify=1`, `ShadowPassVerify=1`,
    `ReflectionInterval=2`, `ShadowInterval=1`, `LightCull=1`,
    `LightReuse=1`, `LightVerify=1`, `LightSkipShadow=0`.
  - `[CatchUp] Enabled=1 MaxSkips=1 Notice=1 NoticeText=Recuperando atraso
    NoticeMinSkips=6`. `NoticeMinSkips=6` came with 0.9.9 (the notice
    showed for single skipped frames). The user had raised `MaxSkips` to
    3 (in the 0.9.7 and 0.9.9 backups) and set it back to 1 on 2026-10-14
    19:19; confirmed 2026-10-15: keep `MaxSkips=1`.
- Logs next to it: `bfme2perf.log`, `bfme2perf_timing.csv`,
  `bfme2perf_scopes.csv` (overwritten on every start; copy them to `%TEMP%`
  before a new build if they matter).

## Waiting for an in-game test

Plugin 0.9.10:

- Scroll and stop: the camera stops dead (no drift).
- Fast right-mouse scrolling: no black strip at the screen edge.
- Moving units still look smooth at `FrameSmoothing=3`.
- The catch-up notice shows only when catching up is sustained (6 skipped
  frames within 2 s), and the battle keeps its speed.
- Multiplayer: a network game with every player on 0.9.10 worked fine
  (played 2026-10-15). Still open: a game where only some PCs have the
  plugin, and a replay.

Realms in Exile (details in its `AGENTS.md` change log, all "not yet
confirmed in game"):

- Dol Amroth as a lobby faction (fortress, porter, spellbook incl. the
  Rebuild fix, AI base), and its farms on the minimap.
- Lothlórien / Lórinand split (`Lorinand=Elves` alias now loads).
- Nazgûl Realms split (Dol Guldur, Angmar, Carn Dûm) and Misty Mountains
  split (Misty Mountains, Gundabad, Moria).
- Inflation filter: income of the new factions' farms drops from the 6th
  building on (the filter additions of 2026-10-14).
- Carn Dûm expansion pads draw `UBFoundation`/`UBFoundationP` like the
  Angmar subfaction's, and the plot model disappears once something is
  built on a pad. See the castle problem below first.

## Waiting for the user (WorldBuilder and decisions)

- **`fortress_carndum.bse` (castle file, check first).** The scripted patch
  (2026-10-14 ~17:37) wrote 3 side + 4 corner pads with the "In Base"
  property and 8 `CastleTemplates` entries. The file on disk now is a later
  save (17:39, 8,807 bytes, not recorded by any agent; most likely the user
  in WorldBuilder): the citadel at (1000, 1000), 7 pads all
  `CarnDumFortressExpansionPadSide` (also at the 4 corner positions), none
  with `objectBaseName`, and `CastleTemplates` lists only the citadel. Every
  other castle has its pads in `CastleTemplates`, so the pads probably
  don't unpack in game. The user chose (2026-10-15) to fix it themselves
  in WorldBuilder: 3 `CarnDumFortressExpansionPadSide` at (890, 1000),
  (1000, 890), (1000, 1110); 4 `CarnDumFortressExpansionPadCorner` at
  (1077, 1077), (923, 1077), (923, 923), (1077, 923); "In Base" on all 7
  and the citadel. Don't overwrite it with the script. After their save,
  check with `tools/bse_list.py` and `tools/bse_fielddiff.py`.
- **AI bases** in `realms_nightly\bases` (the user edits them; don't
  regenerate or overwrite them):
  - Ered Luin: still a byte copy of Realms' old `ai base - dwarves - at and
    infantry`; to be edited.
  - Lórinand: still an unedited copy of `ai base - elves - lw rush`; to be
    edited.
  - Ithilien: edited 2026-10-14 16:26 (the Gondor buildings and farms of
    Realms' old `motw - standard base` are gone; a `GondorStatue` and a
    `RivendellDunedainGathering` remain, `MenFortress` centre). Ask whether
    it is finished.
  - Carn Dûm: done (`CarnDumFortress` centre, 17:04).
  - Optional: a Gundabad base (it uses the Wild goblin spam layouts now).
  - Optional "Shire hobbit farm": the Shire layout already places 2
    `ShireHobbitFarm` and no `ShireHobbitHole`, while the Shire change-log
    entry says the opposite. Ask what is still wanted.
- Optionally open `fortress_carndum.bse` once in WorldBuilder after the fix
  to confirm it loads.

## Backlog

Realms in Exile: see *Backlog* in its `data\AGENTS.md` (capture flags, civilian
castles, redundant alternate maps, buildings outside the inflation filter,
Carn Dûm pad portrait, off-radar buildings, unconfirmed change-log entries).

sage-boost:

- The README's *Not done yet* still says nothing was verified in game; the
  unlocker 0.4.0 (S2/S4, `[Sides]`) and the plugin have been run on Age of
  the Ring with Realms since.
- The history is one commit (2026-10-15); the per-feature branches and
  worktrees are gone (see *Branches* in `AGENTS.md`).
- What the docs can't settle (in-game tests, the user's WorldBuilder work,
  starting a new model): [BACKLOG.md](BACKLOG.md).
- Vanilla BFME2 1.06 for the plugin; the unlocker's manual test checklist.
