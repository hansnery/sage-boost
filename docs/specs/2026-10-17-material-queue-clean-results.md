# Material cache: clean measurements and lifecycle/marker controls

## Decision

Do not promote this prototype as a standalone beta. Once real effect
Begin/End, extra-frame input validation and per-mesh markers are included,
the three final DXVK runs save only **0.66-1.04 ms per modeled frame pair**
at 10% material invalidation. Individual samples regress. The remaining
native group, lifetime and state obligations make installation unjustified
at this gain. This does not achieve the 60 FPS goal.

The user closed the game and launcher before these runs. Read-only process
checks found neither, and the benchmark guard passed before setup and
before/after every paired sample. Runs were sequential. The original 504
concurrent-game samples remain excluded and were not mixed into these data.

## Clean comparison

The original optimistic protocol was first repeated without competing game
activity. It saved 3.24-3.92 ms in DXVK completion across three 10% churn
runs. The narrower repeat range confirms a real synthetic difference, but
that protocol omits native lifecycle and marker work on hits.

A stronger protocol now runs real D3DX27 Begin/End around every hit. It
also prepares and checks complete non-bone inputs again in extras; changed
extra inputs fall back before drawing. It does not call CommitChanges or
EndPass without a real BeginPass. Begin/End provide the effect's own state
save/restore behavior, as described by [Microsoft's Begin contract](https://learn.microsoft.com/en-us/windows/win32/direct3d9/id3dxeffect--begin).
The first lifecycle-only run saved 3.54 ms lit / 3.27 ms shadow on DXVK.

The final protocol additionally creates the same per-mesh labels, performs
the same bounded copies and runs the real plugin's marker entry/exit hooks
during replay. Each marker covers palette staging and any batch draw it
triggers. Regular frames must report exactly 7,200 mesh scopes and no stale
scopes; extras preserve the existing policy of not counting their scopes.
This is the more relevant comparison for the current Detail=1 installation.

Median GPU-completed pair savings, in milliseconds:

| final case | native lit | native shadow | DXVK lit | DXVK shadow |
|---|---:|---:|---:|---:|
| 10% invalidation, run 1 | 3.185 | 3.015 | 1.041 | 0.973 |
| 10% invalidation, run 2 | 3.115 | 2.880 | 0.670 | 0.757 |
| 10% invalidation, run 3 | 3.276 | 2.761 | 1.002 | 0.656 |
| no invalidation | 3.525 | 3.086 | 1.222 | 1.065 |
| complete invalidation | 1.316 | 1.233 | 0.072 | 0.297 |

These are savings against each backend's own baseline, not absolute
backend comparisons. Lit and shadow each contain the whole 7,200-mesh
workload; do not add their gains. Do not add these gains to earlier
dispatch/cache experiments. Neither completion nor submission measures
presentations, and no saved-game FPS can be inferred from these numbers.

## Correctness and refusal evidence

All final native/DXVK comparisons retain bit-identical output: 16,668 lit
pixels and 16,644 shadow texels covered. Fresh-pose and material changes
remain visible in the negative controls. Zero, partial and complete regular
invalidation pass. A new extra-frame test changes inputs after capture,
requires all affected groups to fall back, checks the output, then checks
the warmed extra and its exact hit/miss counts.

Every final case also verifies 20,736 individual Begin/End boundaries.
The fixture seeds clockwise culling before validation, while its effect
disables culling during drawing. End must restore all recorded render
states to their entry values, including on retained hits. This exercises
165,888 changed states in the lit test and 20,736 in the shadow test.
These additional state reads are disabled before timed warmups/samples.
Sampler/shader states and native engine caches have different obligations;
this check does not prove their complete runtime equivalence.

An earlier validation attempt checked restoration without first changing
the shadow entry state. Its positive control correctly failed because no
changed state was exercised. That partial run is excluded. The final
opposite-cull-state control fixes the test, not the rendered technique.

Method references, draw/mesh counts, frame-boundary GPU state and missing
snapshot refusals remain checked. The existing retained benchmark's four
image/state cases, full plugin suite and native FX lifecycle checks pass
after the optional callbacks are added. No production
source, shader code, ASI or installed configuration is changed by this work.

## Data and reproduction

- [504 clean original-protocol samples](../benchmarks/2026-10-17-material-queue-clean.csv)
  and [run timestamps/hashes](../benchmarks/2026-10-17-material-queue-clean-runs.json).
- [420 final lifecycle/marker samples](../benchmarks/2026-10-17-material-queue-markers.csv)
  and [protocol/source hashes and run metadata](../benchmarks/2026-10-17-material-queue-followup-runs.json).
- [189 exploratory/partial samples](../benchmarks/2026-10-17-material-queue-exploratory.csv),
  labeled separately; these do not replace the final protocol.

From `perf/`, run `mingw32-make bench-material-queue MATERIAL_CHURN=10
MATERIAL_PROTOCOL=lifecycle-markers`. Use churn 0 or 100 for the controls.
`MATERIAL_PROTOCOL=lifecycle` retains real Begin/End and extra input checks
but omits replay markers; omitting the protocol retains the original screen.

## Next direction

The promising-looking cache gain depended heavily on omitted marker work.
Investigate the cost of marker processing itself before further native-cache
integration. Any cheaper diagnostic path must preserve useful scope/count
information and the native debugging contract; changing the user's Detail
setting or silently dropping diagnostics is not a demonstrated solution.
The actual native marker constructor/cleanup and profiler dispatch need
separate evidence. Rendering-cache integration remains deferred.

The last qualifying troop-heavy gameplay capture is still about 30 FPS.
No new gameplay test is requested for this experiment.
