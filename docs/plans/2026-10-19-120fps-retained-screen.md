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
- [ ] Run native/DXVK lit/shadow cases with one and three extras, alternating
  ordering, four warmups, 21 samples and eight cycles per sample.
- [ ] Preserve raw sample rows and source/binary hashes; report CPU/fenced
  cycle savings and whether capture amortization changes the earlier decision.
- [ ] Commit and push explicit files. Keep the installed beta.17 as baseline;
  qualify native scene/lifetime/state work only if this screen warrants it.

## Current execution state

The standalone executable builds with warnings as errors. The first run
refused before GPU setup because BFME2 was running; no timing samples or
image-control passes exist yet. The report tool compiles and requires all
four cases with all phases and 21 samples for each count before reporting.
It explicitly identifies the inherited fixture baseline as different from
an end-to-end beta.17 comparison. Resume the same make target after the
game and launcher close; no background benchmark process is pending.
