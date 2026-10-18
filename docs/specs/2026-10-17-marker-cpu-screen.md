# Render-marker CPU screen

Three guarded runs after the user closed BFME2 and its launcher. This is
an offline cost screen, not a gameplay or FPS measurement. The executable
does not run a scene or Present. GPU fixture initialization happens before
timing. Production hook objects use the ASI's compiler flags.

Each workload processes 7,200 synthetic mesh labels. Four warmups precede
21 rounds, each averaging eight workloads. Mode order reverses on alternate
rounds. The original game's local CRT constructs the labels and performs
the modeled native event copies. The production entry hooks run unchanged.
The unhooked control does not execute native SEH, GetModelName or PIX code.

| Mode | Run 1 ms | Run 2 ms | Run 3 ms |
|---|---:|---:|---:|
| Regular, Detail=1 | 1.202300 | 1.196262 | 1.196950 |
| Regular, Detail=0 control | 0.710162 | 0.708588 | 0.705887 |
| Extra-frame hooks | 0.753875 | 0.751625 | 0.752813 |
| Unhooked modeled copies | 0.333375 | 0.328887 | 0.325613 |
| Label construction only | 0.095125 | 0.096738 | 0.094912 |

Values are workload medians. Every regular Detail=1 workload records exactly
7,200 scopes; other modes record zero. All modes preserve the stale-scope
count. Detail=0 is an offline cost control; no installed setting changed.
[All 315 samples](../benchmarks/2026-10-17-marker-cpu.csv) and
[run/source hashes](../benchmarks/2026-10-17-marker-cpu-runs.json) are retained.

The full marker workload costs approximately 1.95 ms per modeled regular
plus extra pair. Only about 1.29 ms is the difference from the modeled
unhooked copies. Neither is an achievable standalone saving: preserving
diagnostics and debug callbacks has a cost. These results do not justify
disabling Detail or releasing a profiler-only beta.

The next experiment can combine batch-level accounting with the existing
material-cache fixture. Preserve the number of logical mesh scopes and the
total time spent staging/submitting their work. This changes the measurement
boundary from separate per-member intervals to one aggregate interval; it
must be documented rather than called identical per-event instrumentation.
Any runtime version must retain individual native events when a debug
callback is active and prove native lifetime and cleanup behavior first.
The existing per-member protocol remains the comparison baseline.

## Source follow-up: dynamic skeleton binding

The reference `MeshClassRenderWithFXMaterial.cpp` sets the skeleton context
immediately before channel-1 binding and clears it after rendering. The
matched `Rva0014D405Clear.cpp` clears its tree, bone-list and joint-count
fields. `fxshadernamespacesas.cpp` shows the no-context bone setter taking
the world-transform fallback, rather than copying the previous mesh's full
bone palette. Thus the proposed duplicate *full palette* update at group
Begin is not supported by this source path. The fallback still does work;
no measurement here claims it is free or authorizes skipping callbacks.

Read-only disassembly of AotR's PerformRendering also confirms that its
EBP+8 slot is updated to the current group's end. The existing shadow-light
reconciliation walk uses that group boundary, not the whole queue. No
quadratic whole-queue rescan defect was established.

## Reproduction

From the documented Git Bash toolchain, in `perf/`:

```sh
mingw32-make bench-scopes
```

The read-only process guard refuses setup or samples while BFME2, its
launcher or WorldBuilder is running. The test links the existing synthetic
fixture; local assets stay in memory. No production source or installed
binary changes result from this screen. Sustained full-rate 60 FPS remains
unachieved; the last qualifying heavy-save capture is about 30.18/s.
