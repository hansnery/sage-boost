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
