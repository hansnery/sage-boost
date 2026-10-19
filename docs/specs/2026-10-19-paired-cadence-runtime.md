# Paired cadence beta.14

This implements route 2 of the
[approved timing experiment](2026-10-18-simulation-beta-design.md). The
standalone [policy screen](../benchmarks/2026-10-18-paired-cadence.json) is a
historical test-only checkpoint; beta.14 connects its controller to the
existing verified frame and limiter hooks.

## Behavior

`[Experimental] PairPacing=1` permits one checked extra draw per ordinary
game update. Its predicted completion may borrow up to 4 ms against the
next native period. Later limiter waits repay small debt instead of starting
every period from the late completion. Ordinary updates, counters, rate
globals and duration parsers are not changed. The game still owns the camera.

Cadence uses the native millisecond clock throughout; QPC is used only for
pacing starts and measuring the complete extra draw, including restoration.
The 1.25 ms reserve covers the native clock's unknown fractional phase plus
250 us overhead. Small debt survives native-period changes so a multiplayer
pacing adjustment cannot repeatedly erase borrowed time. Missing waits,
long stalls and ineligible scenes reset the epoch.

The runtime requires the existing frame, limiter, draw, unit and game-state
sites, `MaxFPS>=60`, `FrameSmoothing=3`, `ShadowInterval=1` and
`ExtraFrameShadows=1`. Reflection preferences remain the user's choice.
The option defaults off. Missing prerequisites log a refusal and retain
legacy behavior. It changes no existing INI value.

While paired cadence owns an epoch, the catch-up adapter keeps observations
but cannot skip a render or impose a second deadline. If no ordinary limiter
wait occurs, paired ownership ends at the next frame boundary and catch-up
can resume. Paused games, the shell, stopped client time, screenshots,
minimization, building placement and camera scrolling do not use paired
cadence; legacy rendering behavior remains available there.

## Recovery and diagnostics

A predicted borrowing allowance cannot bound unexpectedly slow GPU/OS work.
Actual debt above 4 ms uses the existing late-frame backoff. A stale cost
floor is retried only after 60 eligible frames in which a measured reference
or previously successful cost would fit. Three unsuccessful paired probes
rest extra frames. This avoids both permanent starvation after a transient
and repeated attempts at work that remains too expensive.

The normal CSV continues counting real extra draws, costs, scopes and
Present calls. The log adds `paired cadence:` reports every 60 eligible
frames: extras, shortened waits, peak debt and prediction misses. In this
mode the existing probe counters describe fitting the paired allowance,
not necessarily fitting the original deadline. A successful probe resets
stale cost history and logs its measured cost.

## Verification boundary

`test_pair_runtime.exe` links the production integer-only `fps.o`,
`pairpace.o` and `catchup.o`. It drives the actual adapters with owned engine
state, a quantized native clock, advancing fine clock and controlled draw
durations. It is not a GPU benchmark or native game-loop execution.

The adapter tests cover ordinary-update cadence, fresh shadow calls,
reflection preference, held view/particle updates, unchanged camera,
pause/UI/loading gates, changing periods, missing waits, state-check failures,
catch-up ownership and cost recovery. The policy test separately checks
clock wrap and the independent sum of 6,000 changing native periods.
The full plugin suite checks supported-image sites, hooks, configuration,
ABI, load/version and existing rendering behavior.

The final suite passes with exit 0. The [runtime report](../benchmarks/2026-10-19-paired-cadence-runtime.json)
records exact source/object/plugin hashes and raw adapter output. Its two
paired cases add 1,800 extras while retaining 1,800 ordinary updates in
about 59.4 virtual seconds. The transient test recovers with 211 extras;
the persistent-cost test makes three unsuccessful probes, then rests.

The first runtime recovery test correctly failed: one expensive draw could
raise the learned floor and prevent future extras. The bounded recovery
path fixes that case. Sustained expensive work is a separate negative
workload: it must back off, not receive the transient-recovery treatment.

No actual game, GPU scene, FrameView measurement or multiplayer peer runs in
these new cadence tests. The old cost aggregates are not paired samples.
Those tests alone do not establish a gameplay FPS gain. A local beta test
was justified by removing a demonstrated admission/recovery obstacle.

Hans subsequently reports about 55 FPS in the heavy scene. The
[copied gameplay capture](../benchmarks/2026-10-19-beta14-gameplay-check.json)
records 51.5833 presentation calls/s in the densest eligible complete
60-second window, with 1,295 extras and matching shadow samples. The scene
varies and the interval includes a slowdown. The settled 59/s target and
matching-beta sync remain open; the improvement does not establish 120 FPS
headroom or remove the adapter's one-extra-per-update limit.
