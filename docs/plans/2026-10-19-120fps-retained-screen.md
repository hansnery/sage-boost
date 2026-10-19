# Multi-extra Retained Rendering Screen Implementation Plan

**Goal:** Establish whether capture-once/replay-three has enough measured
benefit to justify deeper engine integration toward the user's 120 FPS target.

**Architecture:** A standalone offline fixture embeds the existing retained
benchmark, adds independently changing poses and validates each extra. The
production renderer and old benchmark retain their behavior.

**Tech Stack:** C, WinLibs i686 GCC, local D3D9/DXVK, Python result analysis.
Execute inline under Hans's standing preapproval; no additional agent needed.

## Constraints

No game/WorldBuilder launch, no installed or release changes, no EA assets
saved. Preserve fresh shadows and identical geometry. Charge capture cost,
report complete cycles, and label synthetic limitations.

- [x] Add `perf/tests/bench_retained_multi.c` with distinct phase palettes,
  baseline/retained image checks for each of three extras, stale-pose controls,
  method references and existing per-frame count/state checks.
- [x] Add an isolated Makefile build/run target using existing dependencies.
- [x] Run native/DXVK lit/shadow cases with one and three extras, alternating
  ordering, four warmups, 21 samples and eight cycles per sample.
- [x] Preserve raw sample rows and source/binary hashes; report CPU/fenced
  cycle savings and whether capture amortization changes the earlier decision.
- [x] Commit and push explicit files. Keep the installed beta.17 as baseline;
  qualify native scene/lifetime/state work only if this screen warrants it.

## Current execution state

After the game closed, all four setup-only cases completed. All three phases
pass output and stale-pose controls. DXVK's three-extra completion savings
are 2.77 ms lit and 2.45 ms shadow per cycle; one extra is -0.36/+0.20 ms.
This justifies a stronger screen, not installation or a gameplay-FPS claim.
The results are in `docs/benchmarks/2026-10-19-retained-multi.{json,csv}`.

## Follow-up: material and bulk costs

- [x] Extend the existing fixture with real local effect Begin/End, exact
  material keys, per-group state restoration and aggregate scope accounting.
- [x] Charge fresh captures every ordinary frame; validate extra inputs and
  preserve the existing mid-pair material-invalidation negative control.
- [x] Advance Time between cycles; compare against the existing bulk-dispatch
  fixture, keeping its optimistic/native-callback limitations explicit.
- [x] Run the same one/three-extra matrix; record any failure or regression.
  Only pursue runtime ownership/invalidation work if savings survive.

This follow-up still does not execute the game's native FX loop or parameter
source callbacks. Time is constant inside a timed cycle; if actual extra
frames change it, cache misses can erase savings. No installed change follows
from this screen alone.

The stronger matrix passes correctness but regresses on DXVK by 2.52/2.11 ms
per three-extra lit/shadow cycle. Runtime promotion is rejected. Both results
and 336 total raw sample rows are preserved; no installation was performed.
