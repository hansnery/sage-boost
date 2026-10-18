# Target: 60 visual FPS with full-rate shadows

Status: proposed next-stage design. The user set the target on 2026-10-17
and explicitly chose shadows at 60 FPS as well. Beta.1 work through
`ace76a5` has been pushed to `origin/codex/perf-beta`; stable branches stay
unchanged. This target is not yet achieved.

## Success criterion

Use the user's troop-heavy saved game as the acceptance scenario. Preserve
resolution, graphics settings, troop count and camera position for each
comparison. Record at least 60 seconds after loading settles, then a moving
camera/battle segment. Compare the installed beta.1 with each new build.

Target sustained approximately 60 actual presentations per second (allow
timer quantization, e.g. >=59 over the stationary test interval), with a
fresh shadow draw for each extra frame. Count presented frames and inspect
frame gaps; a 60-FPS average with uneven bursts is not sufficient evidence
of smooth 60-FPS pacing. Report median and 95th-percentile presentation
intervals alongside the average. Menus and loading do not qualify.

The user still launches and plays the game. No autonomous game launches.
Simulation, input and the camera retain their stock update cadence. This
is a units-and-shadows rendering target, not a 60-Hz simulation or camera
motion change. Failure to meet the target must be reported plainly.

## Cost evidence

In seconds 55-114 of the preserved beta.1 capture, row averages show about
23.98 ms of game-frame work and a roughly 12.46 ms extra-draw estimate.
Adding the scheduler margin/slack gives about 41.5 ms of combined work
against a 33.3 ms game-frame interval: roughly 8 ms must be removed from
that combination. This is an engineering budget, not a measured prediction
of the next draw. Timing scopes overlap and cannot simply be summed.

Regular shadow passes average about 7.48 ms; FX batch scopes across passes
about 6.87 ms; shadow renderOneObject calls about 2.70 ms; skeleton work
inside regular rendering about 1.65 ms. Actual shadow instanced submissions
are only about 0.16 ms in this trace. Focus on the work around those draws,
not merely reducing the number of instanced draw submissions further.

Only 30 declarations were cached and there were no evictions. Missing
normalmapped shader coverage is a small proportion of this scene. Neither
is currently supported as the main path to 60 FPS in this save.

## Approaches

1. Recommended: reduce measured full-pass CPU/setup work, retaining the
   same shadow cadence and picture. First close the extra-frame measurement
   gap, then optimize the dominant repeated work with output/state tests.
2. Independent shadow shader coverage: implement the two identified
   normalmapped variants in a separate experiment. Narrowly verifiable,
   but likely a small gain for this particular scene.
3. Worker-thread animation or scene reconstruction: defer. Shared caches,
   object dependencies and FP requirements require a larger safety proof;
   current measurements do not justify starting there.

Shadow/reflection cadence cuts and lowering graphics quality are not the
recommended path. The user explicitly rejected 30-FPS shadows. Keep
ExtraFrameShadows=1; do not silently override it or other installed values.

## Next beta: separate extra-frame measurements

Add low-frequency pass-level measurement for extra frames: shadow,
reflection, main views, UI, and total extra draw. Use existing verified
marker hooks and the main render thread. Do not time each extra-frame mesh
or attribute extra time into regular-frame scopes.

Keep a separate bounded marker stack for this purpose. Only recognized
pass-level markers enter it. Unmatched markers and early returns reset
that frame's measurement and increment an invalid-sample counter, rather
than leak a half-open scope into the next draw. Reset on every extra-draw
entry/exit, including failure. Nested markers must not double count the
same pass. Preserve the existing shadow-matrix bookkeeping and safety
checks before and after the draw.

Append per-row extra-pass totals, sample counts and invalid-sample counts.
Never label a missing sample zero-cost. Keep existing columns stable.
Presentation-gap distributions should be collected in bounded integer
buckets, without a per-frame disk write. Record the number of extra shadow
draws so a higher presentation count cannot conceal reduced shadow cadence.

Test recognized/unrecognized markers, nested scopes, wrong-thread calls,
overflow, abandoned scopes and extra-draw safety failures. Run the full
plugin suite. Measure instrumentation overhead in the native harness; keep
it pass-level and outside per-mesh loops. Existing detailed measurements
remain available for comparison; do not disable the user's verification
settings as an undocumented performance shortcut.

## Full-pass optimization audit and promotion gate

Audit material Begin/BeginPass/EndPass/End transitions and renderOneObject
work against the read-only reference. Identify exactly which state is
unchanged before omitting or sharing work. Any material batching candidate
must preserve geometry, shader, texture, light/fog, bone, stream and draw
order semantics. Do not merge across a state boundary simply because model
names or effect pointers match.

Keep any discovered optimization separately switchable in a subsequent
beta. Require a reproducing native harness, restored device state,
reference/lifetime checks and equal output within the established GPU
comparison tolerances on native D3D9 and DXVK. Unknown shader/state cases
fall back to the game's path. Measure the same saved-game scene afterward;
do not claim 60 FPS from offline tests alone.

No lowering of deadline margins, bypassing catch-up/safety stops, camera
movement, x87/MXCSR changes, network/logic changes, new hardcoded install
addresses, copied third-party implementations or redistributed EA assets.
Commit locally and keep development on the opt-in beta branch. Installation,
publication and stable-channel changes remain separate authorized actions.
