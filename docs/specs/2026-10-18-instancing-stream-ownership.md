# Captured instancing stream ownership

Beta **0.9.11-beta.7** retains the COM reference returned by
`GetStreamSource` until the captured group has finished using it. Previously
`capture()` saved the pointer and immediately released that reference.
When the device held the only remaining reference, temporarily binding the
instance buffer could destroy the saved buffer before restoration.

This is a correctness prerequisite for bulk rendering, not a new measured
performance improvement. No game or WorldBuilder was launched, and the
installed ASI/INI remain unchanged. Last gameplay remains about 30.18
presentations/s. The latest combined offline savings remain 6.17-7.96 ms per
regular/extra pair; those are not an FPS prediction.

## Ownership changes

The getter now runs after the shader, declaration and capacity refusal
gates. Its successful reference transfers to `g_stream_owner`; there is
no extra per-draw getter or AddRef. Release moves from immediate capture
to the terminal group transition, after normal submission/restoration.
The failed-getter path clears the saved pointer and releases any returned
reference. A null stream requires no reference.

Completion, rejected captures, model/base-vertex splits, unsupported tails,
group replacement, device replacement and initialization release the owner.
Offline bulk completion and pending-draw cancellation use the same release
helper after resolving their pending work. The ownership token is separate
from `group` so retained test snapshots cannot duplicate it by copying the
structure. The original bound buffer remains owned across consecutive
batches within one group.

## Checks

The deterministic COM fixture first reproduced the old bug: the restore
attempt reached a destroyed buffer. Tombstones record that error without
freeing the test object, so failure comes from the intended assertion.
The fixed implementation passes eleven cases covering consecutive batches,
multiple leader draws, getter/shader/capacity rejection, disabled fallback,
model splits with short and supported tails, replacement, normal completion
and a null stream. References and bindings balance without dead-object use.
This test is part of `make test` and can run as `make test-inst-ownership`.

`make test-inst-ownership-gpu` exercises the production object on native
D3D9 and DXVK. A private IUnknown attached to a real stream buffer reports
destruction without dereferencing freed memory. The creator releases its
reference after leader capture. Completed and unsupported groups release
normally; only the deliberately unfinished group uses initialization to
close its owner. Final unbinding destroys the resource in all three cases
per backend. Two consecutive supported groups preserve the original buffer,
offset and stride and produce pixels identical to the ordinary lit draw.

Both AotR and ROTWK private-image native group/fallback checks also pass
with the ownership changes, on both backends for lit and fresh-shadow
workloads. Across the eight combinations, 384 zero/partial fallback cases
and 496 single-GPU-failure cases preserve output/state/unit counts. All 56
deliberately broken controls fail their intended checks. These are the
existing fallback regressions, not additional FPS measurements.

The full plugin suite passes with beta.7. The production instancing and
offline recovery objects contain no x87/SIMD/FP-control instructions.
Build/source/log hashes and check summaries are recorded in the
[evidence manifest](../benchmarks/2026-10-18-instancing-stream-ownership-checks.json).

## Remaining integration work

The checked submission/replay engine and bulk bridge still run only in
offline fixtures. Production `draw_batch()` does not yet implement their
checked state restoration and uncommitted-draw replay. Persistent failures,
device loss, errors during ordinary replay and native exceptions remain
open. In particular, an interrupted group can retain a default-pool buffer
until a later group or initialization; render-abandonment/Reset cleanup
needs an explicit policy before the complete bulk candidate is installed.

This change does not establish all source, shader, geometry or material
lifetimes. No camera, logic, input, network, frame-counter, x87-control or
MXCSR changes are introduced. Continue production bulk integration before
asking for the user's next saved-game test.
