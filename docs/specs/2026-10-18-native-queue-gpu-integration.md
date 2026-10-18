# Owned queue, sorting and native GPU integration

The combined offline candidate saves **5.42-5.84 ms per regular/extra pair
on DXVK** in two serial runs with shuffled input. A separate already-ordered
input run saves **4.48-4.51 ms**. These results replace arithmetic additions
of the earlier isolated sorting and rendering screens: the same owned
records now pass through sorting and rendering in one timed workload.

This is meaningful progress, but remains below the roughly 8 ms engineering
budget. It does not establish an installed speedup or 60 FPS. Last measured
gameplay remains about 30.18 presentations/s. Production, installed ASI,
installed INI and stable branches are unchanged; no gameplay test requested.

## What is joined

The fixture collects 7,200 records using the verified native owning-record
copy constructor. It sorts ranges of 2,900, 1,400 and 2,900 records, passes
those exact ranges to the native group/mesh/FX rendering path, and releases
the records through the native destructor. Both reference and candidate pay
these collection/destruction costs inside the measured frame.

There are 288 distinct fixture model identities, 25 meshes per model. The
native group loop discovers their boundaries from the sorted records; the
fixture does not hand it one pre-separated group per call. The controlled
prepare callback selects the corresponding material inputs. All meshes
still use the fixture geometry and six-bone palettes, refreshed for both
regular and extra frames. The shadow workload redraws its map each frame.

The 38 sort/ownership bodies join the previous 25 rendering bodies. Two
helper identities overlap, yielding 61 distinct verified bodies and 8,114
bytes. Bodies are read from local images into private RAM, never persisted
or distributed. The game entry point and scene loop never execute.

The rendering bodies now share a private arena at their target-relative
positions, with nonoverlap/bounds checks and a separate generated switch
table. The group hook copy remains separate. Each sort version retains its
own target-relative placement. Default loader behavior for the older
fixtures is unchanged. This avoids the earlier one-page-per-function layout
as an uncontrolled cost in the new rendering comparison.

**Scope:** these three ranges model the aggregate pass workload. They are
not a reproduction of all three different internal FX queue paths. Queue
collection uses native record copying, not `Add_Rendering_Task`, vector
growth or the native vector erase wrapper. Source computations, ready
assets, geometry helpers and long-lived model resources remain controlled.

## Correctness evidence

Both local executable images pass on native D3D9 and DXVK, with lit and
fresh-shadow workloads: eight image/backend/workload combinations.
Each combination preserves:

- Exact sorted record bytes and native tie permutation for ascending,
  descending, shuffled and interleaved input, before and after rendering.
- Eight regular/extra image comparisons across those four orders, with
  unchanged mesh/model/container/tree hashes and native engine/effect state.
- Three complete owned ranges, 288 groups in the expected order, mesh and
  method reference counts, surrounding guards and native ABI/FS chains.
- The earlier 136 native group comparison pairs, callback observations,
  empty/small groups, split/partial batches, earlier draws, Begin failure,
  the real verification window and missing-leader image control.

Sixteen deliberately broken queue executions are rejected. One corrupts
temporary raw copies; the other duplicates a queued mesh after sorting.
The launcher requires the exact-record comparison to fail, rather than
accepting an arbitrary error as detection. The corrupt-copy control fails
before rendering; the duplicate control is checked again before releasing
the queued ownership.

The original 936-case native sort checks still pass on both local images,
including altered-code and corrupt-copy controls. The previous 248 native
FX/GPU recovery cases and their broken controls also pass with the default
loaders. The complete plugin `make test` suite passes.

Whole-queue GPU fault injection is not added here. The ordinary frame
state-restoration checks and new engine/effect snapshots do not replace
the earlier immediate-state failure audit. Exception/termination paths
remain fail-stop, and sources/resources remain fixture-owned. Runtime
context guards, arbitrary lifetimes, persistent/device-loss/rigid failure
paths and zero-consumption bulk fallback still need qualification.

## Final timings

Four warmups precede 21 alternating-order samples per case, each averaging
eight separately fenced regular/extra pairs. Material time advances and
queue shuffle seeds change between repetitions; both versions receive the
same inputs. Full record/object snapshot checks are outside timing; queue
collection, sorting, native rendering, destruction and normal counts remain
inside. All performance runs are serial and pass the game/WorldBuilder
process guard.

Completion savings below are median paired differences in milliseconds:

| Configuration | Lit run 1 / 2 | Fresh shadow run 1 / 2 |
|---|---:|---:|
| Combined, native D3D9, shuffled | 4.920 / 5.011 | 5.019 / 5.184 |
| Combined, DXVK, shuffled | 5.425 / 5.614 | 5.419 / 5.838 |
| Sorting only, DXVK, shuffled | 2.818 / 2.771 | 2.640 / 2.744 |
| Rendering only, DXVK, shuffled | 2.829 / 2.653 | 2.843 / 2.837 |
| Combined, DXVK, ascending (one run) | 4.484 | 4.508 |

The combined DXVK completion p10-p90 ranges are 4.917-6.478 / 4.848-6.172 ms
in run 1, and 4.733-6.164 / 4.525-7.939 ms in run 2. Preserve all
[378 timing samples](../benchmarks/2026-10-18-native-queue-gpu-timing.csv)
and [18 summaries, controls, limitations and hashes](../benchmarks/2026-10-18-native-queue-gpu-runs.json).

Lit and shadow are alternative full workloads, not additive pass savings.
Neither the actual saved-game queue distribution nor the complete scene
is reproduced. Do not add these savings to prior screens, convert the
fixture's absolute time to FPS, or infer presentation pacing from its fence.

## Next action

Screen native queue insertion with available vector capacity. Source and
call-site inspection show a default temporary record, six holder
initializations, by-value resize/fill work and temporary destruction for
each queued mesh. A direct fill could avoid some of that work while keeping
ownership, queue selection and growth fallback intact; no gain is measured
yet, and the full dependency contract must be verified before execution.

Research boundaries: the 249-byte insertion body has a unique full-length
wildcard match at AotR RVA `0x17430D`, SHA256
`a3048394e04d6b8a2d8078cf821f7b6fc453e8991ec44323ea7d5a57853839f7`.
Its 35-byte default-resize wrapper matches at `0x1742EA`, SHA256
`dd85184f4cef26d5cee1be7fd7b89c947990674c9b1919e155a6481f78af0dc7`.
These are offline research identities, never installed addresses.

```sh
# From perf/ with the documented i686 toolchain
mingw32-make test-native-queue-gpu
mingw32-make test-native-queue-gpu AOTR_DAT=C:/Jogos/AgeoftheRing/rotwk/game820.dat
mingw32-make bench-native-queue-gpu
mingw32-make bench-native-queue-gpu QUEUE_MODE=sort
mingw32-make bench-native-queue-gpu QUEUE_MODE=render
mingw32-make bench-native-queue-gpu QUEUE_ORDER=ascending
```
