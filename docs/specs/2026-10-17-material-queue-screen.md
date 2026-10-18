# Material-queue cache: correctness screen and excluded timings

## Status

The larger offline prototype now runs the local game's D3DX27 effect
lifecycle and can reuse preparation in both regular and extra frames.
Native D3D9 and DXVK image/state checks pass. **Performance qualification
is pending; this is not an installable optimization or a 60-FPS result.**

The implementation and reproduction steps are in
[MATERIAL_QUEUE_BENCHMARK.md](../../perf/tests/MATERIAL_QUEUE_BENCHMARK.md).
No production source changed. No game or WorldBuilder was launched by this
work, no installation was performed, and no gameplay test was requested.

## What this adds to the earlier experiment

The earlier retained benchmark measured GPU setup without effect binding.
The new baseline executes parameter-block application, dynamic-input
setters, texture binding, Begin/BeginPass, leader bone upload/CommitChanges,
EndPass and End. It uses the real local defaultw3d effect with synthetic
geometry and inputs, rather than implementing or redistributing EA shaders.

One mode still records all groups every regular frame and reuses them only
in the extra. A second, persistent mode computes complete used non-bone
input keys and reuses unchanged groups in regular frames too. It still
prepares and checks inputs on every group. Changed inputs take the normal
path and refresh the snapshot. Separate controls invalidate zero, 30 or
all 288 groups each modeled frame pair.

Each group occupies its own screen region, with generated cutout textures,
opacity and colour variation. This prevents earlier groups being entirely
hidden behind later groups during output comparisons. The cached draw
uses new palettes every frame and redraws the shadow map.

## Correctness evidence

- Both final variants match baseline output bit for bit on native D3D9 and
  DXVK: 16,668 covered lit pixels and 16,644 shadow texels in the main
  comparison. Shader hashes B2EA5BA1/635CC3FF identify supported variants
  476/5, which also appear in the preserved gameplay capture.
- The stale-pose control changes 20,962 lit pixels and 21,130 shadow texels.
  Changing all material inputs changes 17,044 lit pixels and 1,860 shadow
  texels; stale snapshots fail that control and recapture matches.
- Persistent mode compares regular and extra output across zero, partial
  and complete invalidation. Exact miss counts pass. Geometry bindings and
  stream frequencies are reconciled before a miss following a hit.
- Recorded material states, shaders, geometry bindings and shader constants
  are restored at the fixture's boundaries. Method references and draw/mesh
  counts balance. Missing snapshots are rejected.
- The complete plugin suite passed. The original retained benchmark's four
  output/state comparisons still pass with the optional callbacks disabled.

These tests do not establish engine-side state equivalence, object/resource
lifetimes, device reset, arbitrary materials, native call-site ABI, shroud
or visibility behavior. The persistent regular shortcut also omits per-mesh
markers; preserving useful diagnostics remains a runtime obligation.

## Timing exclusion

The first repeated measurements appeared materially larger than the earlier
setup-only cache: the 10% invalidation mode reported DXVK paired completion
savings of roughly 3.8-4.7 ms. **Those measurements are excluded.**

The six recorded matrix runs took place between 19:57:49 and 19:59:54 local
time. Read-only process inspection found game.dat PID 121468 had started at
19:57:15, with launcher PID 121348 starting one second earlier. Thus all
504 recorded samples overlap a running game process. The copied game CSV
contains menu rows with extra draws, so the process cannot be treated as
idle. Shared CPU/GPU contention invalidates these as clean performance
evidence, although it does not invalidate independent image comparisons.

[All 504 samples](../benchmarks/2026-10-17-material-queue.csv) are preserved
with `qualification=excluded` and `exclusion=concurrent_game_process`.
They must not be used to justify installation or predict saved-game FPS.
Lit/shadow are alternative complete workloads; their savings are not additive.

A new read-only process guard now checks before GPU setup and before/after
each paired sample. Both retained targets refuse if game.dat, either BFME
launcher, or WorldBuilder is running, or if process enumeration fails.
It was compiled and observed refusing the live launcher before starting
the fixture. Do not add an override to collect allegedly clean samples
alongside gameplay. The no-game success path needs its next quiet run.

## Installation observed independently

At 20:00, the installed ASI reported 0.9.10, modified at 19:57:12, SHA-256
`db65e51d42bb7f49947631a17b9aeebc0604f3cd9e2c055d8e8a35daf65408e5`.
This differs from the previously installed beta.2. This work did not replace
it. The current log confirms 0.9.10 loaded by game.dat; the cause of the
external replacement is unverified. The INI hash remains unchanged.

INI/log/CSVs were copied before reading to
`%TEMP%/bfme2perf-observed-20261017-200053`. No qualifying busy-scene rows
appear in that copy; it does not update the last ~30-FPS gameplay result.
The local beta.3 build remains unchanged and uninstalled.

## Next work

Rerun the full measurement matrix with the process guard succeeding. Keep
all results, including complete-invalidation regressions. Only then decide
whether the persistent cache has enough upside to justify a native bridge.

Meanwhile audit the actual FXShaderSetup lifecycle and native group loop:
the reference Begin path applies render state, parameter block, textures
and all parameter-source callbacks before D3DX Begin. Skipping the whole
method would omit that work and its side effects. A safe bridge must retain
it, preserve effect lifecycle/cleanup and engine-visible render state, use
current records rather than borrowed objects across frames, and fail before
partial replay. The current fixture is not such a bridge.

The ongoing goal remains sustained 60 visual FPS in the user's troop-heavy
save with fresh shadows and unchanged multiplayer behavior. Do not lower
that goal to a synthetic gain or request a diagnostic-only gameplay cycle.
