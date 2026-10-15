# Status and handoff (2026-10-15)

Where the three repositories and the installed game stand, for a model or
person picking the work up. Rules, build steps and branches are in
[AGENTS.md](../AGENTS.md); the Realms side (factions, AI, files outside the
repo, backlog) is in the Realms repo's `data\AGENTS.md`; what the docs can't
settle is in [BACKLOG.md](BACKLOG.md). Re-check anything below against the
files before relying on it.

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

- 0.9.10, built from `05420da` (pushed), installed 2026-10-14 18:51. The
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
