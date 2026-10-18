# Native insertion, sorting and GPU screen

Direct insertion into available queue storage removes the native temporary
record construction, copying and destruction. The combined insertion,
sorting and rendering candidate saves **7.14-7.32 ms per regular/extra pair
on DXVK** in two serial shuffled-input runs. This replaces the previous
queue/GPU screen, rather than adding an isolated insertion result to it.

This remains an offline result. The engineering target is roughly 8 ms;
actual queue order and complete saved-game behavior remain unmeasured.
Last gameplay remains about 30.18 presentations/s. No production source,
installed binary, INI or stable branch changes; no gameplay test requested.

## Native contract and candidate

The verifier follows the full insertion dependency closure from a unique
full-body wildcard match in each local image: 33 bodies, 2,148 bytes.
It checks instruction streams, branch boundaries, relocation roles and
dependency section roles before producing a numeric/hash-only manifest.
Five altered contracts are refused: method limit, mesh-model addressing,
capacity branch, record-field assignment and allocation element size.
Native bytes remain only in private process memory, with target-relative
function spacing and executable protection after relocation.

The actual native task insertion selects among three vectors from the
mesh's model and geometry. The reference source's inferred "light
environment" label for mesh+0xC4 is misleading; the verified plugin layout
identifies that field as the model, and the 0x400 flag selects skinning.
Each record owns a mesh, a method count and six method holders.

The candidate only appends directly when the method count is at most six,
the selected vector has a complete unused slot, and the method vector or
its elements do not overlap that slot. It initializes all six holders,
preserves method order, retains each referenced object and consumes the
caller's by-value mesh ownership. Other cases call the original native
function, including full capacity and oversized method stacks.

Native vector growth, erase and record copy/destruction execute in the
CPU fixture. Only the allocation/free boundary uses controlled, guarded
heap callbacks. This proves normal successful growth semantics in the
fixture, not the game's heap implementation or allocation-failure handling.
Native exception metadata and handlers remain fail-stop dependencies.

## Correctness checks

Both local images pass 576 CPU comparisons each, combining eight insertion
counts (0 through 7,680), three initial capacities, zero through seven
methods and distinct/duplicate/nullable method patterns. Checks cover:

- Exact record contents, queue selection/order, sizes and capacities.
- Mesh/method reference counts, immutable model/source fields and mesh bytes.
- Guarded allocations, untouched spare storage and balanced native cleanup.
- Stack balance, callee-saved registers and the native exception-frame chain.
- Dropping oversized stacks, including deletion of the last mesh holder;
  nullable dropped input; and destination-alias fallback behavior.

Each image also rejects a deliberately corrupted inserted record through
the exact-record oracle. Candidate code compiles with general registers
only; it does not change floating-point control state.

The GPU fixture now calls actual native task insertion for each of its
7,200 meshes and native vector erase after rendering. Every pass starts
with available storage. The three aggregate pass ranges remain
2,900/1,400/2,900 records, and the native selector chooses the fixture's
skinned queue within each pass. These are not three different internal
rendering paths; the CPU fixture independently covers all selector branches.

Both images pass native D3D9 and DXVK lit/fresh-shadow cases: eight cases,
each with four input orders and regular/extra output comparisons. Exact
sorted records/ties, ordered groups, references, guards and object/engine/
effect snapshots match; pixels/depth meet the existing tolerance. The
earlier 136 group comparison pairs run in each case. Twenty-four deliberate
failures are rejected by the specific record oracle: corrupted sort copies,
duplicated queued meshes and corrupted inserted method slots.

## Measurements

All runs are serial and enforce the game/WorldBuilder process guard.
CPU measurements use four warmups, 21 alternating-order samples and eight
7,680-record insertion/erase workloads per sample. The initially empty
case grows on its first workload and then reuses that storage.

GPU measurements use four warmups, 21 alternating-order samples and eight
separately fenced regular/extra pairs per sample. Material time and shuffle
seeds advance identically for both versions. Timed work includes actual
insertion, sorting, group/mesh/FX rendering and native erase; expensive
record/object snapshot comparisons stay outside timing.

The CPU screen saves 0.80-0.86 ms per 7,680-record workload across the
tested method counts; one-method cases save 0.81-0.82 ms. These isolated
times are not additional GPU savings.

Completion savings in milliseconds:

| Combined shuffled-input case | Run 1 | Run 2 |
|---|---:|---:|
| Native D3D9, lit | 8.326 | 7.105 |
| Native D3D9, fresh shadows | 7.003 | 6.990 |
| DXVK, lit | 7.322 | 7.295 |
| DXVK, fresh shadows | 7.137 | 7.181 |

Insertion-only DXVK comparisons save 1.49-1.94 ms per pair. A separate
already-ordered sensitivity run saves 6.206 ms lit and 5.985 ms with fresh
shadows for the combined candidate. Do not assume the saved game's queue
order matches the shuffled case. The shuffled DXVK completion-saving
p10-p90 ranges span 6.43-8.88 ms across the four final cases.

Preserve [294 GPU timing samples](../benchmarks/2026-10-18-native-insertion-gpu-timing.csv),
[252 CPU timing samples](../benchmarks/2026-10-18-native-insertion-cpu-timing.csv)
and [summaries, controls and hashes](../benchmarks/2026-10-18-native-insertion-gpu-runs.json).
The earlier joined queue/GPU controls and complete plugin `make test`
suite pass after the shared fixture changes.

The first native D3D9 lit run is visibly more variable; the installed
backend is DXVK. Lit and shadow cases are alternative full workloads, not
additive savings. Absolute fixture completion time is not a gameplay FPS
measurement. Input-order sensitivity and isolation results accompany the
preserved timing data.

## Remaining runtime work

This closes normal insertion, spare capacity, native growth fallback and
erase in controlled fixtures. It does not qualify the entire combined
candidate for installation. Source computation, ready-asset adapters,
geometry helpers and long-lived resources remain controlled. Whole-queue
GPU fault injection, persistent/device-loss failures, exception handling
and production context guards are still open.

The group bridge still exits the offline process if bulk handling consumes
zero records after preflight. Replace that assertion with a proven ordinary
rendering fallback which resolves any deferred leader exactly once and
preserves engine/effect/device state. Retain the existing verification and
callback exclusions. Then qualify the signatures, source/lifetime limits
and guarded runtime path before requesting gameplay.

```sh
# From perf/ using the documented i686 toolchain
mingw32-make test-native-insert
mingw32-make bench-native-insert
mingw32-make test-native-insert-gpu
mingw32-make test-native-insert-gpu AOTR_DAT=C:/Jogos/AgeoftheRing/rotwk/game820.dat
mingw32-make bench-native-insert-gpu
mingw32-make bench-native-insert-gpu INSERT_MODE=insert
mingw32-make bench-native-insert-gpu INSERT_ORDER=ascending
```
