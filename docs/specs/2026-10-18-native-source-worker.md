# Native source values across the worker boundary

Extend the owned-batch experiment with actual verified native matrix-source
callbacks on the producer. This qualifies a concrete slice of capture rather
than moving native rendering callbacks and their shared state to the worker.
The surrounding asset, material and resource lifetimes remain controlled.

Reuse the existing nine-body native matrix verifier and its independent
54-case oracle. Native bytes stay in private RAM, loaded from the user's
local image only after complete-body/dependency checks. Source providers
read producer-owned private state. A recording effect sink copies each
typed matrix setter, semantic identifier and 64-byte value into the packet.
The worker maps semantic identifiers to its own effect handles and applies
the original setter type. No engine/effect pointer crosses threads.

Use actual world/view/projection source callbacks for every fixture group.
Both lit and shadow effects receive the captured values after their other
source inputs and before Begin. Preserve SetMatrixTranspose versus SetMatrix
semantics; a generic byte copy is not an equivalent setter. Export immutable
canonical fixture inputs once from the GPU owner for the producer's private
native providers. Direct and queued modes capture the same native values.

The unqueued GPU reference uses its original canonical values. Require
exact nonempty output/state equality after native capture and copied replay,
including all existing scratch-poison, full-queue and stale-pose/material
controls. Poison provider storage after capture to reject borrowed data.
A deliberately wrong matrix-setter mode must fail the output/state oracle,
so a correct result cannot come solely from unused parameters.

A fourth reference changes the matrices while retaining the same pose and
material. It uses ordinary SetValue directly, independently of the typed
capture/replay path. The timing loop alternates the two source generations;
an intentionally stale generation must fail the output oracle. The original
fixture remains available without native capture for regression checks.

Measure capture and copies in the producer costs, with GPU completion and
backpressure retained. Validate both executable variants and lit/shadow
workloads; record verifier refusals and source/input hashes. Do not convert
these isolated passes to FPS or claim full native material capture.

Still open: actual engine object/resource ownership, all non-matrix source
families, changing effect identity, Begin/pass result dependencies, arbitrary
failure/fallback and live device/window/Reset integration. This fixture
does not qualify a production worker or justify another gameplay test.

## Result

Eight positive runs (both images, lit/shadow, two repeats) pass 96 exact
image/state cases and 16 alias/capacity controls each. Eight wrong-setter or
stale-source processes fail the output oracle as intended; each executable
also rejects eight altered native contracts. Each positive run executes
509,760 native matrix callbacks and consumes 590 packets, including two
intentionally rejected aliases. Default worker regressions preserve the
original lit/shadow image hashes.

The [report](../benchmarks/2026-10-18-native-source-worker.json) preserves
168 timing samples. Producer preparation is 0.5218-0.5536 ms/pair, excluding
5.4178-6.0864 ms collection waits. Lit paired completion savings are
0.0687-0.3292 ms; shadow deltas range from -0.4462 to +0.0966 ms. No
substantial repeatable completion gain is established. The matrix overrides
follow the fixture's original source setters, and the material/resource
lifetimes remain synthetic. Do not infer full game capture cost or FPS.

## Reproduction

Build `perf/build/bench_native_source_worker.exe` with the normal 32-bit
Makefile, then run `tools/bench_native_source_worker.py <install-folder>
--image <game.dat> --image <game820.dat> --repeat 2 --output <report.json>`.
The runner stages the fixture and the installed DXVK into a new temporary
folder, checks for the game and launcher before/after each process, and
preserves numeric manifests, logs and hashes. Negative controls require an
actual pixel/state assertion, not merely a nonzero exit status. No game
entry point, live engine objects or external accelerator code runs.

## Native pose preparation extension

The matrix-only workload has little producer work to overlap. The optional
`bench_native_pose_worker.exe` adds a concrete consumed computation: the
existing verified native vector decoder supplies next-endpoint positions
for all 43,200 fixture bones on each draw. Quaternion and padding bytes stay
equal to the template. The decoder bodies and filter dependency are verified
before private loading from the local executable, with the existing ABI,
cursor, alias and changed-contract controls. The adapter uses general
registers only and adds no floating-point-control access.

