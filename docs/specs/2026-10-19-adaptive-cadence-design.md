# Adaptive cadence toward 120 FPS

Hans accepts beta.14's roughly 55 FPS in a very heavy Ultra High scene and
authorizes publishing that checkpoint and pursuing 120 now. His standing
preapproval covers this design. Beta.14 is published separately; it remains
the public Beta while a new candidate is qualified.

## Choice

Extend paired cadence with a measured allowance of one, two or three extra
draws. This reuses the working rendering/state/shadow path and absolute
native deadline. Merely raising MaxFPS cannot bypass beta.14's one-extra
limit. Native simulation retiming remains a broader alternative with more
timer, gameplay and network qualification; it is unnecessary to remove this
specific render ceiling. Earlier render-thread experiments do not justify
replacing the working implementation.

## Contract

- Add `[Experimental] PairMaxExtras=1`, accepting 1..3. Default 1 preserves
  paired beta.14 behavior. The effective maximum also respects MaxFPS.
- Only consider more than one after eight successful measured extra draws.
- Choose the maximum count once per ordinary frame, using the native
  remaining time and a conservative complete draw cost. Counts above one
  require every predicted draw plus its reserve to fit without borrowing.
- Keep beta.14's 4,000 us predicted borrowing only for the one-extra path.
  The absolute native deadline and repayment are shared across all draws;
  three draws must never get three separate borrowing allowances.
- Pace multiple presentations toward equal subdivisions of the native
  period, anchored to the first wait after the ordinary presentation. Clamp
  delays to leave predicted work/reserves for the remaining draws. If costs
  rise, reduce the remaining work; never increase the allowance mid-frame.
- Recheck current cost, eligibility and deadline before each draw. Preserve
  late backoff and bounded single-draw recovery probes. Repeated native
  wait callbacks cannot reopen an exhausted or refused allowance.
- Keep every fresh shadow, user reflection preference, ordinary update,
  camera/input behavior, state guard and integer-only timing implementation.
  Do not change native counters/rates or floating-point control state.

## Verification and limits

Use production scheduling objects with the existing owned-engine virtual
clock fixture. Check one/two/three-extra controlled workloads, ordinary
cadence, dynamic cost increases, second-draw faults, repeat callbacks,
MaxFPS limits, pause/UI gates, no-wait frames, period changes, clock wrap,
fresh-shadow counts and state restoration. Retain all beta.14 regression
cases, run the complete plugin suite, and audit integer object instructions.

A 33 ms native period allows about 121 modeled presentations/s with three
extras. This is neither an exact 120 Hz limiter nor proof of evenly spaced
8.33 ms gameplay frames. The existing heavy-window aggregates (18.01 ms
ordinary and 8.86 ms extra) cannot fit three extras; the controller must
fall back. More render-cost reduction may be needed for heavy-scene 120.
Do not claim gameplay improvement, multiplayer qualification or publish a
replacement based only on the controlled fixture.
