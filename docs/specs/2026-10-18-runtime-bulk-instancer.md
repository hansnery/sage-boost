# Bulk operations in the production instancer

Beta **0.9.11-beta.9** moves the native leader gate, skeletal palette capture,
checked batch submission and bulk record loop into `inst.c`. `instbulk.h`
defines configuration, qualification, advancement/cancellation and outcome
interfaces for the forthcoming native group hook. Normal GPU fixtures now
delegate to this production implementation rather than their own bulk loop.

The runtime feature remains **off**: startup does not yet configure it or
install the native group hook. The ASI includes the implementation and uses
the ordinary instancing path until that integration is complete. Nothing
was installed, and no game/WorldBuilder was launched. This change provides
no new timing or gameplay FPS result; last gameplay remains about 30.18
presentations/s.

## Eligibility and ownership

Configuration takes mesh/draw body locations and event callback slots from
the verified native contract. It does not discover or patch an executable.
Only skeletal groups with an accepted capture qualify. Native begin/end
callbacks must be absent, and regular-frame verification windows retain
the original path. The final DIP must have the expected native frame
identity and return locations before any deeper stack read occurs.
Geometry, bones, joint count, model, base vertex and method ownership must
agree with the captured leader. The caller must use the accepted render
thread and preserve the native group/frame context.

Before suppressing that final DIP, the instancer captures the original
palette through `instsubmit.o` and stages the leader. The ordinary native
mesh function still completes its state cleanup and method-reference
release. Subsequent bulk records borrow the enclosing group's method;
they do not manufacture and release per-mesh temporary references.
Pending units flush at capacity, model boundaries or the group end.

The captured stream reference remains held through submission/restoration.
Normal completion and successful cancellation release it. An incomplete or
incoherent bulk transaction is reported to its caller and cannot be claimed
as a recovered success. The API returns accepted record count separately
from the most recent submission result; the latter is not a total unit
count for the whole group. Unsupported staging can leave pending units for
ordinary replay or for the normal member path to flush.

Per-mesh state at offset 0x310 is excluded from bulk rendering. That exclusion
also applies to the ordinary member/leader handler while bulk is enabled.
Refusing only the bulk bridge would still let that handler skip the mesh.
The ownership fixture now includes a twelfth case proving that pending
units flush, references balance and the unsupported mesh remains native.

## Test integration

`inst_recovery_engine.c` now delegates leader qualification, bulk processing
and cancellation to production functions. Link-time wrappers observe the
actual `pf_submit_batch`/`pf_submit_pending` calls for immediate device-state
assertions; they do not implement a second submission algorithm. The
production ASI has no such wrappers or test corruption switches.

The normal combined queue/GPU fixture compiles unmodified `inst.c` with
production flags. The dedicated stage-refusal fixture still uses an ignored
copy of our source with only the stage entry renamed, allowing controlled
refusal/partial-write injection before calling the original stage body.
`make_inst_recovery_source.py` no longer replaces `draw_batch`. Device-effect
corruption and the missing-leader control remain in test adapters only.

Native callbacks, split/partial groups, earlier source draws, verification
windows, references, scope accounting and fresh lit/shadow output are checked
against the original path. The fault cases require recovered unit counts,
matching output and immediate device/effect/native-field state. Combined
queue checks also retain exact record/tie order and owned references.
Final results and source/build/log hashes are recorded in the
[evidence manifest](../benchmarks/2026-10-18-runtime-bulk-instancer-checks.json).

The final checks pass on both executable families: 384 fallback cases,
496 injected GPU failures and 56 broken device-effect controls, plus eight
combined queue/GPU workloads and 24 broken-record controls. Twelve ownership
cases, 19 submission result cases and the full plugin suite pass. Production
and executed adapter objects pass the general-register instruction check.
No fresh timing was collected after this integration; the older combined
saving remains engineering evidence for the candidate, not a measurement
of this build or a gameplay FPS prediction.

## Remaining activation work

Startup must discover the complete contract before existing hooks alter its
bodies, install the guarded group bridge, and enable aggregate timing with
correct normal/extra/detail accounting. The caller must handle incomplete
ordinary replay, incoherent persistent failure, nested/abandoned rendering,
device Reset and native exceptions. The current offline bridge's ExitProcess
failure policy is still test-only and must not become runtime behavior.

Normal resource ownership is covered; this does not prove arbitrary material
source side effects or every dynamic lifetime. Configuration and the caller's
context guards must remain restricted to the qualified native path. Measure
the complete active hook before installing a substantive candidate and
requesting the user's saved-game test. No camera, logic, input, network,
frame-counter or floating-point control changes are introduced here.
