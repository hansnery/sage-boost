# Intermittent paired-cadence recovery

Beta.16 fixes recovery starvation when occasional light ordinary frames
permit extras between otherwise cost-blocked frames. It inherits beta.15's
opt-in adaptive allowance; PairMaxExtras still defaults to one.

The original counter required 60 eligible blocked frames but reset on every
normally admitted extra. A slow measured extra could therefore remain in
the 64-draw maximum for many seconds, while intermittent cheaper successes
prevented the bounded probe from ever running. The reference ordinary draw
can be more expensive than an extra and cannot always age that maximum out.

Remove only the reset on normal admission. Eligible blocked observations
now accumulate across ordinary successful extras. Ineligibility, a candidate
that no longer fits, a probe attempt and pause handling still reset credit.
Probes still use an established successful cost, require the same deadline
admission, and retain the same failure limit and backoff. No deadline reserve,
borrow allowance, frame counter, native rate, render pass or INI value changes.
This does not solve recovery when no cheaper successful sample exists.

## Reproduction

Use the production-object virtual-clock adapter. Warm up, record a 20 ms
ordinary reference, then draw one 18.5 ms extra during an 8 ms ordinary frame.
Thereafter extras cost 12.849 ms and ordinary work costs 19.854 ms, except
every fiftieth frame costs 8 ms. These are controlled inputs, not a replay
of per-frame gameplay data.

Before the fix, 1,800 updates admit only 36 extras and no probes. After the
fix they admit 1,741 extras, with one successful probe, zero late predictions,
and 59,400,015 virtual microseconds. The regression also checks fresh shadow
and reflection counts, ordinary counters, random seed, held view/particle
updates and camera state. Existing persistent expensive-work tests still
stop after three failed probes. If no intermittent light frame supplies a
cheaper successful sample, both versions admit zero extras in this fixture.

The newer user capture shows zero probes and prolonged cost estimates above
some admitted extra costs, which motivated this investigation. One-second
aggregates cannot prove the same control-flow sequence occurred in that game.
No gameplay gain, final 59/s result or multiplayer qualification follows from
the fixture. Keep the published beta.14 and Stable intact; beta.16 requires
separate deployment and real gameplay qualification.

## Supplemental eligibility regression

After local deployment, an additional production-object regression accumulates
30 eligible blocked observations, interrupts with pause, scrolling or building
placement, then resumes. None of the three cases probes within the next 30
observations; each probes successfully during the following 40, without a late
prediction. This checks that retained credit does not cross an ineligible UI
or game state and that eligibility can recover afterward. `make test-pair-runtime`
passes including these cases and the existing three-failed-probe rest case.
Only the test changed; the installed beta.16 binary and preferences did not.
The complete prior suite remains the runtime build validation; these additional
cases were run through the focused adapter target, not a second full suite.
