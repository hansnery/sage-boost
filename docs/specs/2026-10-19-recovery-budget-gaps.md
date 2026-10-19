# Recovery credit across intermittent budget gaps

The beta.16 capture retains high estimates with zero probes in several
cost-blocked intervals. Its per-second telemetry cannot identify every
probe refusal, but a production-object reproduction establishes a second
starvation mechanism beyond the normal-admission reset fixed in beta.16.

## Controlled reproduction

Warm the adapter, establish a 20 ms ordinary-draw reference, and admit an
18.5 ms extra during an 8 ms ordinary frame. Subsequent extras cost 12.849 ms.
Ordinary work is normally 19.854 ms, but every 25th frame costs 25 ms and
every 50th-plus-one frame costs 8 ms. The 25 ms frames cannot admit even the
cheaper recovery candidate, so they reset recovery credit before it reaches
60. All times are fixture inputs; no native game/GPU workload executes.

Over 1,800 updates, production beta.16 admits 36 extras, zero probes and
zero late predictions in 59,400,032 virtual microseconds. An offline candidate
that removes only the reset when the candidate does not fit admits 1,669
extras and one probe, with zero late predictions in 59,400,019 microseconds.
It does not increment credit on non-fitting frames. Pause/UI/other eligibility
resets and the probe-attempt reset remain unchanged. With no intermittent
cheap successful sample, both variants still admit zero extras.

The candidate passes the complete focused adapter test, including pause,
scrolling and placement credit resets, three-failed-probe rest, changing
periods, multi-extra faults, guarded state and fresh-pass checks. This is
not the full plugin suite, an installed build, or a game FPS measurement.

## Next qualification

Before runtime promotion, explicitly test long non-fitting gaps, transitions
to persistently expensive draws, and changing recovery candidates. Retaining
credit must not bypass the current candidate's admission, move the deadline,
increase probe frequency above one per 60 eligible observations, or weaken
backoff and eligibility resets. Then add regression coverage, rerun the full
suite, and audit the compiled timing object. Current installation remains
beta.16; do not request another gameplay test on this offline result alone.

Reproduction files are under `perf/build/`: `screen_pair_budget_gaps.py`,
`screen_pair_budget_gaps.c`, `screen_pair_budget_candidate.c`,
`budget_fps.exe`, `budget_screen_pair_budget_candidate.exe`, and
`budget-candidate-suite.log`. These ignored artifacts are local test copies,
not changes to the production scheduler.
