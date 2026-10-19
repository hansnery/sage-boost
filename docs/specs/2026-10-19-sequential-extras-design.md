# Sequential extra-frame admission toward 120 FPS

The latest user target is 120 FPS, accepting near-60 as a milestone. The
installed beta.17 still defaults to one extra. Simply enabling three is
unlikely to transform the heavier captured rows: their conservative recent
cost estimate often exceeds twice the mean measured extra cost.

## Evidence and choice

The preserved final beta.16 backup's 1193..1252 interval has mean complete
extras of 6.772 ms. Its per-row estimates commonly range from 11 to 15 ms.
Using its average unadjusted budgets, only a small minority of rows could
reserve two complete worst-case extras upfront. These one-second values
cannot replay actual admission decisions or predict FPS. The existing
scope file is minute-bucketed and must not be joined as one-second data.

Choose sequential admission: use the measured average only to choose an
opportunity and optional pacing; require the conservative cost plus the
existing reserve at each actual claim. Alternatives are lowering the cost
estimate (risks admitting expensive work) or keeping upfront reservation
(forgoes headroom discovered after a cheap completion). The new policy keeps
the conservative estimate and the existing claim checks.

A controlled exploratory model, with 12 ms ordinary work, 7 ms typical extras
and a real 12 ms extra every 64 draws, admits 3,600 rather than 1,800 extras
over 1,800 ordinary updates. Both finish at 59.4 virtual seconds with no late
draws; modeled maximum presentation gap improves from 19.75 to 19 ms.
This is not a gameplay trace or an FPS prediction. The first exploratory
version could lose the second claim by pacing across an unknown native-clock
millisecond phase. Leaving that millisecond out of optional delay corrects
the model; it must be independently tested across fractional phases.

## Policy and implementation

Add separate pure-integer policy entry points; preserve the old functions
for comparison and one-extra compatibility. At the start of a wait, choose
at most the configured one/two/three extras using
`(count - 1) * average + expected + reserve <= remaining`.
This chooses a ceiling, not a promise that all draws will occur.

Before every draw, retain `pf_pair_claim` unchanged. Multi-extra draws cannot
borrow past the deadline. Recompute remaining opportunities after each draw;
discard any that no longer fit. For optional pacing, retain enough time for
the next conservative-cost claim and reserve one additional millisecond for
unknown fractional native-clock phase. When only one extra is configured or
selected, preserve the old policy exactly, including its bounded borrowing.

The runtime adapter requires eight successful observations before multi-extra
selection, uses the existing complete draw/state/fresh-shadow path, and keeps
pause, UI, load, stop, failure and recovery behavior. No new native hooks,
clock writes, camera changes, graphics cuts or frame counters are introduced.

## Qualification and deployment gate

Test the actual policy and production adapter: realistic cost variation,
first/second draw spikes, changing estimates, fractional phase, wraps,
changing periods, lost eligibility, recovery, and persistent overload. Require
unchanged ordinary cadence and strict per-draw conservative admission. Check
pacing as well as counts. Keep controlled results separate from game evidence.

Only after the full plugin suite and integer-object audit pass may a new
substantive beta be installed under the standing authorization. Retain public
Stable/Beta releases. Any local opt-in to PairMaxExtras=3 must back up ASI,
INI/logs/shortcut, preserve all existing preferences, pin the new hashes and
run the shortcut's CheckOnly mode; never launch the game. Installation is not
a claim of a 120 FPS result, which still requires gameplay evidence.
