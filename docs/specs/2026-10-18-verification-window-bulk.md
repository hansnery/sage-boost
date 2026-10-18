# Preserve bulk rendering for unsampled verification groups

Beta.12 fixes a disagreement between verification selection and bulk
admission. Beta.11 disables bulk rendering for every regular-frame group
while the verification window is open, even after a group's category quota
is exhausted. Evaluation already selects only a small number of groups.
Both decisions now use that existing selection predicate. Selected groups
retain the native verification path; other eligible groups can use bulk.

The window duration, total/shadow/category quotas, bone-list priority,
counters and failure policy are unchanged. Capture identifies the shader
category before leader deferral. The helper reads this state without
changing it. No new hook, signature, setting or scheduler change is needed.

## Correctness evidence

The production-source policy regression crosses 23,040 states. It preserves
1,056 selected cases and admits 2,784 previously excluded unsampled regular
open-window cases. Accounting and the other eligibility guards are checked.
The pre-fix implementation fails the admission assertion. The test is part
of `make test`.

Native/GPU controls pass for AotR and ROTWK, native D3D9 and DXVK, lit and
fresh-shadow rendering: eight candidate runs. Four old-source AotR controls
also pass with their expected exclusion. Each run includes 136 group
comparison pairs, explicit selected/unsampled cases, whole-queue output,
state/reference/ordering checks and three broken-queue controls. The selected
case still verifies; the unsampled regular and extra cases do not consume
samples. The full plugin suite passes. Its first attempt exposed a stale
beta.11 string in the loader test; the assertion was updated and the full
suite rerun successfully.
The changed instancing object and surrounding production bulk/submission
objects pass the instruction audit: no x87, SIMD or FP-control instructions.

## Performance and promotion limits

The timing fixture forces an open window with exhausted category quotas
on every timed frame. Six- and twenty-bone lit/shadow workloads retain
21 samples each for old and new code, using AotR/DXVK. The old source is
our `inst.c` at `35ac82b`, generated into the ignored build folder. All
native game bodies remain private in memory. No EA bytes or assets ship.

Each executable alternates ordinary-instancing and optimized queue paths;
its printed `SUMMARY` is the combined queue/group saving, **not the new
fix's incremental saving**. Old/new optimized medians come from separate
runs. Unchanged extra-frame work also varies substantially between runs,
so their raw difference is not a reliable isolated improvement estimate.
The manifest retains both medians and all 168 samples without an FPS claim.

In the actual beta.11 capture, all six verification samples occur in one
reporting row in each of twelve windows (seconds 102, 107, ..., 157). That
does not establish the exact window duration, but it does not support a
sustained two-second penalty in this scene. The forced workload cannot be
multiplied by a presumed duty cycle to predict gameplay performance.

Keep this correction in beta development, but do not install it or request
another gameplay test on this evidence alone. The last real result remains
30.0667 presentation calls/s, with the user's screenshot showing 29 FPS.
Installed beta.11 and the user's INI remain unchanged.

Evidence: [validation manifest](../benchmarks/2026-10-18-verification-window-checks.json)
and [168 timing samples](../benchmarks/2026-10-18-verification-window-samples.csv).

## Reproduction

With the game and launcher closed, build `make test` and these optional
targets with the documented i686 toolchain:

- `build/bench_verify_window_gpu.exe`
- `build/bench_verify_window_old_gpu.exe`
- `build/bench_verify_window_20_gpu.exe`
- `build/bench_verify_window_old_20_gpu.exe`

Run `tools/bench_native_insert_gpu.py` with the installed `game.dat`, the
chosen benchmark executable, the user's `Shaders.big` and `msvcr71.dll`,
then `lit narrow 10 leader-bulk-first` (or `shadow`). Use
`--controls-only --self-test` for correctness controls. The DXVK cases run
from an ignored build directory with a copy of the user's `d3d9.dll` beside
the benchmark. Native cases have no local D3D9 proxy. Repeat controls with
`game820.dat`. Run benchmarks serially; do not launch the game for them.

## Remaining shadow fragmentation

Follow-up: the [material-chain screen](2026-10-18-material-chain-bulk.md)
finds that the capture's cumulative shadow-state summary has **zero**
remaining comparator splits after 6,119 original splits are merged. Do not
treat the hypothesis below as evidence for changing opacity handling.
Native grouping also compares the entire method array; that condition is
separate from the shadow-state comparator and is the next investigation.

The copied capture averages about 1,900 meshes/frame in adjacent shadow
groups sharing a model and method. This counter establishes fragmentation,
not its cause. The current batching predicate still preserves material
eligibility, matching presence of light environments, and opacity.

The public read-only Open-BFME-2 checkout at
`ff1544d9fef63a5460d1fcd012cea309c13f7dff` supplies a useful clarification:
the matched mesh virtual getter at slot 25 is not a single alpha-field
load. Its base getter multiplies two factors (+0x90 and +0x94), and the
mesh override multiplies that result by AlphaOverride (+0x2F4). The public
ledger identifies the 31-byte base body and 16-byte mesh body as matched.
This is reference evidence; the corresponding installed-image bodies and
virtual dispatch must still be qualified before any runtime use.

Relevant public paths are `MeshClassLifetime.cpp`, `RenderObjSceneAndCost.cpp`
and `MeshStateEqualBFME2.cpp` in `Code/Libraries/Source/WWVegas/WW3D2`, with
the ledger in `reverse/functions.csv`. The shader's opacity also includes
its material/preshader product. Reading only the mesh alpha field or
dividing by the leader's opacity is not a justified replacement. Any future
mixed-opacity batching needs exact rounding/zero handling, unchanged fade
output, native fallback and complete state restoration. No such change is
implemented or claimed here.
