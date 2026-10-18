# Larger bone-palette workload screen

**Goal:** Measure the production bulk path on twenty-bone units observed in
the user's verification log, before trusting further synthetic speedups.

**Architecture:** Add a separately named twenty-bone runtime GPU executable
using `PF_FIXTURE_BONES=20`; retain the default six-bone fixture. Increase
its native tree storage with the bone count, exercise every bone index,
and derive expected batch counts from the selected shader's register space.
The actual runtime hooks, native body verification and control matrix stay
in use. No source/asset is taken from another performance project.

- [x] Parameterize the fixture palette and tree size in `test_inst.c`.
  Extend mesh indices to cover all twenty bones.
- [x] Generalize draw-count checks in `bench_dispatch.c`,
  `bench_material_queue.c` and `bench_retained.c`. Report the actual DIP
  reduction: including the leader does not always remove a draw when a
  full batch splits. Add the separate Makefile executable target.
- [x] Run installed-path native D3D9/DXVK controls on both executable
  families, lit and shadow, and retain the six-bone regression checks.
- [x] Measure twenty-bone combined work with the game closed, preserving
  serial paired samples and comparing only equivalent workload/settings.
  Treat the result as coverage/sensitivity evidence, never saved-game FPS.
- [x] Record findings and choose the next CPU-rendering target. Do not
  install a diagnostic build or weaken pacing/shadow/quality constraints.

Result: all ten distinct control cases pass; 126 raw timing samples are
retained. Twenty-bone groups require four-instance batches, and including
the leader removes no draw in a 25-unit group. Optimized pair costs are
substantially higher than six bones. Next audit native material-source
work; this is not a new optimization or gameplay FPS result. See
[results](../specs/2026-10-18-workload-screens.md).
