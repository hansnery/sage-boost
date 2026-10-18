# Material cache: per-group restoration and advancing clock

The combined material-cache candidate **does not qualify for runtime
promotion** after these stronger controls. Advancing the effect's clock
reduces DXVK completion savings to 0.61-1.08 ms per modeled regular/extra
pair. Adding per-hit restoration of the fixture's touched device state
then makes it 0.26-0.87 ms slower. These are offline workload timings;
the last qualifying gameplay capture remains about 30.18 presentations/s.

This supersedes the favorable screening decision in the earlier
[aggregate-accounting report](2026-10-17-material-bulk-accounting-screen.md).
It does not rule out cheaper state reconciliation or other cache designs.
No production code, installed binary, settings or game files changed.

## Why the previous screen was optimistic

The previous fixture restored device state at the end of the whole frame.
A native integration also has to leave intervening original draws and the
engine's binding caches coherent. The new `lifecycle-bulk-state` mode
captures and restores the fixture's full touched-state mask on each hit:
shaders, declaration, index/vertex buffers, stream frequencies, all 256
vertex and 32 pixel float constant registers, and material render,
sampler and texture states. This is one conservative strategy for the
fixture, not a proven minimum cost or a native engine-cache contract.

Correctness checks compare the state before and after each hit. A control
that omits restoration is rejected. A separate control deliberately alters
shader/buffer bindings, stream frequencies and boundary constant registers,
then verifies restoration without drawing the incompatible state. Exhaustive
state reads are disabled before timing; capture and restore remain timed.

The exact material keys also include `Time` in **both** fixture techniques.
The older timing screen held it constant. The read-only Open-BFME reference's
`fxshadernamespacesas.cpp`, `Rva0014D564Now`, supplies this parameter from
`WW3D::Get_Sync_Time() * 0.001f`. The plugin's verified display-sync sites
advance that time with client frames and preserve sync state around extras
(`perf/src/fpsites.c`, `perf/src/fps.c`). A fixed clock is therefore not a
reasonable basis for assuming persistent regular-frame hits during play.

The new clock modes advance a synthetic `Time` by 33 ms per pair, holding it
constant within that regular/extra pair. Both measured paths receive the
same values. Correctness checks require all 288 regular groups to miss
after a clock change and all 288 following extra groups to hit, with matching
output. This models a changing input; it does not measure actual gameplay
cache hit rates or change any game clock.

A real D3DX command recorder also distinguishes key changes from device
commands. Changing `Time` by 123 changes two of 962 recorded words in the
lit fixture. The shadow fixture's 853 words remain equal for those fixed
inputs. That single shadow comparison is not a general proof that Time
can be omitted for all materials, effects or inputs. No Time key is omitted.
Only counts and timing metadata are persisted, never effect/shader bytes.

## Results

The established workload remains 288 groups of 25 meshes with fresh poses,
real local D3DX27 Begin/End, exact input validation and aggregate accounting.
Each case uses four warmups and 21 alternating-order samples, each averaging
eight fenced pairs. Material churn remains 30 of 288 groups; clock changes
invalidate every regular group independently of that setting. Process
guards passed before setup and between samples. No Present or game runs.

Median completion savings in milliseconds; negative values are regressions:

| Protocol | Repeat | Native lit | Native shadow | DXVK lit | DXVK shadow |
|---|---:|---:|---:|---:|---:|
| Fixed clock, frame restoration only (control) | 1 | 5.4124 | 5.2091 | 3.4381 | 3.0734 |
| Fixed clock, per-hit restoration | 2 | 4.5401 | 4.3578 | 1.5727 | 0.7020 |
| Fixed clock, per-hit restoration | 3 | 4.6185 | 4.2149 | 1.6329 | 1.2715 |
| Advancing clock, frame restoration only | 1 | 2.0069 | 1.9333 | 0.6093 | 0.7579 |
| Advancing clock, frame restoration only | 2 | 2.1472 | 2.1651 | 0.6649 | 1.0823 |
| Advancing clock, per-hit restoration | 1 | 1.8157 | 1.9042 | -0.8677 | -0.5542 |
| Advancing clock, per-hit restoration | 2 | 1.5024 | 1.9784 | -0.7400 | -0.2578 |

Lit and shadow are separate full workloads and must not be added. Samples
vary substantially: the final DXVK lit repeats have p10/p90 savings of
-1.69/+0.33 ms and -1.62/-0.05 ms; shadow repeats -1.75/-0.19 ms and
-1.57/+0.42 ms. Two repeats are sufficient to reject promotion here, not
to predict the exact performance of a different runtime implementation.

The final state/clock cases each pass 10,309 full boundary comparisons,
including restored binding corruption, and reject the omitted-restore control.
Regular/extra images, fresh-pose controls, zero/partial/full invalidation,
extra-input changes, effect End restoration and logical counts also pass
on native D3D9 and DXVK. Native source callbacks, effect/geometry lifetimes,
engine binding-cache integration and GPU failure behavior remain unproved.

The full `mingw32-make test` suite passes. The earlier retained-batch
fixture also passes again on both backends (lit exact, shadow within its
established depth tolerance). Built beta.3, installed 0.9.10 and installed
INI SHA-256 hashes are unchanged from the previous checkpoint.

Preserved measurements:

- [336 boundary samples](../benchmarks/2026-10-17-material-boundary.csv) and
  [original hashes](../benchmarks/2026-10-17-material-boundary-runs.json).
  These include an explicitly preliminary 84-sample run before the binding
  corruption control; its rebuilt binary hash is unavailable. The report
  uses the two later repeats and their regression control. Those earlier
  source/binary hashes have not been relabeled with the later clock build.
- [336 advancing-clock samples](../benchmarks/2026-10-18-material-clock.csv)
  and [source/binary/log hashes](../benchmarks/2026-10-18-material-clock-runs.json).
  All four clock runs use the same final executable.

Reproduce in `perf/` using the documented toolchain:

```sh
mingw32-make bench-material-queue MATERIAL_CHURN=10 MATERIAL_PROTOCOL=lifecycle-bulk-state
mingw32-make bench-material-queue MATERIAL_CHURN=10 MATERIAL_PROTOCOL=lifecycle-bulk-clock
mingw32-make bench-material-queue MATERIAL_CHURN=10 MATERIAL_PROTOCOL=lifecycle-bulk-state-clock
```

Further native-contract work alone cannot recover the missing gain. Before
investing in runtime integration, a redesigned candidate needs a meaningful
measured gain with changing inputs and a credible state-restoration policy.
Keep native effect/source processing intact when screening simpler bulk
dispatch or redundant state-update work. No new gameplay test is justified
by these results, and 60 FPS remains an unachieved target.
