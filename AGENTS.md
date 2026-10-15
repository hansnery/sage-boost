# sage-boost: notes for agents

Two runtime add-ons for BFME2 1.06 and ROTWK 2.01, developed on Age of the
Ring (AotR) with Realms in Exile (RiE): the faction unlocker (`dll/`, ships as
`dinput8.dll`) and the performance plugin bfme2perf (`perf/`, ships as
`mss\bfme2perf.asi`). [README.md](README.md) explains both;
[docs/STATUS.md](docs/STATUS.md) says what is installed, unpushed and waiting
for a test or for the user; [docs/BACKLOG.md](docs/BACKLOG.md) lists what the
docs can't settle (untested in game, the user's WorldBuilder work). Read all
three before changing anything.

Related repos:

| repo | remote | local path |
|---|---|---|
| sage-boost (this one) | [hansnery/sage-boost](https://github.com/hansnery/sage-boost) | `C:\repositories\sage-boost` |
| Realms in Exile | [hansnery/realms-nightly](https://github.com/hansnery/realms-nightly) (private, Git LFS), `master` | `C:\Program Files (x86)\Realms in Exile Nightly\realms_nightly` (its agent notes are `data\AGENTS.md`). Since 2026-10-15 it replaces [hansnery/data](https://github.com/hansnery/data) and [hansnery/art](https://github.com/hansnery/art), which are only archives now |
| Realms WorldBuilder | [hansnery/realms-worldbuilder](https://github.com/hansnery/realms-worldbuilder), `main` | `C:\Jogos\AgeoftheRing\realms-worldbuilder` |
| Open-BFME-2 (reference, read only) | [Open-BFME/Open-BFME-2](https://github.com/Open-BFME/Open-BFME-2) | `C:\repositories\Open-BFME-2` |

## Hard rules

- **Never modify `C:\repositories\Open-BFME-2\`.** It is the read-only
  reference (decompilation and baseline images). Read-only scripts against
  its baselines are fine; `dll/Makefile`'s `GAME_DAT` reads the 1.06 one.
- **No code from SAGE-Unlocked (GPL-3) or bfme2-accelerator (MIT).**
  Studying their READMEs and approach is fine; everything here is written
  from scratch.
- **Never redistribute `game.dat` or any EA asset** (shaders, `.big`
  contents, models, maps). Only the DLLs and their INIs ship; tools read the
  user's install at run time and store at most hashes.
- **No hardcoded absolute addresses in the install path.** Every site is
  found by a wildcarded byte signature in the running executable and
  verified; if it isn't found exactly once with the expected bytes, the
  feature stays off and the log says why.
- **`dll/` (the faction unlocker) is a separate project.** Don't change it
  unless the user asks. They did once (2026-10-15: `[Sides]` cap 16 to 32,
  `2259a46`).
- **Never touch the x87 control word or MXCSR.** Hook stubs use general
  registers only; floating point lives in `perf/src/fpsmath.c` (SSE scalar,
  no vectorising).
- **Multiplayer safety: nothing that logic, input, the network or the CRC
  sees may change.** No retiming of game frames, no writes to the frame
  counters or the frame limit, no logic random draws. The plugin is
  visual-only: extra frames, catch-up, instancing and the shadow-pass cuts
  touch only the render.
- **The plugin must never move the camera.** 0.9.10 lesson: moving the
  camera's render object directly bypasses `W3DView::setCameraTransform`,
  which also refits the shadow map's light camera and recentres the
  terrain's loaded area, so the moved view showed a black strip; and every
  camera smoothing re-timed the game's own camera motion (drift after
  scrolling stopped). Every frame shows the camera where the game has it.
- Don't launch the game or WorldBuilder; the user tests in game.

## Git

- Conventional commits (`feat`, `fix`, `docs`, `chore`, `refactor`,
  `test`, with a scope such as `(perf)` or `(unlocker)`), body wrapped at
  ~72 columns, and the trailer the existing commits carry:
  `Co-authored-by: Cursor <cursoragent@cursor.com>`.
- Stage explicit paths only; never `git add -A` or `git add .`.
- Commit locally; push only when the user asks.
- `.gitattributes` forces LF (`* text=auto eol=lf`). Files that show as
  modified with CRLF-only differences (`git diff --ignore-cr-at-eol` is
  empty) are noise, not changes; leave them out of commits.
- Plugin builds carry their version in the subject, e.g.
  `fix(perf): ... (0.9.10)`; bump `PERF_VERSION` in `perf/src/main.c` and
  the two versions in `perf/src/version.rc` with it.

## Branches and worktrees

The history was squashed into a single commit on 2026-10-15. `feat/perf-dll`
(worktree `C:\repositories\sage-boost`) is the current branch and the one the
installed builds come from; `main` is the same commit and has nothing of its
own. Fast-forward `main` to `feat/perf-dll` when the user wants a release.
Both have an `origin` copy. The per-feature branches (native-sides,
perf-fps, perf-instancing, perf-skeleton, perf-shadow-inst, perf-shadowpass,
perf-lowshaders, perf-catchup, perf-lights) and the `perf-v*` tags were all
merged and were deleted, locally and on origin, with that squash.

Check again with `git branch -a -vv` and `git worktree list`. If you add a
worktree for a feature, remove it only when the user agrees
(`git worktree remove <path>`).
`C:\repositories\Open-BFME-2-grok` is a worktree of the read-only
Open-BFME-2 reference, not of this repo; leave it alone.

## Build, test, deploy

Toolchain: WinLibs GCC 16.2.0 i686 (`i686-ucrt-posix-dwarf`) at
`C:\tools\winlibs-i686\mingw32\bin` (gcc, windres, objdump,
`mingw32-make`), run from Git Bash. Python 3.13 with `pefile` and
`capstone` for `tools/verify_sites.py`. From PowerShell:

```powershell
& 'C:\Program Files\Git\bin\bash.exe' --norc --noprofile -c 'export PATH=/c/tools/winlibs-i686/mingw32/bin:/usr/bin:$PATH; cd /c/repositories/sage-boost/perf && mingw32-make test'
```

| folder | `mingw32-make` | `mingw32-make test` | `mingw32-make dist` |
|---|---|---|---|
| `dll/` | `build/dinput8.dll` | `tools/verify_sites.py`, engine, hook and proxy tests | `dist/`: DLL, INI, LICENSE |
| `perf/` | `build/bfme2perf.asi` | row, config, site, hook, load, fps, instancing, shadow-pass, catch-up tests | `dist/`: `.asi`, INI |

- Both Makefiles read `AOTR_DAT` (default `C:/Jogos/AgeoftheRing/rotwk/game.dat`)
  and `ROTWK_DAT` (`.../game820.dat`) when present; `dll/` also needs
  `GAME_DAT`, the 1.06 baseline in `C:\repositories\Open-BFME-2\baselines`.
  Tests only read these images.
- `perf/`: `make inst-gpu` and `make shadow-fx` are off-line GPU checks
  (need the game's `Shaders.big`). `tools/inst_shaders.py` regenerates
  `perf/src/inst_shaders.h`.
- Signature tables: change them in `tools/verify_sites.py` and regenerate
  with `python tools/verify_sites.py --write`, never by hand in C.
- Tool tests: `python tools/test_port_sites.py`,
  `python tools/test_mapinfo.py`, `python tools/test_refpack_encode.py`.
  The other scripts are listed in [tools/README.md](tools/README.md).

Deploy only after `make test` passes, and only when the user asks.

| file | goes to |
|---|---|
| `dll/build/dinput8.dll` | `C:\Jogos\AgeoftheRing\rotwk\dinput8.dll` (next to the user's `dinput8.ini`) |
| `perf/build/bfme2perf.asi` | `C:\Jogos\AgeoftheRing\rotwk\mss\bfme2perf.asi` (next to the user's `bfme2perf.ini`) |

1. Back up first, into a new folder in `%TEMP%` named after the installed
   version and time (e.g. `%TEMP%\bfme2perf-0.9.9-installed-1852`,
   `%TEMP%\dinput8-backup-20261015-020959`): the installed binary, its
   INI, and the current log/CSVs (the next start overwrites them).
2. If the game is running, the loaded file can't be overwritten but can be
   moved: `Move-Item` it into that backup folder as `<name>.loaded` (same
   volume), then copy the new build in. The running game keeps the old
   code; the next start loads the new one. Don't touch the INI while the
   game runs.
3. Compare hashes of the build and the installed copy.
4. INIs: never overwrite the user's. Add a new key with its comment, keep
   every existing value, encoding and line endings (both installed INIs
   are ASCII with LF line endings as of 2026-10-15).

Logs: `rotwk\dinput8.log` (patches, `sides:` aliases, `skirmish:` lines,
`exit:` hit counts), `rotwk\mss\bfme2perf.log` (version, hooks applied or
refused, switch-offs), `rotwk\mss\bfme2perf_timing.csv` (one row per
second) and `bfme2perf_scopes.csv`. The game writes them while it runs:
copy before reading.

## Where things are

- Design and measurements: `docs/specs/2026-10-13-perf-dll-design.md`
  (the plugin, milestone by milestone), `docs/specs/2026-10-13-perf-instancing-design.md`,
  `docs/specs/2026-10-12-native-sides-design.md`; plans in `docs/plans/`.
- Unlocker research: `faction-limit-findings.md`, `side-string-lifecycle.md`,
  `DISCREPANCIES.md`. `HANDOFF.md` is the original, completed brief.
