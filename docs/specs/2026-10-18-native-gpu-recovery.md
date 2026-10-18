# Checked skeletal batch recovery

The offline leader/bulk candidate now recovers all **224 single rejected
batch operations** from the earlier fault audit, plus **eight failures of
the new pre-deferral palette snapshot**. Output/depth, immediate GPU state,
and successful submission counts match the clean reference. Two serial
performance repeats retain **2.90-3.06 ms** of DXVK completion savings per
regular/extra pair, with fresh shadows in the shadow workload.

This repairs the tested failure cases without losing the candidate's
roughly 3 ms gain. It is not an additional 3 ms saving, does not reach the
roughly 8 ms reduction sought, and does not qualify an installed beta.
Last measured gameplay remains about 30.18 presentation calls/s.

## Recovery implementation

`perf/tests/inst_recovery_engine.c` replaces two routines in a build-only
copy of our own instancer. The generator checks unique function-definition
anchors and prints source/output hashes. Production `inst.c` is unchanged;
no runtime signatures or installed hooks are added.

Before suppressing an eligible skeletal leader, the candidate reads the
original shader's active bone registers. Failure disables instancing and
leaves that leader and all subsequent units on the native path. A new
leader capture invalidates the prior snapshot, including when eligibility
later refuses the optimization.

Each batch retains its staged unit count locally until submission or
recovery finishes. Every setup operation is checked; no instanced DIP is
issued after a rejected setup. The original shader, declaration, stream
binding/frequencies and overwritten registers are restored. A rejected
idempotent restoration write is retried once. Errors remain available for
logging after successful restoration; any error disables later instancing.

If no instanced draw succeeded and state restoration succeeded, each staged
bone palette is uploaded to the original shader and drawn once at ordinary
stream frequency. The leader's original palette is restored afterward.
The fallback never changes the D3DX parameter values or calls source
callbacks again. This avoids requiring native mesh objects for replay in
this controlled skeletal path. If the batch succeeded, restoration failure
does not trigger replay of its already submitted units.

The leader/bulk optimization is gated to skeletal groups in this candidate.
Rigid groups and nondeferred legacy instancing retain their existing path;
this work does not establish their failure recovery. All unit and shadow
output remains required for the eventual gameplay target.

## Independent failure checks

The device proxy still injects one `D3DERR_INVALIDCALL` before forwarding
the selected operation. It checks the operation's actual API slot; candidate
operation labels allow early exits, restoration retries and ordinary
fallback calls instead of assuming the old fixed sequence. Batch-entry and
exit callbacks bound independent full vertex-constant and binding snapshots.
Successful DIPs are classified by the actual indexed-stream frequency, so
ordinary fallback calls are distinguished from instanced submission.

For native D3D9 and DXVK, lit and fresh-shadow workloads, both regular and
extra frames, and both the first 15-unit batch and the final 10-unit batch:

- All fourteen original operation positions recover with zero differing
  lit pixels or shadow depths beyond tolerance, zero immediate state-word
  differences, exactly 25 submitted units, and instancing disabled.
- Rejecting the new leader palette snapshot produces 25 native ordinary
  draws with no leader suppression and matching output/state.
- Native reference ownership, scope accounting, stack/register ABI and
  FS chain checks pass. The existing 136 paired joined-native controls and
  actual verification window also pass before each failure run.
- Twelve deliberately broken controls are rejected: corrupt counts,
  omitted fallback draws, and replay of an already committed batch, on each
  backend/workload combination.

Examples: rejecting batch one's DIP now produces 25 ordinary draws, instead
of losing the first 15 units. Rejecting batch two's DIP retains its already
submitted 15 instances and draws only the remaining ten ordinarily.
Rejecting restoration after batch two preserves the 25 submitted instances
and produces no ordinary replay draws.

[224 batch observations](../benchmarks/2026-10-18-native-gpu-recovery-faults.csv)
and [metadata including the eight capture failures](../benchmarks/2026-10-18-native-gpu-recovery-runs.json)
are preserved. The earlier unmodified-code counterexample target remains
separate and continues to detect its known missing-output/state defects.

## Healthy-path measurements

Each run uses four warmups and 21 alternating-order samples, averaging
eight fenced regular/extra pairs per sample. Runs are serial, pass the
game/WorldBuilder process guard, and include all recovery checks and palette
snapshot costs. The fault proxy and readback instrumentation are absent
from the timed executable. Normal images, fresh poses/shadows, native
controls, counts and verification pass on every workload.

| Run | Backend | Lit completion saving | Shadow completion saving |
|---|---|---:|---:|
| 1 | Native D3D9 | 2.522 ms | 2.619 ms |
| 2 | Native D3D9 | 2.615 ms | 2.595 ms |
| 1 | DXVK | 3.060 ms | 2.985 ms |
| 2 | DXVK | 2.900 ms | 2.935 ms |

DXVK completion p10-p90 ranges are 2.665-5.625 / 2.785-3.553 ms in
run 1 and 2.659-3.546 / 2.742-3.167 ms in run 2. The high tails are retained
in the [168 raw samples](../benchmarks/2026-10-18-native-gpu-recovery-timing.csv).
These repeats support retaining the prior approximate saving, not claiming
an improvement from recovery itself. Lit and shadow are alternative full
workloads and must not be added. GPU completion is not presentation cadence.

## Remaining qualification

The test proves recovery from a single pre-forward rejection on an
otherwise working device. It does not prove persistent or multiple errors,
errors after mutation/submission, device loss/reset, or a rejection during
the ordinary replay itself. The candidate forbids replay after persistent
restoration failure, but cannot guarantee a complete/correct frame in that
case. Device-loss semantics remain as documented in the preceding audit.

Native source/state-cache behavior and resource lifetimes, rigid recovery,
native exceptions and general multipass/method/instance paths remain open.
The zero-consumption bridge abort is still a fixture assertion, not a
production fallback. The next integration work must establish native source
and effect/engine-state reconciliation for the staged replay and preserve
failure handling; the larger performance deficit also remains unresolved.

The full plugin suite and original four-backend/workload fault audit pass.
The recovery object uses general registers only. Production sources, built
beta.3, installed 0.9.10 and installed INI hashes are unchanged. Nothing was
installed, no game was launched, and no gameplay test is requested.

Reproduce from `perf/` with the documented toolchain and local assets:

```sh
mingw32-make test-native-gpu-recovery
mingw32-make bench-native-gpu-recovery
mingw32-make test
mingw32-make test-native-gpu-faults
```