The direct baseline generates the pose on the worker immediately before
drawing, without copying a palette packet. Serial-copy and two-slot pipeline
modes generate it on the producer and copy every palette byte into the
owned packet. All modes do the same native work and consume the same values.
Immutable channel, stream and filter hashes must survive the complete run.
Only copied bone values cross the queue; native callbacks and live engine
objects are not handed to another thread.

The independent reference uses the decoded palette with the original matrix
setters. Decoded versus raw-template images must differ, so unrelated CPU
work cannot manufacture an apparent overlap benefit. A deliberately stale
pose must fail the packet image/state comparison, alongside the existing
wrong-setter and stale-matrix faults. Provider/scratch poisoning, borrowed
buffer refusal, bounded-queue recovery and pre/post-run image/state checks
remain enabled outside the timed loop.

This is a warm synthetic channel workload: zero or one cursor advance, with
generated streams and incoming positions. The decoded next endpoint is
consumed in both cases. No interpolation, hierarchy composition or real
animation asset is modeled. The decoder count and cursor distribution were
not measured from gameplay, and the fixed bulk-after-second fixture is not
the full beta.13 renderer. Its own device/window still avoids the unsolved
live-device integration. This experiment tests overlap, not a deployable
animation or rendering replacement.

### Result

Two repeats on both images for advance one, plus two repeats on `game.dat`
for advance zero, pass 144 exact packet image/state cases across 12 positive
processes. All 18 wrong-setter/stale-source/stale-pose faults fail the output
oracle, and 24 alias refusals plus 24 capacity/recovery controls pass.
Each positive process executes 594 pose fills (25,660,800 vector calls),
509,760 matrix callbacks and 590 queue consumptions including two rejected
alias packets. The original matrix worker passes 24 image/state cases and
four output faults; the original batch worker preserves its hashes in
20 image/state cases.

The [report](../benchmarks/2026-10-18-native-pose-worker.json) retains
252 three-mode samples. Paired completion savings against direct mode are
1.4112-1.9442 ms/pair for advance one and 0.7457-1.2160 ms for advance zero.
Producer preparation is 2.4055-2.4579 and 1.8517-1.9204 ms/pair respectively,
excluding the recorded waits. Completion includes those waits and final GPU
drains. Lit and shadow are separate modeled passes; do not add their savings
or translate them into FPS. The result supports further worker integration,
with the broader source/lifetime boundaries still open. No runtime change,
install or user gameplay test follows from this fixture.

### Reproduction

Build `perf/build/bench_native_pose_worker.exe`, then use the same runner
with `--native-pose --pose-step 1`, both images and `--repeat 2`. Repeat with
`--pose-step 0` for the lower-work sensitivity case. Each process receives
separate numeric matrix and decoder manifests. Original matrix-worker CLI
and target remain available by omitting `--native-pose`.

## Device owner integration experiment

Before moving this path into the game, verify the complete local native
device-creation body and its flags. Microsoft requires MULTITHREADED when
the window thread and D3D rendering thread differ; CreateDevice, Reset,
TestCooperativeLevel and final device release remain on the window thread.
An already-created device cannot gain the flag through Reset. A runtime
bridge therefore needs an early verified creation hook or must stay off.

Extend the existing optional pose fixture with an owner mode: initialize
its hidden window/device/resources on the producer using MULTITHREADED,
then transfer exclusive drawing to the worker. Pump window messages while
collecting completions, and verify actual window/creation thread identity
and flags. Retain the independent pre-handoff images and all ownership/
stale-data controls. Compare completed work against the same serial/direct
path with the same flags. This tests handoff and required locking overhead;
Reset/resource recreation and final release need their own qualification.
The fixture's legacy resource leaks mean process exit is not evidence of
correct live-device destruction. Do not install this test-only mode.

The native creation audit verifies the complete 460-byte body, jump targets,
factory slots, one-time-init dependency and consistent global identities in
both local images, with six altered-contract refusals each. The native path
requests flags 0x20/0x22/0x40/0x42/0x50/0x52, never MULTITHREADED (0x4).
These are disk-code semantics, not a read of a live device. The previous
beta.13 gameplay log says `d3d9.dll` was not loaded when the ASI attached;
that supports an early creation hook on that startup path, not every loader.
An eventual hook must verify the device is not already created and preserve
all existing creation flags, FPU behavior and native call results.

