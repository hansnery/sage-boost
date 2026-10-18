# Native material lifecycle and pending-state integration

The offline leader/bulk recovery candidate now runs through nine additional
verified native functions: the material/effect lifecycle, source binder and
walker, and pending render-state application. The joined fixture executes
25 verified bodies (5,648 bytes) in private RAM with the local D3DX27 effect
and real GPU. Native D3D9/DXVK lit and fresh-shadow controls pass.

The stronger integration exposed an unbound-stream offset leak in the
unchanged instancer. The checked candidate now restores that offset too.
Its expanded recovery tests pass **248 single rejected operations**, with
matching pixels/depth, submitted-unit counts, GPU state, effect parameter
values and the fixture's native engine fields. Two final serial repeats
save **2.68-2.93 ms** per regular/extra pair on DXVK.

This is a stronger integration result for the existing candidate, not an
additional saving or a 60 FPS result. The roughly 8 ms reduction sought is
still unmet; last measured gameplay remains about 30.18 presentations/s.

## What now runs natively

The eight previously verified FX wrappers perform effect lookup, Begin,
BeginPass, Dynamic, EndPass, End, asset binding and six-channel source
selection. Their calls reach the live local effect interface. Native Begin
selects its technique, applies its parameter block, binds the sources,
calls D3DX Begin with its own flags and records success. Native Dynamic
applies pending state, binds channel one and commits the effect changes.
The fixture counts these operations independently per complete frame.

The complete 746-byte pending-state function now runs from both the native
FX wrappers and native Draw. The fixture supplies buffer objects matching
the verified field layout. Each group dirties vertex/index bindings; the
native code binds the real vertex/index buffers, clears the corresponding
flags and updates the original counters. Additional controls seed wrong
device bindings and check the resulting bindings, exact dirty-mask value,
counter deltas and clean no-op behavior under both fixed-function skip
modes. Fixed-function shader/material/light/texture branches are not
exercised by these GPU cases.

`tools/native_fx_state_contract.py` verifies complete normalized bodies,
direct dependencies and eleven distinct mutable global regions. Both local
images pass, including five altered-body/dependency refusal controls each.
The loader rechecks lengths/hashes before copying to private writable
memory, relocates only verified fields, and seals each page execute/read.
No native code or EA asset is written to disk; manifests contain numbers
and hashes. The game's entry point and loop never execute.

Material input computation remains controlled: channel zero sets the
fixture's material values and channel one supplies its current bone palette.
The source walker and surrounding native lifecycle execute, but the source
callback bodies, ready-asset adapter and lifetime policy are not native.
The texture-holder list is empty here; texture values come from the
controlled source. Geometry, group equality/prepare/finish and much of the
scene remain fixture objects. This does not establish complete engine
integration or coverage of all shaders, bone counts or rendering methods.

## State leak found and corrected in the candidate

The clean legacy-instancing reference matched pixels but left unbound
stream one's offset changed from zero to 3,080 after its first batch on
native D3D9. Returning a stream to a null buffer does not reset its offset
and stride. Wine's native-conformance tests also explicitly check this
behavior. [D3D9 stream tests](https://github.com/wine-mirror/wine/blob/master/dlls/d3d9/tests/device.c).

The recovery candidate restores the previous offset/stride through its live
instance buffer before unbinding it. Both operations use checked,
idempotent restoration; a one-shot rejection of the new preliminary write
is retried and disables later instancing. No draw occurs between these
restoration operations. Persistent errors and arbitrary previously retained
offsets beyond the fixture's ranges remain unqualified.

The failure-test image reference now uses **25 ordinary native mesh draws**
with plugin instancing disabled. It no longer relies on the legacy
instancer as an exact-state reference. The timed baseline still uses the
unchanged instancer, so the measured optimization comparison is preserved.
The legacy offset counterexample is retained in the run metadata. No
snapshot fields were normalized away to hide the difference.

## Expanded recovery evidence

For four backend/workload combinations, each regular and extra mode tests
fourteen original failure positions in both batches, the new preliminary
null-stream restore in both batches, and the pre-deferral bone capture.
That is 240 batch faults plus eight capture failures. Each injection returns
`D3DERR_INVALIDCALL` before forwarding one selected call.

Immediate snapshots now include the native render-state structure, SAS
context, method fields/reference count, dirty mask and skip flag, alongside
the device bindings/frequencies and all vertex constants. A separate
read-only effect query captures the fixture's material-key values and full
bone parameter array. Every tested recovery preserves these snapshots,
renders all 25 units exactly once, and disables later instancing. The native
group ABI/FS chain, method ownership and scope checks also pass.

Sixteen deliberately broken executions are rejected: corrupt counts,
missing replay, duplicate committed replay and omission of the null-stream
offset restoration, on each backend/workload. Each normal run also passes
the existing 136 native group comparison pairs and real verification window,
including callback observations, splits/partial batches, earlier DIPs and
Begin failure.

The synthetic earlier-DIP control initially used constants left by a prior
case because it drew before Dynamic's trailing CommitChanges. It now
explicitly commits its own palette before that deliberately inserted draw,
as the preceding fixture did. The normal source path uses only the native
trailing commit. The optimization still must refuse groups with that earlier
draw; no expected image differences were waived.

[240 batch observations](../benchmarks/2026-10-18-native-fx-gpu-faults.csv)
and [metadata with capture failures, hashes and counterexample](../benchmarks/2026-10-18-native-fx-gpu-runs.json)
are preserved. Fields not emitted for the additional null-restore cases are
blank rather than inferred in the CSV. These tests do not prove failures
after mutation/submission, multiple or persistent failures, device loss/reset,
or recovery from an error during ordinary fallback drawing.

## Final measurements and remaining work

Four warmups precede 21 alternating-order samples per case, each averaging
eight fenced regular/extra pairs. The two final runs are serial and pass
the game/WorldBuilder process guard. They include all native lifecycle/state
work and the offset-restoration fix; fault snapshots are absent from timing.

| Run | Backend | Lit completion saving | Shadow completion saving |
|---|---|---:|---:|
| 1 | Native D3D9 | 2.502 ms | 2.630 ms |
| 2 | Native D3D9 | 2.669 ms | 2.430 ms |
| 1 | DXVK | 2.680 ms | 2.932 ms |
| 2 | DXVK | 2.738 ms | 2.795 ms |

DXVK completion p10-p90 ranges are 2.520-2.968 / 2.692-4.203 ms for run 1
and 2.612-2.885 / 2.613-3.124 ms for run 2. All
[168 timing samples](../benchmarks/2026-10-18-native-fx-gpu-timing.csv) are
retained. Lit and shadow are alternative full workloads, not additive pass
savings. Completion does not measure presentation pacing. Earlier timings
before the offset fix and incomplete/debug runs are excluded.

The native contracts, full plugin suite and preceding four-case GPU recovery
regression pass. Production sources, built beta.3, installed 0.9.10, user INI
and stable branches are unchanged. Nothing was installed or launched.

Next investigate native source computation and remaining expensive render
work, including whether sources unused by a selected technique consume
avoidable CPU time. Full lifetime, source side-effect, nondeferred/rigid/
verification recovery and native exception behavior remain open. The bridge's
zero-consumption abort remains an offline assertion. More performance gain
is required before another gameplay request; the target remains sustained
approximately 60 FPS with fresh full-rate shadows and unchanged logic/input/
network/camera behavior and graphics quality.

```sh
mingw32-make test-native-fx-state
mingw32-make test-native-fx-recovery
mingw32-make bench-native-fx-gpu
mingw32-make test
mingw32-make test-native-gpu-recovery
```
