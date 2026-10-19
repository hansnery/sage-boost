# Complete render batches on one worker

The external DLL screen preserves output but does not establish a substantial
gain. Shader/constant queries after queued effect work force synchronization.
Screen a larger independently written boundary: immutable skeletal/material
packets consumed entirely by one rendering worker. This is an offline
experiment under the continuing development authorization, not a live hook.

## Choice and isolation

Moving the live engine display callback onto another thread would expose
mutable logic, camera and resource state concurrently; do not do that.
Reimplementing a full effect-state mirror before measuring a larger boundary
would also repeat the uncertain per-call integration. Start with owned
packets and the existing actual-effect/geometry fixture instead.

The worker creates its own hidden window, D3D9 device, effects and resources,
and performs every GPU call. The producer never calls D3D or passes a live
engine pointer. This respects the documented [device/window thread
requirement](https://learn.microsoft.com/en-us/windows/win32/direct3d9/creating-a-device)
without enabling D3DCREATE_MULTITHREADED. The live game's existing device and
window ownership are separate integration work; this is not their solution.

Each packet contains copied skeletal palettes for 7,200 fixture meshes plus
material generation, clock, pose selection and a monotonic sequence. The
fixture's immutable geometry and resources remain owned by the worker.
Use two bounded slots with explicit ownership transitions and completion
events. No slot may be reused while queued/running, and completion means
the worker drained its GPU work. Check payload integrity and strict sequence
order; retain hashes/counts only, not shader bytes or images.

## Evidence and measurement

Capture unqueued reference output on the worker using the existing pose and
material paths. Then compare copied-packet output exactly for original pose,
fresh pose and changed material, including post-frame state. Deliberately
overwrite the producer's scratch storage after publication; the queued
packet must still match. Stale palettes and altered material inputs must
produce observable differences. Require nonempty lit/shadow output.

Compare a direct no-palette-copy fixture, copied packets with immediate
wait, and a two-slot copied-packet pipeline. All render on the same worker;
the direct mode uses the existing pose generator and a 24-byte descriptor.
Copied modes use identical preparation, rendering and completion fences.
Rotate all six mode orders and retain 21 triples after warmup. Report producer copy/
preparation time, worker render/completion time, total completion and queue
wait separately. Do not insert an artificial logic delay or claim a speedup
from enqueue time alone. Backpressure is part of the measurement.

Run both checked and owned timing modes. All validation packets have full
integrity checks, including the deliberately borrowed-buffer fault. Checked
timings retain those costs on every packet; owned timings rely on the same
bounded ownership protocol without full-buffer checksumming. Recheck exact
output and ownership after sustained slot reuse. Immutable pose templates
are generated on the worker, so the producer copies bytes without changing
its floating-point control state or performing pose calculations.

The fixture still lacks native material-source capture, arbitrary resources,
terrain, reflections, Present and full installed beta.13 native integration.
Separate lit/shadow passes cannot establish 60 FPS. The test can establish
whether a bounded owned batch can move effect/state/draw work together and
what its actual copy/worker costs are. Any favorable result must next be
qualified against the real capture boundary and device/window ownership.
Production, installed files, game timing, camera and stable branches stay
unchanged. No game or WorldBuilder launch.

## Result

Six final runs pass 60 exact image/state comparisons, 12 deliberately
borrowed-buffer refusals and 12 full-queue/recovery controls. Each consumes
588 ordered packets, including two deliberately rejected controls. The
1,382,424-byte payload carries six bones for each of 7,200 meshes; two slots
bound ownership and backpressure. Preserve all 126 three-mode samples in
`docs/benchmarks/2026-10-18-render-batch-worker.json`.

Owned-mode producer preparation is 0.7024-0.8115 ms per synthetic regular/
extra pair, **excluding** the separately reported queue/collection waits.
Against the no-copy fixture, paired completion deltas are -0.2633/-0.1288 ms
lit and -0.6285/-0.2262 ms shadow. The handoff adds work; it does not make
the GPU renderer faster. Its potential value is letting the producer do
other work while rendering completes, which this test does not model as
game simulation. Checked timings add roughly 5.9-6.0 ms versus direct work;
do not disguise that diagnostic cost or compare only with the copied serial
baseline. The initial two-mode pilot is superseded by this fairer baseline.

## Next real boundary

The native lifecycle audit rules out passing live callbacks wholesale to a
worker. The asset getter can initialize resources; Begin applies pending
render state, binds textures and walks sources before D3DX Begin; Dynamic
fetches the effect after source binding. Begin and Dynamic can therefore
use different effect identities, and Begin's result/pass count controls the
native path. See `2026-10-17-native-fx-lifecycle-audit.md` and the actual
wrappers exercised by `perf/tests/bench_native_fx_gpu.c`.

A next capture prototype must keep readiness checks, source evaluation,
native state/counters and ownership on their original thread while
recording copied values and stable resource identities for the renderer.
It must explicitly handle effect identity changes, returned Begin/pass
information, unsupported commands and failed/deferred work. A packet of
palettes plus synthetic material generation does not settle these contracts.
Qualify these boundaries before proposing live worker hooks or another
gameplay test. Device creation/Reset/window ownership is also still open.

Reproduce with `mingw32-make build/bench_batch_worker.exe`; copy the executable
to a temporary directory containing the selected local D3D9 backend. Run
with local `Shaders.big`, local `msvcr71.dll`, `lit`/`shadow`, and optionally
`owned`/`checked`. D3DX27 must be beside the CRT. The recorded commands and
hashes identify the tested DXVK backend. No external Accelerator DLL is used.