Two hidden assumptions failed in the first owner-mode attempts: timing and
instancing each retained the initialization thread ID. Test-only helpers now
transfer their IDs only at an empty, exclusive handoff, and return them after
worker completion. They are not production APIs: the live plugin must keep
frame and renderer ownership separate while both threads can run.

The next attempt matched images but failed state word 744: opacity was
0x3F666666 on the initialization thread and 0x3F666667 when re-evaluated on
the worker. No floating-point control workaround is used. Owner mode now
evaluates opacity, diffuse color and time on the producer and copies their
numeric values plus a fixture-local texture index into each packet. The
worker resolves the texture against its fixed resource table. The ordinary
reference still evaluates the original expressions independently. A stale
material control must fail the image/state oracle. This proves the need to
capture values for this fixture; it does not qualify all native source
families or a live resource registry.

Both images, lit/shadow, two repeats pass 96 exact packet image/state cases,
16 output-corruption controls, four missing-MULTITHREADED refusals before
worker startup, and 16 alias/capacity controls each. Each positive process
dispatches 590 ordered worker messages on the window thread without calling
D3D in the window procedure. Across 168 three-mode samples, paired completion
savings are 1.4908-1.9790 ms/pair lit and 1.4836-1.7123 ms shadow, with copies,
message handling, required device locking and GPU drains included. Separate
process timings do not establish the incremental cost of MULTITHREADED alone.
See the [owner report](../benchmarks/2026-10-18-window-owner-worker.json).

The native Reset_Device source releases assets before calling the D3D Reset
method, so flushing only at the latter would be too late for queued borrowed
resources. A live bridge must stop publication and drain before native
resource teardown, keep Reset/TestCooperativeLevel/final device release on
the window owner, and reject stale resource generations after recreation.
That lifecycle, native Begin/pass results, full-frame integration and real
FPS remain open. Reproduce the owner mode by adding `--window-owner` to the
native-pose runner and building `bench_window_pose_worker.exe`.

