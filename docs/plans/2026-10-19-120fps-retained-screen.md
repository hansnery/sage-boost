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

- [ ] Add `perf/tests/bench_retained_multi.c` with distinct phase palettes,
  baseline/retained image checks for each of three extras, stale-pose controls,
  method references and existing per-frame count/state checks.
- [ ] Add an isolated Makefile build/run target using existing dependencies.
- [ ] Run native/DXVK lit/shadow cases with one and three extras, alternating
  ordering, four warmups, 21 samples and eight cycles per sample.
- [ ] Preserve raw sample rows and source/binary hashes; report CPU/fenced
  cycle savings and whether capture amortization changes the earlier decision.
- [ ] Commit and push explicit files. Keep the installed beta.17 as baseline;
  qualify native scene/lifetime/state work only if this screen warrants it.
