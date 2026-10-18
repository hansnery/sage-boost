# Compatible-member dispatch: offline screening result

## Decision

Do not build or install a runtime beta for this shortcut alone. The final
measurements save about **1.4-1.7 ms of submission time** per synthetic
regular-frame-sized workload with Detail=1, below the approximately 3 ms
screening threshold chosen before the experiment. This is a useful possible
component of a larger change, not evidence of sustained 60 FPS.

The user requested another gameplay test only after a significant
improvement. No gameplay test is requested. Installed beta.2 and its INI
remain byte-identical; no setting, cadence, shadow quality or production
code changed. No game or WorldBuilder was launched.

## Experiment and results

The [benchmark description](../../perf/tests/DISPATCH_BENCHMARK.md) documents
the independently modeled outer loop and real plugin hooks, GPU draws and
test-only bulk helper. It includes 7,200 distinct synthetic mesh identities
in an 11.29 MB working set, 288 groups and 6,912 instanced meshes. The trace
averaged about 288 instancing groups and 6,837 instanced meshes per frame.

Both colour and shadow output match bit for bit; references, counts, bulk
consumption and relevant restored device state pass. The helper bypasses
6,624 member dispatches per workload. The optimized side deliberately omits
those members' markers, so it is optimistic relative to a production path
that must retain meaningful Detail=1 diagnostics.

Two final runs, after regression tests finished, each used 21 paired
samples per configuration. Each sample averages eight separately fenced
workloads; four warmup pairs precede it and AB/BA order alternates.
Times below are medians of paired savings, in milliseconds per workload.

| Backend / workload | Detail=1 submit, run 2 / 3 | GPU-completed, run 2 / 3 | Detail=0 submit, run 2 / 3 |
|---|---:|---:|---:|
| Native D3D9, lit | 1.439 / 1.647 | 1.414 / 1.669 | 1.043 / 1.070 |
| Native D3D9, shadow | 1.495 / 1.559 | 1.439 / 1.499 | 0.960 / 0.832 |
| DXVK, lit | 1.592 / 1.721 | 1.496 / 1.487 | 1.221 / 1.026 |
| DXVK, shadow | 1.583 / 1.597 | 1.517 / 1.450 | 1.108 / 1.097 |

[All 336 paired samples](../benchmarks/2026-10-17-dispatch.csv) are committed.
Each alternative processes the whole 7,200-mesh workload: **do not add lit
and shadow numbers together**. The real frame's count already includes its
shadow, reflection and view passes. GPU-completed includes event-query wait,
not a GPU timestamp duration, and submission can include driver stalls.

Exploratory single-workload samples showed substantial wall-time variation,
including one native shadow run with a 2.738 ms median saving. Rather than
select a favorable run, final samples average eight fenced workloads and
retain every paired result. An initial averaged run overlapped the tail of
the regression process and is excluded; the committed runs were collected
after that process had exited. This still is not a controlled replay of the
game: one material, two shader variants, synthetic geometry/poses, no scene
setup, effect Begin/End, PIX or actual caller instructions. The result is a
screening measurement, not a formal bound or a prediction of saved-game FPS.

Reproduce from Git Bash with the documented i686 toolchain on PATH:

```sh
cd /c/repositories/sage-boost/perf
mingw32-make bench-dispatch
```

The recorded backends are Windows' `d3d9.dll` and the copied installed DXVK
at `perf/build/dispatch_dxvk/d3d9.dll`. No game executable is loaded by the
benchmark. The event copy shape was cross-checked read-only against the
installed AotR constructor; it is modeled in independent C, not replayed.

## Validation and next boundary

Full `perf/` test suite passed, including the installed AotR/game820 site
contracts, native hook/stack checks, scheduler safety, instancing, shadows,
catch-up and lights. The final benchmark also runs the existing instancing
GPU fixture before its checks on both backends. Shaders and runtime source
were unchanged, so the separate 59-variant matrix was not repeated here.

Installed SHA-256 values remain:

- ASI: `05d9b381feb2b7ddc0001466ea8c23333e33ce06679786ca2bd1b4c87de94799`
- INI: `79bbb612a6ce22e76f930e9334a6e650bc2c49c3d28260c6bc716c026da94c54`

The standalone dispatch route is now measured and deprioritized. It does
not justify paying for new installed-loop signatures, continuation/SEH
proof and replacement scope accounting yet. A combined optimization would
need additional measured savings before reconsidering those obligations.
The larger unresolved item remains the busy-scene extra-frame cost: the
14.641 ms estimate is stale, but there is no safe measured replacement in
the captured trace. This experiment neither resolves that blind spot nor
loosens the multiplayer deadline checks. Sustained 60 FPS with full-rate
shadows remains unachieved.
