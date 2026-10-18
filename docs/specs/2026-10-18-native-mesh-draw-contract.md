# Native mesh final-draw contract

An isolated fixture now verifies and executes the native mesh, triangle
wrapper, indexed draw and normal cleanup bodies from both local executables.
It passes **38,016 native executions per image** (19,008 original/candidate
pairs), eleven altered-code/data refusal controls and two broken-gate
controls. The full plugin suite also passes. No production code, install,
INI, stable branch, reference checkout or game process changed.

This supplies a stack-based way to distinguish the leader's final draw
from earlier callback draws, including recursive rendering of the same
mesh. It does not measure FPS or implement the runtime feature. The earlier
[GPU screen](2026-10-18-leader-batch-screen.md) still supplies the only
performance evidence: roughly 2.94-3.12 ms per modeled pair on DXVK.

## Verification boundary

`tools/test_mesh_draw_contract.py` verifies seven complete bodies against
the read-only 1.06 reference. These are research identities, never runtime
installation addresses:

| Body | Reference RVA | Bytes |
|---|---:|---:|
| MeshClass::Render_With_FX_Material | 0x14A880 | 2,482 |
| Draw_Triangles wrapper | 0x120620 | 33 |
| DX8Wrapper::Draw | 0x1203C0 | 452 |
| Geometry End | 0x0B3FD0 | 1 |
| Set skeleton context | 0x14D3E7 | 30 |
| Clear skeleton context | 0x14D405 | 18 |
| Owning method release | 0x07B724 | 16 |

Total: 3,032 code bytes, plus eight verified switch targets. Complete
normalized bodies preserve every branch, stack operand, virtual call slot
and immediate constant. Every external relocation has a checked function
or data role. Global fields retain their relative offsets; mutable regions
must be writable and non-executable, and float literals match exactly.
Switch targets must match the reference and land on instruction boundaries.

Folded tiny helpers and the exception-handler pointer are resolved through
the uniquely matched full mesh body. The empty End and reference-release
bodies are then compared completely. The three-byte empty row constructor
is checked directly. Larger controlled dependencies use unique surrounding
signatures; their implementations are not claimed as fully verified here.

The C harness independently checks loaded body hashes, substitutes
controlled dependencies/data, then makes its private pages execute/read
only. Only numeric relocation metadata and hashes enter the temporary
manifest, which is deleted afterward. No PE entry point, scene, networking,
real GPU or original exception handler executes. No EA bytes are persisted.

## Final-draw identity

The native mesh issues the intended Draw_Triangles call at body offset
`0x92A`. It follows dynamic material update and geometry Begin. After it,
the copied native code executes an empty geometry End, optionally clears
three skeleton-context words, releases its owning method argument and
restores the exception chain and registers.

At the direct indexed-draw call, let `slot` be the original DIP return-slot
pointer and `leader_slot` the recorded mesh-call return-slot pointer:

- `slot + 98 == leader_slot`: a 392-byte distance to this exact invocation.
- `slot[22]` is the mesh's return address after Draw_Triangles (`mesh+0x92F`).
- `slot[15]` is the triangle wrapper's return after Draw (`triangles+0x1D`),
  or the known Draw exit stub when regular-frame timing wraps that return.
- `slot[0]` is `draw+0x18B` without a hook, or `draw+0x18A` after replacing
  the six-byte indirect call with the plugin-style five-byte call and NOP.

These offsets are valid only for the verified complete bodies and calling
path. A runtime implementation must resolve them from verified sites and
check the recorded frame identity before reading deeper stack fields.
They are not permission to install fixed addresses or assume another build
has the same layout.

A recursive call to the same mesh matches both ordinary return addresses
but belongs to a different stack frame. The fixture exercises that case
and rejects a deliberately weakened gate that omits frame identity. A
second broken gate uses the wrong stack distance and fails the positive
deferral checks. Callback draws before the intended final draw are counted
and retained; the candidate declines deferral after any such earlier DIP.

## Executed cases

Each image runs direct calls, the real production general-register entry
stubs, and entry stubs with Draw return wrapping. A small controlled C
dispatcher models the relevant `timing.c` routing and non-nested return
wrapping; this is not a test of its entire accounting implementation.
The actual stub assembly and copied native bodies execute in every hooked
case. Stack cleanup, callee-saved registers and the FS exception chain agree.

Coverage includes rigid/skinned meshes, geometry flags, absent/present
containers and trees, null/self anchors, counts 0/1/2, all four vertex/index
buffer kinds, snapshot validation, null model/material/geometry/method
paths, primary/array FX material slots, drawing disabled, polygon limits,
emissive filtering, inferred vertex counts and method references 1/2.
Controlled material, geometry and state-application callbacks issue nested
draws; a material callback also recursively renders the same mesh.

Original/candidate results compare logical draw arguments/order, controlled
GPU-input records, the full native state/context buffers, dirty flags,
draw/API accounting, callback counts, reference balance and deletion.
Deferred records are emitted only after the native mesh call returns, and
the modeled GPU inputs must still match. Across three modes, each image
exercises 8,784 valid final-frame observations and 1,728 recursive cases
that demonstrate why the weaker return-address-only test is insufficient.

## Remaining integration obligations

This test proves normal native control flow with controlled material,
geometry, render-state, statistics and sorting dependencies. It does not
exercise foreign-anchor/facing math, real effect source computation,
resource lifetime/reset, actual GPU errors or native exception unwinding.
Unexpected helpers/exceptions fail the fixture.

Combine this with the native group and GPU fixtures before any deployment:

1. Run native mesh/group control flow with the real effect and instancing
   path, retaining fresh palettes and full-rate shadow output comparisons.
   Recheck the leader's model/geometry/state before staging it.
2. Require the group eligibility and live diagnostic-callback gates before
   deferring the leader. The native group destroys its mesh event after
   the mesh returns; an active EndEvent callback could issue another draw
   after deferral. The existing bulk proof requires both callback slots null.
3. Preserve existing verification samples. The offline GPU screen disables
   instancing verification; the user's setting is enabled. Groups selected
   for verification must keep the original path without losing the leader.
4. Prove partial-batch, split-group, state restoration and failure handling
   with a deferred leader. A later refusal must not silently discard it.
5. Measure the integrated native/GPU path and all guards before deciding
   whether the approximately 3 ms screen remains sufficient to proceed.

No new gameplay test is requested. Last qualifying gameplay remains about
30.18 presentations/s, and full-rate 60 FPS remains open.

Reproduce from `perf/` with the documented i686 toolchain:

```sh
mingw32-make test-native-mesh
```
