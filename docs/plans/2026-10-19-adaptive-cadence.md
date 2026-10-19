# Adaptive Cadence Implementation Plan

**Goal:** Remove the paired scheduler's one-extra ceiling behind an opt-in
limit while preserving the published beta.14 checkpoint.

**Architecture:** Extend the pure integer paired controller, then use it
from the existing checked draw adapter. Keep one absolute deadline and
unchanged rendering/state guards. Execute inline under Hans's preapproval.

**Tech stack:** C, WinLibs i686 GCC, Python report capture, existing make tests.

## Constraints

Full-rate fresh shadows; no camera movement or game-counter/rate writes;
integer-only controller; no game launch; no new launcher publication until
gameplay qualification. PairMaxExtras defaults to 1 and is capped by MaxFPS.

## Tasks

- [x] Extend `perf/src/pairpace.[ch]` with a once-per-frame maximum allowance:
  `void pf_pair_limit(pf_pairpace *p, unsigned now, unsigned expected,
  unsigned limit)`. The default is one; counts 2/3 require
  `count * (expected + PF_PAIR_RESERVE_US) <= deadline - now`.
  `pf_pair_plan` and `pf_pair_claim` track used draws, recheck costs and keep
  the original one-extra behavior. Cover independent deadline/count/cadence
  oracles in the runtime fixture and retain `test-pairpace` regressions.
- [x] Wire `PairMaxExtras` through `config.[ch]`, `main.c`, `fps.[ch]` and
  `bfme2perf.ini`. Add `pf_fps_set_pair_limit(unsigned limit)`. Refuse invalid
  config values to default 1. Only unlock multiple draws after eight measured
  successes. Loop the existing checked draw path at most three times, stop
  on state/deadline failure, and keep recovery probes single-draw.
- [x] Expand `perf/tests/test_pair_runtime.c` and config tests. A controlled
  regular=6ms, extra=4ms case must draw 5,400 extras over 1,800 ordinary
  updates in approximately 59.4 virtual seconds. Compare with one-extra
  mode, test 90/60 limits, heavier one/two-extra cases, cost transitions,
  a second-draw state fault and full shadow counts. Synthetic timing is not
  a game benchmark.
- [x] Bump the candidate to 0.9.11-beta.15 in `main.c`, `version.rc` and
  the load test. Run `mingw32-make test-pairpace test-pair-runtime`, then
  `mingw32-make test` through the documented Git Bash toolchain. Audit
  production integer objects and save a versioned report with source hashes.
- [x] Review the diff, update status/backlog with measured limitations,
  commit explicit paths with the required trailer and push. Keep the public
  beta.14 payload frozen. Any local installation requires the complete suite,
  a backup and hash verification; no automatic game launch.

## Outcome

The complete suite passes. The lightweight 6/4 ms controlled case produces
5,400 extras with 1,800 ordinary updates in 59.4 virtual seconds; heavier
12/7 ms and 18/8.9 ms cases select two and one respectively. Six integer
production objects pass the instruction audit. The candidate is retained
for further rendering-cost work; it is not installed or published because
these controlled results do not establish a significant heavy-scene gain.
Beta.14's installation and frozen release payload remain hash-identical.
[Evidence](../benchmarks/2026-10-19-adaptive-cadence-runtime.json).
