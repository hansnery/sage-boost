# Ordinary rendering after a refused bulk attempt

The offline native group bridge now recovers when a supported-looking bulk
attempt consumes no follower. It replays any unsubmitted staged units with
the original shader, restores the leader's bone constants, cancels the
unused aggregate timing marker and returns the current record to native
rendering. Completed batches are not replayed. The rest of that group uses
the ordinary path; the following group can still use batching.

Both local executable images pass **384 fallback cases** in total on native
D3D9 and DXVK with lit and freshly drawn shadows. The previous 496 single
GPU-failure cases also pass across those image/backend/workload combinations.
All 56 deliberately broken executions are rejected, including missing and
duplicated pending draws and a missing palette restore.

The combined insertion/sort/render benchmark retains **7.20-7.53 ms saved
per regular/extra pair** in two shuffled-input DXVK runs. Ordered input
saves **5.89-6.16 ms** in one sensitivity run. This is a safer candidate
retaining its earlier performance, not an additional saving to add to it.
No production/installation/INI change and no gameplay request. Last measured
gameplay remains about 30.18 presentations/s; the 60 FPS goal is unachieved.

## Failure boundary

The native bridge sits before the per-mesh event and owning method
temporary. Its preflight can succeed even when the later pose gather
cannot stage the current mesh. Previously zero consumption terminated the
offline process. A deferred leader, and possibly a partial batch, could
still need drawing at that point.

The checked skeletal candidate now resolves that pending count before
returning to the original member path. The stored palettes are already
owned by the local staging buffer. Each pending unit receives one ordinary
indexed draw with the captured native draw arguments. The original palette
is restored before any native method/source work resumes. Native effect
parameter values remain untouched throughout this replay.

The ordinary path does not reconstruct source callbacks or call the native
mesh renderer again for already-staged units. Their geometry and material
were accepted by the existing group's instancing checks. The reference
comparison independently renders the complete group through ordinary native
mesh calls with no instancing.

When a partial bulk call consumes records before refusing one, the bridge
advances only past the consumed records. The refused record is revisited;
the zero-consumption fallback then resolves the remaining staged count.
The tested 15-unit batch capacity makes the boundary explicit: failure at
member 15 leaves no pending units after the first committed batch; failure
at member 16 leaves one pending unit and must retain the 15 already drawn.

Aggregate timing cancellation discards only its own attempted interval and
rolls back the single temporary mesh marker. It preserves the surrounding
scope stack, pass, counts and extra-frame mode. Checks include detail on/off,
all passes, regular/extra frames, duplicate cancellation and a full stack.

## Correctness evidence

Each image/backend/workload combination tests 48 cases:

- Regular and extra frames; one group or a following successful group.
- Refusal before the first follower, at member two, before/at/after the
  batch boundary, and at the last member.
- A refusal before writing and one after poisoning part of the next
  uncommitted staging slot. Neither may submit that slot.

The first group's units occupy distinct regions to expose omissions.
Independent API-submission counts check exactly 25 or 50 units, including
the successful following group. Expected committed instance counts come
from the refusal index and batch capacity; the remaining units must use
ordinary draws. Immediate snapshots compare vertex constants, shader,
declaration, stream bindings/frequencies, native fields and effect values.
Pixels/depth satisfy the existing tolerance. Native references, stack and
callee-saved registers, exception-frame chain and logical scope counts
remain intact. Instancing remains available for subsequent groups.

Every combination also reruns the earlier 56 rejected batch operations,
two pre-deferral capture failures and four null-stream restoration failures.
The first four negative controls retain their prior count/recovery/state
checks. Three new controls omit pending replay, duplicate it, or omit the
palette restore; each must fail the specific clean-output/state/count
assertion, rather than merely exit for an unrelated reason.

The joined insertion/GPU regression passes its four backend/workload cases
and twelve record-corruption controls. The complete plugin `make test`
suite passes. Candidate hook/engine objects use general-register flags;
no floating-point control-state changes are introduced.

## Timing

All six timing cases ran serially with the game/WorldBuilder process guard.
Four warmups precede 21 alternating-order samples, each averaging eight
separately fenced pairs. The same 7,200-record insertion/sort/render/erase
workload, advancing material clock and changing shuffle seeds are used.
The refusal injector is absent from this benchmark engine.

| DXVK completion saving | Run 1 | Run 2 |
|---|---:|---:|
| Shuffled, lit | 7.216 ms | 7.273 ms |
| Shuffled, fresh shadows | 7.528 ms | 7.203 ms |
| Ordered, lit | 6.156 ms | — |
| Ordered, fresh shadows | 5.886 ms | — |

Shuffled completion-saving p10-p90 ranges span 6.63-8.93 ms. Input order
in the actual saved game remains unknown. Neither absolute fixture times
nor these differences establish gameplay FPS or presentation pacing.
Preserve [384 fallback observations](../benchmarks/2026-10-18-native-group-fallback-cases.csv),
[126 timing samples](../benchmarks/2026-10-18-native-group-fallback-timing.csv)
and [controls, summaries and hashes](../benchmarks/2026-10-18-native-group-fallback-runs.json).

## Limits and next work

This qualifies successful ordinary replay after a controlled stage refusal.
It does not qualify errors during that replay, persistent GPU errors, device
loss, native exception unwinding or arbitrary source/resource lifetimes.
Unrecoverable replay or corrupt accounting still stops the offline fixture.
The legacy non-recovery fixture retains its prior batch-flush behavior;
the new ordinary cancellation path belongs to the checked skeletal engine.

Next audit the runtime boundaries and implement signature-verified guards
for the qualified rendering paths. Preserve native group ownership and
method/pass boundaries, callback and verification exclusions, and ordinary
rendering for unsupported cases. Resolve device-failure behavior before
promoting the complete candidate to an installed beta. The remaining
engineering budget and final saved-game FPS/pacing still require evidence.

```sh
# From perf/ using the documented i686 toolchain
mingw32-make test-native-group-fallback
mingw32-make test-native-group-fallback AOTR_DAT=C:/Jogos/AgeoftheRing/rotwk/game820.dat
mingw32-make test-native-insert-gpu
mingw32-make test
mingw32-make bench-native-insert-gpu
```
