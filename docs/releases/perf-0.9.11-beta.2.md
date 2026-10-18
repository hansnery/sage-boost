# bfme2perf 0.9.11-beta.2

Local diagnostic beta for the approved 60 visual FPS investigation, with
shadows also at 60. It retains beta.1 rendering and scheduling decisions.
This build does not claim a performance gain or an achieved 60 FPS target.
No configuration keys or defaults changed. Installed locally at the user's request on 2026-10-17.

## Measurements

The timing CSV appends 27 columns; all earlier positions are preserved.

- `extra_profile_valid` / `extra_profile_invalid`: completed extra draws
  with valid / rejected profiling samples. Every existing gameplay/camera
  safety check must pass before timings are committed. Malformed tracked
  scopes also reject a sample; that rejects timing data, not the draw.
- `extra_profile_total_ms`: total elapsed CPU wall time of valid extra
  draws, including unit placement/restoration and final safety checks.
- `extra_{shadow,reflect,views,ui}_samples` and corresponding `_total_ms`:
  recognized pass marker counts and inclusive elapsed CPU wall time summed
  over valid extras in the row. Divide total by samples for mean pass cost.
  Same-pass nesting counts once; different passes may overlap, so do not
  add them as independent work. Missing samples have blank duration fields.
  A reused/skipped pass can still have a marker: the timer measures the
  marker's surrounding work, not proof that a shadow/reflection was redrawn.
- `present_calls` / `present_gaps`: completed flip-enabled End_Scene calls
  on the render thread (regular and extra) and observed gaps between them.
  These are CPU-side API completion observations, not GPU completion,
  successful D3D Present status or physical display refresh.
- `present_p50_upper_ms` / `present_p95_upper_ms`: conservative histogram
  bucket upper bounds, not exact percentiles. Blank means no gaps or the
  percentile falls above one second; distinguish using counts and overflow.
- Twelve `present_gap_*` columns: exclusive bucket counts, in order, with
  upper bounds 8333, 12500, 16667, 20000, 25000, 33334, 50000, 100000,
  200000, 500000 and 1000000 microseconds, then overflow. Despite `le` in
  names these are individual bins, not cumulative counts. Row boundaries
  retain the previous timestamp. Loading/menu pauses can produce long gaps.

Existing regular-frame pass timings exclude extras. Existing `gap_*`
columns remain limiter-based estimates; use the new completion-gap columns
for the next comparison. No per-mesh clock reads were added in extra frames;
only four exact pass labels are recognized, with a bounded eight-entry stack.
The pure helper's 7000-unknown-marker microbenchmark measured 64 us/frame on
this PC; this is not a whole-game overhead measurement.

## Next gameplay test

After authorized installation, keep the current INI, including
`ExtraFrameShadows=1`. Load the same troop-heavy save and hold a similar view
for at least a minute, then move around and observe shadows and camera stop.
Preserve the log and both CSVs before starting another session. Compare
CPU costs, valid extra samples, completion-gap buckets and game speed.
Do not infer a per-second FPS rate from a row covering a loading hitch.
The 60 FPS target still requires gameplay evidence.

The [full-pass audit](../specs/2026-10-17-full-pass-audit.md) records why broad
material merging and whole-object render caching are not justified by this
capture. The next candidate is reducing repeated CPU dispatch within an
already validated instancing group, subject to benchmarking and safety proof.

## Installation and rollback

Build commit `cad786f` is pushed on `codex/perf-beta` and installed locally;
it is not publicly released. Previous binary, INI and logs are backed up in
`%TEMP%/bfme2perf-0.9.11-beta.1-installed-20261017-173715`. A local package contains only the
ASI and the unchanged template INI. Existing users keep their own INI.
Back up the installed ASI, INI, log and CSVs before replacement; keep exactly
one ASI in `mss`. Restore the backed-up ASI to roll back. Stable branches and
release channels are unchanged.

## Automated validation (2026-10-17)

- Full plugin `mingw32-make test` passed: signature verification on both
  local executable images, native hooks, load/version, scheduling, cache,
  instancing, shadow-pass, catch-up and lighting tests.
- New pure and integrated tests cover malformed/nested/abandoned scopes,
  safety failure, wrong-thread calls, row drains, histogram boundaries,
  overflow, flip flags and extra-versus-regular presentation accounting.
- All 59 offline shader variants passed on native D3D9 and again on DXVK.
- Final build, CSV (including maximum-width row), hooks and load tests
  rerun after increasing the row buffer for the appended diagnostics.
- Logs: `perf/build/beta2-test.log`, `beta2-gpu.log` and
  `beta2-final-check.log`. No game or WorldBuilder was launched.
