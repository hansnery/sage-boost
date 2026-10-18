# Joined native group and mesh GPU screen

The native group loop, events and method ownership now run together with
the native mesh/draw/cleanup and real effect/GPU fixture. Two serial repeats
save **2.78-2.99 ms per regular/extra pair on DXVK**. This retains the
candidate's roughly 3 ms saving under a stronger integration test. It is
not another additive saving and does not establish the roughly 8 ms budget
reduction sought for sustained full-rate 60 FPS in the user's busy save.

No production source, installed binary, INI, stable branch or reference
repository changed. Last qualifying gameplay remains about 30.18
presentation calls/s. No new gameplay test is requested.

## What executes

The launcher verifies sixteen complete native bodies from the user's local
image: the nine group/SEH/name/method-chain/event bodies and seven
mesh/triangle/draw/context/cleanup bodies, totaling 4,332 bytes. The C
fixture copies them into private RAM and rechecks lengths/hashes against
temporary numeric relocation manifests. No EA executable bytes or assets
are saved. Both local images pass the group verifier and 10,752 logical
contract cases plus refusal and broken-bridge controls. GPU runs use AotR.

Both GPU paths execute the original enclosing group loop, including its
method chain setup/clear, owning temporaries, iteration and normal cleanup.
Native diagnostic events call through the actual timing hooks; the native
mesh uses the real Draw return wrapping and instancing hooks established
by the previous screen. Controlled method virtuals drive the real local
D3DX27 Begin, BeginPass, CommitChanges, EndPass and End separately.

The candidate inserts a general-register bridge at the verified boundary
before constructing a per-mesh event or owning method temporary. After the
first mesh, it batches eligible remaining records with aggregate accounting,
writes the first unconsumed record to the native local, and resumes the
native iterator. A refusal executes the stolen instruction and preserves
the original member path. Eligibility includes one pass, native chunk size
one, one method, matching group/model/base, null callback slots and no
regular verification window. The leader's final-draw guards remain active.

The enclosing group is no longer modeled, but rendering equality, mesh
prepare/finish, source computation, geometry/state application and statistics
remain controlled helpers. The group calls real effect operations through
fixture virtuals rather than the game's entire rendering-method/source
implementation. This is not complete native engine-state integration.

## Correctness and failure boundaries

Every backend/workload passes **136 original/candidate case pairs** plus
an immediate real verification-window comparison. Cases cover regular and
extra frames, all independent BeginEvent/EndEvent callback combinations,
0/1/2/3/25-member ranges, a native group split, a model change inside an
otherwise identical native group, an earlier source DIP and method Begin
failure. Split cases retain all 25 instances; partial bulk consumption
returns to the native member path before establishing the next subgroup.

Callbacks execute the native wrappers and local CRT path. Tests compare
callback counts, label hashes and observed native draw-attempt counts, not
just the presence of callback pointers. Populated callback slots preserve
the original member events and prevent leader deferral and bulk dispatch.
Begin failure preserves the native skipped End/finish path. Native stack,
callee-saved registers, FS chain, cleanup state, reference balance, skeleton
context clearing, scope counts and image output are checked.

Normal full workloads also pass regular/extra image comparisons, fresh-pose
and deliberately omitted-leader controls, instanced counts, frame state and
constant restoration. Lit output is exact; fresh shadow depths remain within
the established tolerance. The first leader is visibly offset so omitting
it cannot hide behind a repeated source pose. Source poses otherwise repeat;
logical instance counts and the separate native logical-order tests provide
additional coverage rather than claiming every mesh has a unique pixel.

**GPU failure recovery remains unproved.** The bridge aborts this offline
fixture if a preflight-approved bulk call consumes zero records. It does
not pretend that abort is a production fallback. A failure after suppressing
the leader or partially submitting a batch must not lose or duplicate that
leader or its followers. Source/geometry/engine-cache lifetimes, native
exception unwinding, and multipass/multi-method/native-multi-instance GPU
integration also remain open. Separate logical contract tests do not close
those GPU/runtime requirements.

## Measurements

Four warmups precede 21 alternating-order samples per case, each averaging
eight fenced regular/extra pairs. All qualified runs are serial and pass
the game/launcher/WorldBuilder guard. Completion includes the final GPU
query wait; it does not measure presentation cadence.

| Run | Backend | Lit completion saving | Shadow completion saving |
|---|---|---:|---:|
| 1 | Native D3D9 | 2.638 ms | 2.643 ms |
| 2 | Native D3D9 | 2.545 ms | 2.542 ms |
| 1 | DXVK | 2.777 ms | 2.963 ms |
| 2 | DXVK | 2.986 ms | 2.939 ms |

DXVK p10-p90 ranges are 2.674-2.994 / 2.832-3.153 ms for lit/shadow in
run 1, and 2.754-3.343 / 2.844-3.181 ms in run 2. Lit and shadow are
alternative full workloads and must not be added. Time advances between
pairs, poses are fresh, and every shadow map is cleared and redrawn.

[168 final samples](../benchmarks/2026-10-18-native-group-gpu.csv) and
[source/executable/log metadata](../benchmarks/2026-10-18-native-group-gpu-runs.json)
are preserved. The first exploratory integration attempt failed a control
before producing any timing sample. Subsequent mesh-only regression timings
are excluded from this performance result.

The full plugin suite and all four earlier mesh/GPU cases pass after the
shared fixture changes. Disassembly confirms the bridge object uses no
SSE/MMX or floating-point control-word instructions. Built beta.3, installed
0.9.10 and installed INI hashes remain unchanged.

Reproduce from `perf/` with the documented i686 toolchain and local assets:

```sh
mingw32-make test-native-group
mingw32-make bench-native-group-gpu
```

Next isolate GPU-operation failures in this joined path and prove recovery,
then qualify the remaining source/state boundaries. More saving is still
needed before a gameplay test is warranted; these passing native contracts
do not establish 60 FPS or justify installation on their own.
