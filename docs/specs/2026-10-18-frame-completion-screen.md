# Frame completion policy screen

This follows the [completion-boundary audit](2026-10-18-native-source-worker.md#completion-boundary-audit)
and the [failed-attempt record](../PERFORMANCE-INVESTIGATION.md). It is an
offline experiment under the existing development authorization. It changes
no runtime hooks, graphics settings, camera, simulation or installed files.

## Question and comparison

Does forcing GPU completion after every frame distort the apparent benefit
of the command worker? Compare six combinations: native bulk rendering,
direct command capture and worker command capture, each with either a drain
per frame or one drain after a bounded batch of two, four or eight frames.
The worker uses the previously qualified first-unit batching and merged
asynchronous start. All six paths finish the same amount of GPU work before
their timers stop. Retain the original seven-way per-pair timing separately.

This first control still waits for CPU command execution after each frame.
It permits GPU work to remain pending across frames; it does not yet let
the producer capture the next frame while the worker executes the previous
frame's final commands. No Present, game window or live engine is included.
Batch throughput does not establish acceptable gameplay latency or pacing.
All compared paths use the fixture's `D3DCREATE_MULTITHREADED` device; the
real game's device creation omits that flag. These are matched fixture
controls, not measurements of an otherwise unmodified live renderer.

## Ownership and correctness

`bench_group_completion.exe` adds a test-only policy to the existing group
fixture. `GC_FINISH` still restores the outer device state and ends the scene.
Deferred completion increments a bounded pending-frame count, and `GC_FLUSH`
waits for a final D3D event before resetting that count. Ownership mode cannot
change while GPU work remains pending. The existing frame sequence counter
describes completed CPU command execution in this mode; it is not evidence
of GPU completion before the explicit flush.

For correctness, every frame is copied into a separate GPU render target
before the shared target is cleared for the next frame. State is captured
at the same boundary. After the final drain, all saved targets are read and
compared to individually rendered native reference frames. Sequences vary
pose and material phase, including fresh shadow output. Existing exact lit
and state comparisons and the existing shadow-depth tolerance apply.

Nine batches per process cover depths 2/4/8 and all three render paths,
giving 42 intermediate image/state comparisons. The existing group,
first-unit, startup, callback, failure-recovery and reference checks remain.
Fault 16 replaces a middle retained image with an earlier frame while leaving
the final image intact, verifying that the checker does not accept only the
last frame. Fault 17 attempts a mode change before draining a pending batch.

Correctness copies, readbacks, draw observers and device audit wrappers are
removed before timing. Twelve rotated rounds at each depth retain all six
raw total completion times. Summaries normalize each total to two frames;
paired differences are calculated within each round. Assertions require the
expected number of frames and drains, with no pending GPU work or retained
command references at each measured batch boundary. Both paths include the
same fresh regular/extra sequence and the final completion wait.

## Execution and decision

The [implementation plan](../plans/2026-10-18-frame-completion-screen.md)
names files and commands. The Python runner hashes local inputs without
embedding EA data, enforces the game/launcher guard and runs the two local
executable images on DXVK and native D3D9 sequentially. The original
frame-start executable is also run as a separate regression.

A benefit from deferring completion on both paths must be attributed to
completion policy, not worker threading. Only worker-versus-native results
under the same policy can support the next threading decision. Even a
positive result requires separate qualification of live frame ownership,
resource lifetimes, Present, latency and unchanged gameplay behavior before
runtime deployment or a new request for Hans to test.

## Measured result (2026-10-18)

Across both executable images and lit/shadow workloads, the matched
per-batch comparison gives a DXVK worker saving ranging from **-0.4779 to
+1.2060 ms per regular/extra pair**. Negative values are regressions. Native
D3D9 saves **1.4799-3.4224 ms/pair**, but that is not the installed backend.
On DXVK, native bulk itself saves **0.3408-1.5460 ms/pair** by changing its
completion policy. Removing the benchmark's per-frame drains therefore
cannot be counted as a threading-only improvement. Depth and workload
matter; there is no consistent large win that supports a runtime install.

The eight candidate processes retain the existing checks and pass 336 new
intermediate image/state cases. All 136 deliberate faults are rejected,
including eight stale-middle-image and eight premature-mode-change cases.
The 288 six-way timing rows preserve raw batch totals and normalized paired
differences. The earlier frame-start executable passes separate lit/shadow
regressions on both backends. The [full report](../benchmarks/2026-10-18-group-completion.json)
preserves the pilot, source/input hashes, logs and installed-file hashes.

This completes the matched GPU-completion control. It does not test producer
preparation overlapping the worker's CPU completion across frames. That next
boundary still needs per-frame ownership of output, counters and resources,
fixed mode while requests are pending, and collection before reuse/teardown.
Keep this native per-batch control in any such comparison. No gameplay FPS
estimate, installation or new gameplay-test request follows from this result.
