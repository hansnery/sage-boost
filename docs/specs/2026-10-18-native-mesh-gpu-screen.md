# Native mesh rendering in the leader-batching GPU screen

The guarded native-mesh prototype saves **2.81-2.95 ms per regular/extra
pair on DXVK** across two serial repeats. It preserves the prior screen's
roughly 3 ms saving when the native mesh, triangle, draw and cleanup bodies
replace the modeled mesh callee. This is still short of the roughly 8 ms
combined budget reduction sought in the captured busy scene. No production
source, installed binary, INI, stable branch or reference repository changed.

Last qualifying gameplay remains about **30.18 presentation calls/s**.
These synthetic pair savings cannot be converted directly into gameplay FPS:
the extra-frame scheduler must fit an entire render before the next game
frame. A small saving may leave presentation cadence near 30. No gameplay
test or installation is justified by this result alone.

## Integration and remaining controlled dependencies

The harness loads seven complete bodies from the user's executable into
private RAM, after the existing whole-body, relocation-role and switch/data
checks. The shared loader rechecks numeric lengths/hashes. No native code
or EA assets are persisted. Both local images still pass all 38,016 native
contract executions, eleven refusal controls and two broken-gate controls.
The GPU runs use the AotR image; the verified bodies match game820's hashes.

The native final DIP goes through the actual general-register entry stub
and production timing dispatcher. Draw_Triangles calls the Draw hook, so
regular frames exercise timing.c's real return wrapping; extra frames use
its normal bypass. Native skeleton context setup/clear, geometry End and
method reference release execute around the real local D3DX27 effect and
production instanced draw. The effect lifecycle and advancing Time remain;
there is no persistent material cache or state filter.

The test adapter measures these guards before suppressing the leader:

- Exact mesh-entry stack-slot identity, checked before deeper stack reads.
- Final mesh/triangle/draw return sites, allowing the real Draw exit stub.
- One observed DIP, supported capture and sufficient instance capacity.
- Stable model/base/geometry/bone-list/joint identities, owned method
  reference, and no foreign anchor.
- Both diagnostic callback slots null and the regular verification window
  closed. Open windows retain the original per-member path for sampling.

The enclosing group loop and debug-event object remain modeled. Geometry
binding uses fixed fixture buffers; source computation uploads bones from
the native skeleton context; engine render-state application and statistics
are controlled. Active diagnostic callback pointers are eligibility controls,
not executed callbacks in this fixture. The separate native group/event
tests cover their logical behavior, but have not been joined to this GPU
fixture. **This is not an installable native group/GPU bridge.**

## Correctness controls

Both backend/workload combinations pass regular/extra output checks,
balanced method references, logical marker counts, native context cleanup,
instanced counts, bound shader/declaration/stream checks and frame-boundary
VS/PS constant restoration. Lit images match exactly. The DXVK shadow
comparison has 24 differing bit patterns within the existing depth tolerance;
all 17,428 covered pixels remain present. Every shadow map is cleared and
redrawn, and the fresh-pose control changes the output.

Deliberately omitting each visible leader changes 2,768 lit or 2,516 shadow
pixels across the two frames. This detects a lost leader that repeated poses
could otherwise hide. Normal candidate frames remove 288 separate leader
DIPs, drawing all 25 meshes in each of 288 groups through two batches.

Additional untimed controls compare output and ownership for:

- Either diagnostic callback slot populated: no leader deferral or bulk.
- A model-identity change at member 13: the first bulk call consumes only
  twelve followers; the old batch flushes before the new subgroup's state.
- An earlier native DIP from the dynamic source callback: capture sees
  multiple draws and the original per-member rendering remains intact.
- An immediate real verification window: no leader deferral or bulk,
  nonzero verification samples, zero failed samples and matching output.

Native exception unwinding, foreign-anchor/facing math, source lifetime and
GPU failure recovery remain unproved for the new candidate. In particular,
the ordinary leader must not be lost if a later staged draw fails. Do not
promote this adapter before completing those paths and native group/cache
integration.

## Measurements

Four warmups precede 21 alternating-order samples per case; each sample
averages eight fenced regular/extra pairs. The game/launcher/WorldBuilder
guard passes, and performance runs are serial. Completion includes the
final GPU query wait, not a GPU timestamp or presentation measurement.

| Run | Backend | Lit completion saving | Shadow completion saving |
|---|---|---:|---:|
| 1 | Native D3D9 | 2.549 ms | 2.537 ms |
| 2 | Native D3D9 | 2.566 ms | 2.532 ms |
| 1 | DXVK | 2.810 ms | 2.954 ms |
| 2 | DXVK | 2.899 ms | 2.872 ms |

DXVK p10-p90 ranges are 2.713-3.073 / 2.790-3.140 ms for lit/shadow in
run 1 and 2.712-3.433 / 2.706-3.115 ms in run 2. Lit and shadow are
alternative full workloads: do not add them, or add these results to the
earlier leader/bulk screen. This integration tests that candidate's saving.

[168 final samples](../benchmarks/2026-10-18-native-mesh-gpu.csv) and
[source/executable/log hashes](../benchmarks/2026-10-18-native-mesh-gpu-runs.json)
are preserved. An earlier native-lit run before adding fallback controls is
excluded. Subsequent old-fixture regression timings are also excluded.

## Reproduction and next step

From `perf/`, with the documented i686 toolchain and local assets:

```sh
mingw32-make test-native-mesh
mingw32-make bench-native-leader
```

The Python launcher verifies the supplied image and creates only a temporary
numeric relocation manifest. The adapter and harness are never linked into
the ASI. The full plugin suite and all four previous leader/bulk GPU cases
pass after the shared fixture changes. Built beta.3, installed 0.9.10 and
installed INI hashes remain unchanged.

Next combine the native enclosing group/event/method flow with this native
mesh/GPU path, prove partial/failure recovery, and measure the full bridge.
Further savings are still needed for the user's full-rate 60 FPS target.
