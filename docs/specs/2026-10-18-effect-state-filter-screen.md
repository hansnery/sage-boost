# Effect state-write filtering: offline screen

Filtering redundant D3DX device writes does not justify a runtime beta.
The final standalone filter costs about 0.48-0.50 ms more per modeled pair
on DXVK. Combined with bulk mesh dispatch, two repeats save 1.47-2.11 ms,
similar to the earlier dispatch-only screen and below the roughly 3 ms
screening gate. The last heavy-save gameplay capture remains about 30.18
presentations/s. No production code or installed files changed.

## Candidate and controls

Unlike the rejected material cache, this experiment retains every modeled
source setter and effect Begin/BeginPass/CommitChanges/EndPass/End. Time
advances by 33 ms between regular/extra pairs and stays fixed within each
pair. A user-defined D3DX state manager forwards changed values and omits
only exact repetitions it has observed. Its scalar/byte comparisons compile
with the plugin's `-O2 -mgeneral-regs-only` flags.

[Microsoft's state-manager interface](https://learn.microsoft.com/en-us/windows/win32/direct3d9/id3dxeffectstatemanager)
allows effect state callbacks and makes restoration the application's
responsibility. This is not permission to omit restoration. The local
D3DX27 Begin(flags=6) behavior is compared directly: the fixture's affected
render states **and texture bindings** return to their entry values. The
prototype restores those through an explicit state block and invalidates
their mirror entries after End. Other observed shader, float-constant and
sampler entries may survive until a frame boundary, where all are invalidated.

Original and filtered paths have separate effect instances with the same
synthetic inputs and shared textures. The filtered instance receives its
manager before any default Begin. This avoids the history-dependent behavior
encountered while switching managers on one already-used effect. Both paths
pay the same enclosing frame-state capture/restore and verification costs.

Three protocols isolate the change:

- `state-filter`: original member dispatch and filtered effect device writes.
- `state-filter-bulk`: the same filter with the existing aggregate dispatch
  after the first two original members. Logical mesh counts remain intact.
- `state-forward`: the same manager/restoration protocol, forwarding every
  request, as a control for the manager's own cost.

All retain the original local effect and fresh bone palettes. There is no
material snapshot replay, shader-Time omission, graphics reduction or
skipped shadow draw. Fixture resources stay alive throughout; this is not
a lifetime-safe manager for arbitrary game effects or a device interceptor.

## Measurements

The existing 288-group, 7,200-mesh workload runs native D3D9 and DXVK lit
and shadow cases serially. Each case uses four warmups, 21 AB/BA rounds,
and eight fenced regular/extra pairs per round. Process guards pass before
setup and between rounds. Completion includes the final GPU fence wait;
neither it nor submission is a Present/FPS measurement.

Median completion savings in milliseconds, negative for slower execution:

| Protocol | Repeat | Native lit | Native shadow | DXVK lit | DXVK shadow |
|---|---:|---:|---:|---:|---:|
| Filter | 1 | -0.5657 | 0.1851 | -0.4769 | -0.5009 |
| Forward control | 1 | -0.7121 | -0.3244 | -0.3327 | 0.0875 |
| Filter + bulk | 1 | 2.1341 | 2.8563 | 2.1083 | 2.0050 |
| Filter + bulk | 2 | 2.5741 | 2.5074 | 1.6999 | 1.4723 |

The combined DXVK p10/p90 ranges are 1.62/3.31 ms and 1.49/1.98 ms lit,
1.07/3.39 ms and 1.20/1.92 ms shadow. Lit and shadow each use the full
synthetic mesh population; their savings must not be added. Component
screens overlap and must not be added either. These results do not predict
cache hit rates, CPU savings or FPS in the saved game.

[336 final samples](../benchmarks/2026-10-18-effect-filter.csv),
[294 separate exploratory/excluded samples](../benchmarks/2026-10-18-effect-filter-exploratory.csv),
and [build/source/log metadata](../benchmarks/2026-10-18-effect-filter-runs.json)
preserve the evidence. No effect or shader bytecode is stored.

## Correctness scope and rejected assumptions

Each final case compares complete extra-frame output and fresh-pose controls;
lit/shadow output is exact with 16,668/16,644 covered texels. Existing frame
state, method-reference and logical scope-count checks pass. The separate
state preflight compares 32 original/manager cases, both inside the pass
and after End, including consecutive identical materials, changed materials
and two clock values. Both effects receive identical complete bone arrays
for these state comparisons. Shader identity is compared by code hash
because separate effects can own distinct COM objects.

An external texture write deliberately makes the mirror stale. Repeating
the cached setter without invalidation leaves the wrong binding and is
detected; invalidation then restores the expected binding. The timed fixture
has no unobserved mid-group mutations except instancing operations that
restore their original state. A runtime implementation would need complete
observation/invalidation of native writes, including state-block application.
That integration, arbitrary effects, deletion/reset and failure recovery
are not proved here. Native source-callback computations are not executed.

The full plugin test suite passes. The earlier
`lifecycle-bulk-state-clock` protocol also passes again on all four backend/
workload cases, including its 10,309 state-boundary checks per case. Sample
counts and recorded source/executable/raw-log hashes are verified. Built
beta.3, installed 0.9.10 and installed INI SHA-256 hashes remain unchanged.

Early checks wrongly assumed a non-null texture after End; inspecting the
binding inside the pass corrected the control. Consecutive-material checks
then rejected carrying stale entries across the single-effect switching
protocol. Those 84 timing samples are excluded. Two later switching-protocol
controls remain exploratory, separate from the permanent-manager runs.

An initial separate-instance check also exposed different unused bone
parameter values; the preflight now seeds the full parameter identically.
One partial native run preceded a DXVK shader-hash failure: GetFunction had
not first queried the exact byte length, so unused buffer capacity entered
the comparison. The final code queries the length first; the 42 partial
samples are excluded. These were fixture failures, not observed game bugs.

Reproduce from `perf/` in the documented toolchain:

```sh
mingw32-make bench-material-queue MATERIAL_CHURN=10 MATERIAL_PROTOCOL=state-filter
mingw32-make bench-material-queue MATERIAL_CHURN=10 MATERIAL_PROTOCOL=state-filter-bulk
mingw32-make bench-material-queue MATERIAL_CHURN=10 MATERIAL_PROTOCOL=state-forward
```

Do not install this filter or spend a user gameplay test on it. The evidence
supports moving beyond individual effect-state calls toward larger repeated
render setup work, with a new cost screen before runtime integration.
