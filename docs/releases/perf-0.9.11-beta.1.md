# bfme2perf 0.9.11-beta.1

Local opt-in beta, based on 0.9.10. Not installed or published. The faction
unlocker and its multiplayer protocol are unchanged.

## Changes

- Recover from a stale extra-frame cost floor. After 300 consecutive
  eligible limiter frames, try one draw budgeted at twice the measured
  regular draw cost. Keep all existing deadline margins and safety checks.
  A successful draw resets stale estimates; three failed recovery probes
  trigger the existing escalating rest policy. Repeated failures eventually
  disable extras for the session rather than keep delaying game frames.
- Replace permanent exhaustion of the 256-entry vertex-declaration cache
  with bounded least-recently-used replacement. Release owned references
  only after the previous group is finished; unsupported declarations still
  fall back to the game's drawing.
- Add diagnostics for blocked extra frames and cache churn.

Recovery does not guarantee 60/120 FPS. Twice the regular draw cost plus
the margin must fit before the next game frame. If it does not, recovery
waits. Catch-up, pauses, minimized windows, screenshots, insufficient
budget and frames without limiter waits interrupt eligibility. Camera,
simulation, networking, input and frame limits retain their prior behavior.

## Diagnostics

The timing CSV appends eight columns, preserving all earlier columns:

- `fps_block_cost`, `fps_block_catchup`, `fps_block_pause`,
  `fps_block_stop`: observed gating reasons, counted once per limiter frame.
  Reasons may overlap; these are not mutually exclusive buckets and an
  earlier gate can prevent observing a later one. Frames without a limiter
  wait are not included in these counters.
- `fps_probe_attempts`, `fps_probe_success`, `fps_probe_late`: recovery
  draws attempted, completed on time, or late. A safety failure can leave
  an attempt without either outcome.
- `fps_learned_ms`: the current learned cost floor.

The log's declaration-cache line records cumulative hits, misses, evictions
and creation failures across regular and extra draws. Many evictions mean
the working set exceeds the cache; compare render time as well as coverage.

## Installation and rollback for an authorized test

Keep your existing INI. The package's INI is a template for new installs;
its default `MaxFPS=30` leaves extra rendering off, just as before. This
beta adds no configuration keys. Existing users with higher MaxFPS keep it.

Back up the installed ASI, INI and log/CSVs before replacement. Install only
one `bfme2perf.asi` in `mss`; never leave another renamed beta/stable ASI
there because Miles loads both. Restore the saved ASI to roll back.

Public distribution must first verify the launcher's opt-in channel and
rollback behavior. Keep this beta out of the established stable channel.

## Gameplay validation still required

Compare 0.9.10 and this beta in similar single-player and multiplayer
scenes: early game, large armies, heavy battles and a quieter scene after
a hitch. Save logs immediately after each session. Check extra frames,
lateness, game speed, cache churn and render costs. Check shadows, camera
scroll/stop, loading transitions, replay behavior and a sustained match
with beta and non-beta performance-plugin clients. All players still need
compatible faction-unlocker versions and mod data.

Native and offline tests do not establish in-game FPS gains or multiplayer
validation. No game or WorldBuilder session is launched by the agent.

## Automated validation (2026-10-17)

- Full `perf/` `mingw32-make test`: passed, including site verification on
  both local executable images, native hooks, load/version, scheduler,
  cache, rendering/state restoration, catch-up and lighting tests.
- `mingw32-make inst-gpu`: all 59 variants passed on native D3D9; all 59
  passed again on DXVK. No GPU test group was skipped.
- Windows version resource: 0.9.11-beta.1, prerelease flag set.
- `git diff --check`: clean.

Local test logs are in `perf/build/beta-test.log` and
`perf/build/beta-gpu.log` (ignored build outputs).