Platform references: [D3DCREATE](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dcreate),
[CreateDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d9helper/nf-d3d9helper-idirect3d9-createdevice),
[multithreading rules](https://learn.microsoft.com/en-us/windows/win32/direct3d9/multithreading-issues).

## Reset and resource lifetime qualification

`bench_window_reset_worker.exe` extends the same owned-packet/native-pose
fixture with a deliberately full two-slot queue at each reset boundary.
The producer closes publication, collects both completed draws while pumping
messages, and only then requests worker teardown. The worker drains the GPU,
ends effect use, releases retained state blocks and instancing caches, calls
OnLostDevice, unbinds resources and releases explicit render/depth targets.
The producer joins the worker before calling TestCooperativeLevel and Reset.
It recreates targets, restores copied numeric fixture state, calls
OnResetDevice and starts a new worker before reopening publication.

Packets carry a resource generation that advances only after successful
Reset. A checksum-valid old-generation packet must be rejected before draw;
disabling this check is a deliberate failing control. Another fault attempts
teardown before collecting the queued work and must fail the boundary guard.
Closed admission is checked both before teardown and after an injected
failed Reset result. This injection exercises result handling; the device
itself is not forced into a real lost state. The successful Reset calls are
real. Allocation failures, actual device loss and fullscreen transitions
remain outside this fixture.

Resource ownership checks exposed old fixture leaks: overwritten shader and
surface owners, unreleased temporary pixel shaders/factory references and
retained source shaders in the test instancer cache. Fix the fixture owners
and explicitly release every cache reference at teardown. Final device
Release must return zero on the window thread before destroying its window.
This adapter is test-only; production instancing reset hooks are unchanged.

Three reset/recreate cycles retain the original pre-reset image/state
references rather than regenerating expected outputs. Each successful
process checks 30 exact images, four alias refusals and queue-capacity
recoveries, three stale-generation refusals, six closed admissions and
four resource generations. No timings or gameplay FPS are inferred.

Build `perf/build/bench_window_reset_worker.exe`; run the existing runner
with `--native-pose --window-owner --reset-lifecycle`, both images and an
output report. Repeat with `--backend native` to use system D3D9; the default
stages the installed DXVK in an isolated temporary directory. The original
owner benchmark remains available without `--reset-lifecycle`.

This closes a fixture lifecycle boundary, not native integration. A live
bridge still needs verified hooks before native resource teardown, separate
frame/render accounting, all native source/effect identities and Begin/pass
result handling. The test restarts its worker between generations and owns
all resources; neither models concurrent native asset creation/destruction.

Platform contracts: [Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d9helper/nf-d3d9helper-idirect3ddevice9-reset),
[OnLostDevice](https://learn.microsoft.com/en-us/windows/win32/direct3d9/id3dxeffect--onlostdevice),
[OnResetDevice](https://learn.microsoft.com/en-us/windows/win32/direct3d9/id3dxeffect--onresetdevice).

Both images on both backends pass 240 exact image/state cases over 24 real
resets, with eight zero-reference final device releases. All 56 deliberate
fault processes reach their expected checks. Original owner/batch regression
hashes remain unchanged across 44 image/state cases, and the full `perf/`
suite passes. See the [lifecycle report](../benchmarks/2026-10-18-worker-reset-lifetime.json)
for input/log hashes, superseded leak-failing pilots and exact controls.
The installed beta.13 and its INI remain hash-identical; no deployment.

## Consumer state independent of game frame hooks

The exclusive thread-ID transfers above cannot be a live implementation:
the producer must continue its own frame hooks while rendering proceeds.
The existing `instsubmit.c` transaction engine already carries its state in
an explicit caller-owned context. Use it directly on the worker instead of
making `timing.c`, `inst.c` and `fps.c` global state concurrent.

The optional `bench_packet_consumer.exe` owns a separate instance buffer,
replacement shader, declaration cache, submission context and draw counters.
It consumes the copied palette directly, without writing synthetic native
mesh/tree objects or calling the game's mesh/event/display hooks. Material
effects and their setters remain on the GPU owner. The first unit follows
the checked ordinary draw path, and ordered followers use checked instanced
transactions. The original hook-based renderer remains the independent
image/state oracle. Regular and extra draws use packet flags and separate
consumer counters, leaving the global extra-frame state unchanged.

Producer preparation executes through the existing frame/render stubs.
An untimed handshake leaves an effect open inside the worker's scene while
the producer executes three more frame hooks and flushes diagnostics.
Require balanced producer stacks, unchanged native instancer totals/method
references, no stale scopes and exact producer frame/preparation counts.
Deliberately transferring the timing thread ID or setting the shared extra
flag from the worker must fail these checks. This is fixture concurrency,
not an invocation of the real game's frame/logic functions.

Retain the reset suite's original output references across all generations,
and require zero final device references. Clear the native instancer's
initial oracle resources on the producer after joining the worker; clear
the independent consumer's resources on the worker before teardown.

Build `perf/build/bench_packet_consumer.exe` and add `--packet-consumer` to
the reset runner. Both `--backend dxvk` and `--backend native` are supported.
This mode does not collect timings. Previous worker timing deltas cannot
be assigned to this changed consumer without a fresh paired comparison.

Next integration boundary: capture a verified native FX group into this
consumer while preserving effect identity, Begin/pass results, resource
ownership and original release order. The fixed single-family fixture is
not a native group capture bridge or a full-scene renderer. Actual failure
recovery and result propagation at that boundary must precede runtime use.

Both images on DXVK and native D3D9 pass 240 exact image/state cases, 24 real
resets and eight zero-reference final releases. All 72 deliberate faults
reach their expected checks, including 16 timing-owner/shared-extra faults.
Each successful process records 40 producer frame hooks, 37 preparation
hooks and three probes while the worker effect is open; 11 regular and 19
extra draws leave the native instancer's counters and method references
unchanged. The original reset/owner/batch variants preserve their hashes
across another 104 image/state cases and 24 deliberate faults. See the
[consumer report](../benchmarks/2026-10-18-independent-packet-consumer.json).
Installed beta.13 and its INI remain unchanged; no runtime promotion.
