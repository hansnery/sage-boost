# Paired frame cadence experiment

Hans pre-approved the simulation-beta plan. This is route 2 of the
[accepted scope](../specs/2026-10-18-simulation-beta-design.md): change the
presentation schedule before attempting a complete 10/60 simulation retime.

The existing extra-frame scheduler requires the extra draw, 3 ms reserve,
one eighth of its expected cost, and 0.5 ms scheduling slack to fit inside
each stock frame. The last capture has a 19.85 ms mean normal frame and
retained extra estimates of 10.82 and 12.85 ms. These are not paired samples
and do not predict gameplay FPS, but explain why the admission policy can
reject work whose combined cost is below the native 33 ms period.

Prototype a separate, integer-only paired cadence controller. Keep one
ordinary update per native period and at most one additional draw per update.
Use the existing checked extra draw, including fresh shadows, and retain
gameplay/camera/state guards. The controller may plan at most 4 ms against
the next period; it repays that debt by ending a later limiter wait at an
absolute deadline. Never repay by skipping game updates or changing their
counters. This is a different deadline policy, not a claim of cheaper draws.

The first screen uses controlled costs, including cases anchored to the
recorded aggregates. It must show where the policy helps and where rendering
is too expensive for 60 FPS. Do not call these costs a replay of the capture.
No runtime integration or installation follows from the model alone.

- [x] Implement the controller separately from the legacy scheduler.
- [x] Compare legacy admission with paired cadence in a closed timing loop;
  include both limiter quantization and actual draw completion.
- [x] Test debt repayment, underestimated work, sustained overload, clock
  wrap, missing frame observations, pause/stall and native-period changes.
- [x] Check independent regular-update counts across different render costs.
  This is a scheduling test, not multiplayer synchronization qualification.
- [x] Keep raw model results and limitations; decide whether to integrate.
- [x] 2026-10-19: integrate behind a new opt-in beta setting, using
  verified existing frame/limiter hooks and one owner of pacing. Qualify
  production hooks, pause/loading gates, diagnostics and fallback; run the
  full plugin suite before any installation. Both pass; see the
  [runtime report](../benchmarks/2026-10-19-paired-cadence-runtime.json).
- [x] Commit/push and install the qualified beta.14 with backup, preserved
  settings, matching hashes and a checked user-invoked test shortcut.
  [Receipt](../benchmarks/2026-10-19-paired-cadence-install.json).
- [ ] Verify the settled heavy-scene target and matching-beta multiplayer
  in game. No offline result completes this requirement.

Native source inspected: `GameEngineExecuteRva0022D290.cpp` computes each
period from the configured cap and network scale, waits from the previous
frame's completion, then stores the current clock as the next start. The
existing catch-up hook can end that wait by setting the saved ESI threshold
to zero. Reuse that verified protocol if integration proceeds. Follow the
actual native period rather than imposing a fixed multiplayer speed.

Full 10/60 retiming remains available, but the native duration findings and
startup dependencies make it a broader change, and doubling updates would
also increase CPU work. No duration parser or shared rate is changed here.

## Screen outcome and concrete integration work

`make test-pairpace` passes 160,438 checks and writes 22 model rows. See
[the report](../benchmarks/2026-10-18-paired-cadence.json) for input costs,
fault controls, model omissions and the unchanged installation hashes.
The modeled benefit warrants an opt-in adapter; it does not qualify an
installation by itself. At that checkpoint the controller was not in
`SRCS`; beta.14 now integrates the items below. See the
[runtime behavior](../specs/2026-10-19-paired-cadence-runtime.md).

- `perf/src/pairpace.[ch]`: controller, compiled with production integer-only
  flags; tested by `perf/tests/test_pairpace.c` and `tools/test_pairpace.py`.
- `perf/src/fps.[ch]`: add an explicitly enabled paired-wait path. Use native
  `timeGetTime * 1000` for both cadence and elapsed time; use QPC only for
  delaying draw starts/measuring cost. Increment regular-frame identity at
  the frame hook, including frames that never wait. Reuse checked extra draw,
  interpolation, shadow/reflection and restoration paths. Preserve old mode
  when the option is absent or prerequisites fail. Count actual extras,
  deadline debt, shortened waits and prediction overruns.
- `perf/src/catchup.[ch]`, `main.c`: establish a single pacing owner. While
  paired cadence is enabled, the legacy catch-up controller must neither
  skip a regular render because of borrowed time nor apply a second wait
  deadline. Keep its game-speed observations if feasible. Find/gate the
  running-game, paused, client-running, screenshot and UI conditions using
  the existing verified sites. On failure, log the reason and use legacy.
- `perf/src/config.[ch]`, shipped INI and README: new opt-in experimental key;
  do not change existing values. Limit the new policy to one extra per
  ordinary update, even if the preserved MaxFPS setting requests more.
- `perf/tests/test_fps.c`/hook tests: exercise the actual adapter with a
  quantized native clock and advancing fine clock. Test costs and their
  learning, debt repayment, pause/resume, missing frame waits, rate changes,
  screenshot/minimized/UI gates, full-rate shadows and state-guard failures.
  Test the policy together with catch-up ownership, not only each in
  isolation. A missed prediction must not permanently starve extras or
  repeatedly slow the simulation.
- Build/version/full suite/deployment receipt: only after the adapter is
  qualified. The test-only policy checkpoint does not bump a plugin version.

The model's 4 ms limit applies to predicted completion. Injected unexpected
work reaches 9 ms debt; the runtime must account for actual completion and
retain a measured overrun/backoff policy. Do not present the prediction bound
as a guarantee against GPU/OS stalls.
