# Shared checked instancing submission

Beta **0.9.11-beta.8** introduces `instsubmit.c/.h`, the production skeletal
batch/replay transaction. The native/GPU recovery adapter now calls the
same `instsubmit.o` linked into the plugin. It no longer contains a second
copy of the state setup, restoration and replay algorithm.

The module is linked but not yet activated by the runtime instancer. The
native group bridge, eligibility, render-abandonment cleanup and persistent
failure policy still need integration. No installation or new performance
measurement is implied. The latest combined offline timing remains
6.17-7.96 ms saved per regular/extra pair; actual gameplay remains about
30.18 presentations/s.

## Submission contract

The caller supplies a descriptor for the already-qualified group and owns
its shader, declaration and stream references for the entire transaction.
The module borrows those objects; it neither owns native mesh/method
references nor mutates effect values. Input stream masks, constant ranges
and palette stride are checked before palette capture. Invalid descriptors
make no GPU writes. Capture occurs before the leader draw is deferred.

Each batch saves the overwritten instance constants, checks setup and draw
results, and restores stream frequencies, binding/offset/stride, declaration,
shader and constants. State writes may be retried once; successful draw
calls are never retried. A null stream's offset/stride is restored through
a live binding before it is unbound, preserving native D3D9 behavior.

If the instanced draw did not commit and restoration succeeded, each staged
bone palette is submitted with the ordinary shader. The original palette
is restored afterward. Pending cancellation uses that same ordinary replay
without another instanced attempt. An API error invalidates the capture
even if recovery succeeded; future optimization requires a new qualified
group and the caller's failure policy.

The result keeps two facts separate:

- `coherent`: all touched device state was restored.
- `complete`: every requested unit was submitted, either in the committed
  instanced draw or in individually counted ordinary replay draws.

A persistent shader restoration failure can return complete but incoherent.
A replay DIP that fails halfway can return coherent but incomplete, with
the successful prefix reported. The caller must require both before treating
the transaction as recovered. The first failed HRESULT and operation are
retained for diagnosis; no process termination, native exception policy,
device reset or silently successful fallback is implemented in this module.

## Test adapter and controls

The existing native adapter still supplies group qualification, final-DIP
eligibility, staged bones and timing/count updates. Its checked path now
constructs the descriptor and delegates to the production object. The
generated copy of our instancer remains necessary for the adapter's two
entry overrides; it no longer provides an alternate checked transaction.

Deliberate missing/duplicate draw and missing restoration controls now live
in the fixture's separate device proxy. The production module contains no
broken-mode switch. Its current operation field supports fault injection
and diagnostics without replacing its algorithm. Those controls demonstrate
that image/state/count assertions detect the corresponding missing device
effects; they are not evidence that an arbitrary lying graphics driver can
be recovered.

`make test-instsubmit` exercises the actual production object against a
deterministic device with nineteen result-contract cases: successful batches,
recovered setup rejection, persistent restoration failure before and after
commit, replay failing after one completed unit, transient/persistent palette
restoration failure, capture refusal, null streams, multiple input streams,
invalid constant/mask bounds, oversized staging and a nested-call refusal.
It distinguishes submitted units from coherent state and checks both.

Both private executable images pass native D3D9/DXVK lit/fresh-shadow
regressions: 384 zero/partial fallback cases and 496 single-GPU-failure cases
across eight workloads. All 56 broken device-effect controls fail their
expected assertions. The full plugin suite passes with beta.8. The normal
combined-insertion benchmark, original fault audit and standalone recovery
targets also compile; these three were not rerun for performance evidence.

Instruction checks of the production module and executed fallback adapter
find no x87/SIMD/FP-control instructions. Results and object/build/source/log
hashes are recorded in the
[evidence manifest](../benchmarks/2026-10-18-checked-submission-core-checks.json).

## Next runtime boundary

Integrate this transaction with the signature-verified group bridge and the
existing instancer. Preserve the ordinary native path for unsupported groups
and verification windows. The caller must resolve partial ordinary replay,
incoherent persistent failures, device loss and interrupted-group ownership
before installation. The offline bridge's ExitProcess path is not a runtime
policy and must not ship.

Read-only reference inspection identifies `WW3DBeginRenderBFME2.cpp` as a
candidate cleanup boundary: its cooperative-level check can enter native
Reset_Device before drawing. This is a source lead only; the corresponding
AotR/ROTWK signatures, calling context and cleanup behavior are not yet
verified. Do not install a hook from that observation alone.
