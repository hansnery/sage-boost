# Performance investigation: what did not deliver the target

Evidence through 2026-10-19. Initially consolidated from development commit
`79d58c5`; subsequent experiments cite their own reports below.
Written at Hans's request after repeated reports of approximately 30 FPS.

**Beta.14 is the first user-confirmed heavy-scene improvement, around 55 FPS.
Hans accepts near-60 as a milestone and now targets 120 FPS.** The older
settled 59/s gate is unproven but no longer blocks this research. Earlier builds sometimes felt
smoother but remained around 30 in Hans's heavy tests. The new copied capture
supports a real increase in presentation calls. It does not establish a
fixed-scene 59/s result, an exact A/B gain or matching-beta multiplayer sync.

There is no controlled same-save before/after measurement that establishes
the size or cause of a gameplay improvement. That does not prove every
optimization is useless; it means we cannot claim the requested improvement.
The many intermediate builds and benchmark qualifications are not separate
successful optimizations or separate gameplay tests. Preserve their failed
or insufficient outcomes below; beta.14 does not retroactively qualify them.

This is the concise outcome record. [STATUS.md](STATUS.md) retains the
chronology; the linked reports retain measurements, controls and provenance.
Read this before reviving an old approach. Subsequent beta.16 captures and
the current beta.17 installation are recorded in STATUS.md and the
[installation receipt](benchmarks/2026-10-19-beta17-install.json). Worker experiments remain
offline harnesses, not installed game features.

## First partial gameplay success: paired cadence (beta.14)

The new policy relaxes the old per-frame admission margin and repays small
overruns by shortening later waits. It adds at most one extra draw per
ordinary update, with the existing shadow and state-check paths. This is a
change to scheduling, not another claim of cheaper rendering. It follows
route 2 of the approved timing experiment, without a complete 10/60 retime.

Production-object tests admit 1,800 extra draws in a controlled workload
where legacy admission allows none, preserving 1,800 ordinary updates in
about 59.4 virtual seconds. Tests also cover jitter, transient recovery,
persistent overload, eligibility and ownership. The full suite passes.
Those tests alone provide no gameplay result or FPS forecast; the earlier,
heavier cost model does not sustain 60.6 even with this policy. See the
[runtime report](benchmarks/2026-10-19-paired-cadence-runtime.json) and
[verification limits](specs/2026-10-19-paired-cadence-runtime.md).

Hans subsequently reports around 55 FPS in the heavy scene. The copied
capture's densest complete positive-logic/nonempty 60-second window contains
3,095 presentation calls (51.5833/s) and 1,295 extras with matching fresh-shadow
counts. Pacing p50 is (16.667, 20] ms; p95 remains (33.334, 50] ms. Workload
varies and one second contains a slowdown. Later higher rates occur with
much less visible geometry. This supports improvement without proving the
settled target. [Gameplay evidence](benchmarks/2026-10-19-beta14-gameplay-check.json).

120 FPS requires both more than the current one extra per update and enough
rendering headroom for three extras. The chosen window's aggregate ordinary
and extra costs illustrate a 44.59 ms four-draw workload against a 33 ms
native period. This is a rough budget screen, not predicted FPS; it does not
justify merely raising the scheduler limit or changing shadow quality.

## Target and evidence standard

The [approved target](specs/2026-10-17-60fps-full-shadows-design.md) is at least
59 presentation calls/s over a settled 60-second troop-heavy interval, with
pacing reported and fresh shadows on every rendered frame. Hans subsequently
[approved game-logic changes in the experimental beta](specs/2026-10-18-simulation-beta-design.md),
while retaining normal gameplay speed/balance, matching-beta multiplayer
sync, input/camera behavior and graphics quality. Earlier exclusions below
describe the former visual-only requirement; stable builds retain it.
The internal counter records completed flip-enabled `EndScene` calls, not
physical display refresh or proof of successful GPU output. FrameView and
visual checks remain necessary for the gameplay claim.

Most offline results below are milliseconds per **regular/extra frame pair**
in an isolated fixture. They are not milliseconds saved across the whole
game, and they cannot be converted directly into predicted gameplay FPS.
Lit and shadow fixtures are separate workloads. Do not add their savings or
the savings from successive versions of the same experiment.

## What happened in the game

These are checkpoints, **not a comparable A/B series**. Camera, troop poses,
scene and sometimes the save changed. A Realms update broke the original
save, requiring a replacement. Rates are internal calls per nominal second.

| Build / capture | Observed result | What it establishes |
|---|---|---|
| beta.1 | Hans: smoother, never above 30 FPS. The selected 91 rows contained only 23 extra frames. | Its recovery/cache changes did not solve the heavy scenario. The selected rows are not a settled 60-second measurement. [Record](STATUS.md#first-beta-gameplay-report-2026-10-17). |
| beta.2 | 1,539 calls / 51 s = **30.18/s**, zero extras. | Diagnostics confirmed cost blocking; the interval is shorter than the target. [Report](benchmarks/2026-10-18-gameplay-baseline-check.json). |
| beta.10 | 1,769 calls / 60 s = **29.48/s**, zero extras; FrameView maximum 30. | Installed batching also had a real eligibility bug, discussed below. [Report](benchmarks/2026-10-18-beta10-gameplay-check.json). |
| beta.11 | 1,804 calls / 60 s = **30.07/s**, six extras; screenshot showed 29 FPS. | Fixing eligibility restored instancing but did not deliver the target. [Report](benchmarks/2026-10-18-beta11-gameplay-check.json). |
| beta.13, recreated save | 1,817 calls / 60 s = **30.28/s**, four extras. | Multi-method batching was active. Four zero-logic rows make this unsuitable as a controlled unpaused target run. [Report](benchmarks/2026-10-18-beta13-gameplay-check.json). |
| beta.13, later live capture | Dense exploratory window: 1,795 calls / 60 s = **29.92/s**. Later readings exceeded 60. | Hans identified the high readings as a **lighter scene or different match**. They do not demonstrate success in the heavy save. [Report](benchmarks/2026-10-18-beta13-live-gameplay-check.json). |
| beta.13, latest "Still 30" report | Latest eligible window: 1,927 calls / 60 s = **32.12/s**, 113 extras. | Exact save/scene was not confirmed and workload varied within the window. This is neither a settled heavy-scene result nor proof of 32 displayed FPS. [Report](benchmarks/2026-10-18-beta13-still30-gameplay-check.json). |
| beta.14, user reports around 55 in the heavy scene | Densest eligible window: 3,095 calls / 60 s = **51.58/s**, 1,295 extras. | First user-confirmed improvement above 30; varying workload, a slowdown and no raw FrameView trace prevent a settled 59/s claim. [Report](benchmarks/2026-10-19-beta14-gameplay-check.json). |

The last beta.13 capture's pacing histogram places p50 in **(25, 33.334] ms** and p95
in **(33.334, 50] ms**. It recorded 1,155 cost blocks, no safety-stop blocks
and no recovery probes. This supports insufficient render headroom as the
recorded scheduling reason. Overlapping scope timings and one-second
averages cannot reconstruct each admission decision or prove the cost of an
extra frame that was never drawn.

## Installed changes that did not solve the heavy scene

**Learned-cost recovery and declaration-cache replacement (beta.1).** Earlier
0.9.10 evidence identified a self-sustaining learned cost floor and millions
of declaration-cache exhaustion fallbacks. Those were real reasons to
investigate. But the beta.1 heavy-save capture had a zero learned floor,
only 30 declarations and no evictions/failures. Neither targeted condition
was active there. Beta.2 also had a zero floor; its retained extra estimate
was below the regular-draw reference, so that reference could not decay it.
Do not repeat these fixes as the explanation for every 30 FPS result.
[Design and original evidence](specs/2026-10-17-perf-beta-design.md).

**Queue insertion, native-order sorting and bulk instancing (through
beta.10/11).** Successively more faithful offline fixtures retained sizable
combined savings: the beta.11 production-linked comparison reported about
8.18-8.66 ms/pair for shuffled queues and 6.52-7.14 ms/pair for ordered ones.
These are combined results from one evolving path, not additive gains from
each integration step. In the game, beta.10 incorrectly rejected eligible
anchor layouts: about 285 instanced meshes/frame and 7,127 mismatches.
Beta.11 fixed that, restoring about 7,040 instanced meshes/frame and zero
mismatches, yet the observed rate stayed around 30. The original fixture
missed a valid non-null anchor-storage case. Preserve the fix; do not treat
restored batching coverage as proof of an FPS gain.
[Integration](specs/2026-10-18-native-insertion-gpu-screen.md),
[anchor defect and fix](specs/2026-10-18-anchor-gate-fix.md).

**Verification-window admission and multi-method batching (beta.12/13).**
Beta.12 allowed unsampled groups to use bulk rendering while retaining the
verification quota; it was development-only. A forced verification-window
benchmark overstated its relevance to a capture that filled the quota in
one row. Beta.13 extended guarded batching to material chains of one to six
methods and added counters showing that followers were actually consumed.
The path ran in game, including multi-method groups, but the reported busy
interval remained about 30.28 calls/s. Coverage is not a measurement of time
saved against a disabled-bulk run.
[Verification-window limits](specs/2026-10-18-verification-window-bulk.md),
[material-chain integration](specs/2026-10-18-material-chain-runtime.md).

**Fresh-shadow and failure-recovery fixes.** Beta.3 corrected regular frames
reusing an extra frame's shadow map even with `ShadowInterval=1`. Stream
ownership and checked submission later fixed failure paths that could lose
units or leak device state. These preserve required output; they are not
independent speedups. Many beta.3-beta.9 steps were build/qualification work,
not separately installed gameplay experiments.
[Shadow correction](STATUS.md#further-screens-and-beta3-correction-2026-10-17),
[GPU recovery](specs/2026-10-18-native-gpu-recovery.md),
[stream ownership](specs/2026-10-18-instancing-stream-ownership.md).

## Smaller approaches rejected or insufficient on their own

All entries here are offline screens. A small positive result is not proof
of zero benefit, but it did not justify another standalone gameplay beta.

| Approach | Decisive result | Disposition / evidence |
|---|---|---|
| Native event construction/destruction | About 0.24-0.26 ms per 7,000 pairs. | Too little measured work to explain the target gap. [Source audit](specs/2026-10-17-updated-reference-performance-audit.md). |
| Effect-runtime/state-save replacement; ambient sampler | Runtime controls saved roughly 0.1-1 ms in fixtures; three particle command comparisons differed. Entire ambient sampler was about 0.05 ms. | No qualified runtime replacement. [Screen](specs/2026-10-17-render-setup-screening-results.md). |
| Bulk dispatch alone | About 1.4-1.7 ms submission saving at roughly 6,900 meshes, before omitted marker work. | Below the preselected 3 ms screen; marker work was not free to remove. [Dispatch](specs/2026-10-17-dispatch-benchmark-results.md), [marker accounting](specs/2026-10-17-marker-cpu-screen.md). |
| Retaining extra-frame GPU setup | DXVK submission saved 0.4-0.7 ms/pair, but completed work saved only 0.2-0.4 ms. Broader snapshots regressed. | No standalone promotion. [Screen](specs/2026-10-17-retained-batch-screen.md). |
| Full-precision palette textures for larger batches | Twenty-bone draw calls fell from 2,016 to 288 per fixture frame, but DXVK completed work **regressed 0.79-1.26 ms/frame** across six/twenty-bone lit/shadow cases. | Eight final correctness cases pass; no runtime promotion. Native gains are small/variable. [Screen and initial restoration failure](specs/2026-10-19-palette-texture-design.md#measured-outcome). |
| Native animation decoder reuse | Warm savings 0.154-0.367 ms per 24,576 calls; diverse phases could regress. | Cold-cache gains were not representative of the warm case. [Screen](specs/2026-10-18-native-decode-screen.md). |
| Queue cleanup | About 0.084-0.093 ms per 7,680 cleared entries. | Not enough to justify a hook. [Workload screens](specs/2026-10-18-workload-screens.md). |
| Matrix-source calculation | About 0.10 ms for 1,222 modeled groups across the measured matrix families. | Small, partly overlapping work. [Report](benchmarks/2026-10-18-native-matrix-sources.json). |
| Duplicate material-source requests | No exact duplicates in 152 observed annotation/semantic requests; default unit/shadow techniques did not request the proposed combined matrices. | The proposed redundant work was absent in this fixture. [Audit](specs/2026-10-18-material-source-demand.md). |
| Typed setter read-before-write filtering | About 0.60-0.68 ms lit saving; shadow ranged from 0.07 ms regression to 0.11 ms saving. | Too small alone. Unguarded filtering also broke parameter-block replay and could hide type conversions. [Screen](specs/2026-10-18-typed-write-screen.md). |
| Pruning effect members using `IsParameterUsed` | Used leaf members could report false; changing them changed commands on both tested runtimes/backends. | Incorrect pruning rule; rejected for output correctness. [Audit](specs/2026-10-18-effect-member-audit.md). |

The larger-palette screen exposed a fixture weakness: increasing from six to
20 bones raised the number of draws for 25 units to seven for both compared
paths. Correcting workload realism is not a new optimization.
The [installed-hook parity audit](specs/2026-10-18-installed-hook-parity.md)
likewise corrected a fixture that bypassed production Draw/DIP behavior; it
did not demonstrate an installed-plugin defect. Measured instanced shadow
submission alone was only about 0.20 ms/frame, much smaller than the enclosing
CPU shadow/render scopes.

## Material caches: apparent wins that shrank or reversed

| Experiment | What stronger controls showed | Decision |
|---|---|---|
| Initial material-queue cache | The apparent 3.2-3.9 ms/pair fell to 0.66-1.04 ms with real effect Begin/End, input checks and marker accounting. The original 504 samples with a competing game were excluded. | Early optimistic numbers are superseded. [Clean comparison](specs/2026-10-17-material-queue-clean-results.md). |
| Cache plus aggregate accounting | Fixed-clock results suggested about 3.0-3.5 ms/pair. Advancing `Time` reduced this to 0.61-1.08 ms; restoring required state at every hit produced a **0.26-0.87 ms DXVK regression**. | Do not promote the fixed-clock cache. [Initial combined screen](specs/2026-10-17-material-bulk-accounting-screen.md), [clock/state follow-up](specs/2026-10-18-material-cache-boundary-clock.md). |
| Device-state write filter | With the complete effect lifecycle, standalone DXVK regressed about 0.48-0.50 ms. Combined with bulk accounting it saved only 1.47-2.11 ms/pair. | No standalone runtime change. [Screen](specs/2026-10-18-effect-state-filter-screen.md). |
| Shared material cache with eight exact fixture keys | Against the already-bulk baseline, DXVK regressed about **0.79 ms lit / 0.97 ms shadow**. Native D3D9 saved about 1.80 / 1.67 ms. | Backend-specific native wins do not justify installing on DXVK. [Report](benchmarks/2026-10-18-shared-material-cache.json). |
| Capture once, replay three for 120 FPS | Setup-only DXVK saved 2.77/2.45 ms per four-draw cycle. Real effect lifecycle, exact keys, per-group restoration and a bulk baseline reverse that to **2.52/2.11 ms regression**. | Three-extra amortization does not rescue this implementation. All phase/material/state controls pass; no runtime promotion. [Results](specs/2026-10-19-120fps-retained-screen-design.md#results-and-decision). |

Revisit caching only with a demonstrated expensive repeated input, correct
time/identity/invalidation semantics, and a completed-work comparison against
the current baseline. Cache-hit counts alone are not a promotion criterion.

## Render threading: tested boundaries and their failures

These are isolated experiments. None established whole-game presentation,
live engine resource ownership or Realms multiplayer compatibility. Unless
stated otherwise, timings include queue waits and final GPU completion.

| Experiment | Result | Why it was not installed |
|---|---|---|
| Official Accelerator DLL, effects then actual geometry/fresh shadows | Standalone hash checks passed. Actual draw comparisons were near-neutral or drifted: lit ranged from 0.03 ms saving to 1.00 ms regression; shadow saved 0.32-0.57 ms in the two reported runs. | No substantial repeatable gain; effect/getter synchronization limited overlap. [Effects](benchmarks/2026-10-18-external-render-thread-screen.json), [draws](benchmarks/2026-10-18-external-render-thread-draws.json). |
| Avoiding external getter drains with fixed fixture replies | Drain requests fell from 2,887 to five. Apparent 2.43-3.18 ms savings were versus the disabled wrapper. Against a truly unhooked baseline, lit **regressed 0.28-0.41 ms** and shadow saved only 0.11-0.12 ms. | Wrapper overhead distorted the initial comparison; fewer drains did not prove worthwhile elapsed savings. [Report](benchmarks/2026-10-18-external-readbacks.json). |
| Independent worker owning a complete render batch | Copying cost 0.70-0.81 ms/pair before waits; completed work **regressed 0.13-0.63 ms** versus direct rendering. | Moving work to a thread did not create useful net overlap. [Report](benchmarks/2026-10-18-render-batch-worker.json). |
| Native matrix-source capture into worker packets | Producer capture about 0.52-0.55 ms before waits. Lit saved 0.07-0.33 ms; shadow ranged from 0.45 ms regression to 0.10 ms saving. | No repeatable improvement across required passes. [Report](benchmarks/2026-10-18-native-source-worker.json). |
| Synchronous native Begin/group handoff | **All 96 timing pairs regressed**, by about **13.37-17.84 ms/pair**. | Reject this handoff boundary. [Report](benchmarks/2026-10-18-native-group-consumer.json). |
| Producer-side effects, batches of 16 groups of device commands | DXVK **regressed 2.31-3.08 ms/pair**; native D3D9 saved only 0.10-0.73 ms. | Recording/capture cost outweighed overlap on the installed backend. [Report](benchmarks/2026-10-18-group-command-batches.json). |
| Bulk capture of group followers | Saved 0.91-2.36 ms versus the previous worker, but still **regressed 0.20-1.62 ms on DXVK versus native bulk rendering**. | Improving a slower prototype did not beat the relevant baseline. [Report](benchmarks/2026-10-18-group-bulk-capture.json). |
| Including the first unit in the worker batch | Reduced fixture draws from three to two per 25 units. Saved 0.95-1.82 ms versus the previous worker; versus native bulk, DXVK ranged from 0.26 ms regression to 0.55 ms saving. | No consistent material advantage over direct bulk rendering. [Report](benchmarks/2026-10-18-group-first-batch.json). |
| Merged/asynchronous frame start | Full matrix passed. DXVK ranged from **0.82 ms regression to 0.91 ms saving** versus the prior worker, and 0.86 ms regression to 0.94 ms saving versus native bulk. Native D3D9 improved. | No consistent DXVK gain. Final completion still serialized frames. [Full report](benchmarks/2026-10-18-group-frame-start.json). |
| Deferring GPU completion across 2/4/8 frames | Under the same final-completion policy, DXVK worker-versus-native ranges from **0.48 ms regression to 1.21 ms saving per pair**. The native path itself saves 0.34-1.55 ms when its per-frame waits are removed. | Completion policy helps the baseline too; it is not a threading-only gain or a measured game improvement. CPU frame execution remains serialized. [Report](benchmarks/2026-10-18-group-completion.json). |

The [capture profile](benchmarks/2026-10-18-group-capture-profile.json) found
about 3.17-3.34 ms of native producer traversal versus 0.39-0.56 ms of queue
publication/wait overhead. Instrumentation itself added 0.13-0.61 ms.
Profiling identified where time went; it was not a performance improvement.

Some worker fixtures **did** show positive synthetic results:

- [Pose preparation overlap](benchmarks/2026-10-18-native-pose-worker.json)
  saved about 0.75-1.94 ms/pair, but used a synthetic 43,200-decoder workload
  per draw, not a measured live animation hierarchy.
- [Window/device ownership](benchmarks/2026-10-18-window-owner-worker.json)
  with copied material values saved about 1.48-1.98 ms/pair. The real device
  creation omitted `MULTITHREADED`; Reset cannot add it. Evaluating material
  expressions on the worker produced a one-bit opacity mismatch, requiring
  producer-evaluated values instead.
- [Reset/resource lifetime](benchmarks/2026-10-18-worker-reset-lifetime.json)
  and [packet-consumer isolation](benchmarks/2026-10-18-independent-packet-consumer.json)
  passed fixture correctness checks. These are prerequisites, not additional
  FPS gains or proof of safe whole-game threading.

These observations justify investigating specific overlap mechanisms; they
do not establish that threading as a whole has failed or succeeded.

## Approaches not tested, or excluded by the requested behavior

- **SAGE Unlocked in Realms multiplayer is not independently qualified.**
  The documentation review recorded the author's 1.9.4 mixed-FPS/stock-peer
  sync fix claim and 1.9.5 corrections. Hans's earlier unit-skipping report
  did not identify a release or establish a network desync. We must not
  label the claimed sync fix disproved. The official archive was staged,
  not executed; its `dinput8.dll` loader conflicts with the faction unlocker.
  [Review and upstream links](STATUS.md#direction-change-evaluate-external-render-threading-2026-10-18).
- **Accelerator in a real match is untested here.** The official DLL ran in
  isolated harnesses only. Its reviewed README described multiplayer as
  untested; defaults also changed frame scheduling, camera and faction AI.
  A default installation would not isolate rendering performance.
  [Release inventory](benchmarks/2026-10-18-alternative-release-preflight.json).
- **MetaIdea's 60 FPS mod updater was reviewed, not executed (2026-10-18).**
  It extracts and rebuilds mod archives, changes the FPS settings and rescales
  gameplay data: speed factors by 0.5 and time factors by 2 at its default
  60 FPS. This includes weapons, movement, build times and cooldowns. It is
  a separate mod conversion, not evidence of reduced rendering cost or
  unchanged logic. Compatibility with unconverted Realms clients is not
  established. The author discontinued it in favor of SageMetaTool. That
  successor's wiki also describes scaling engine values and rewriting
  gameplay INI values during parsing; it does not establish unchanged logic
  or lower rendering cost. Neither tool was executed here. Hans's subsequent
  approval allows investigating retiming in our own experimental beta; it
  does not establish either external tool's performance or compatibility.
  [README](https://github.com/MetaIdea/bfme2-60fps-mod-updater-tool),
  [factors](https://github.com/MetaIdea/bfme2-60fps-mod-updater-tool/blob/main/ExecuteModDefinitionScript.lua#L1-L5),
  [conversion rules](https://github.com/MetaIdea/bfme2-60fps-mod-updater-tool/blob/main/ModDefinitionScript.lua),
  [SageMetaTool's mechanism](https://github.com/MetaIdea/SageMetaTool/wiki#how-does-the-60-fps-engine-proper-gamespeed-fix-work-exactly-).
- **Producer/worker CPU overlap across successive frames is not implemented.**
  The new [completion control](specs/2026-10-18-frame-completion-screen.md)
  allows bounded pending GPU work, checks every intermediate image and gives
  both paths equal final completion. It still waits for CPU command execution
  after each frame. Removing that remaining wait needs owned per-frame results
  and resources; simply dropping it would race shared state.
  [Completion-boundary audit](specs/2026-10-18-native-source-worker.md).
- **Skeleton threading and extra shader coverage were deprioritized, not
  disproved.** Measured skeleton work was about 1.54 ms in the first heavy
  capture; missing shader counts were small relative to instanced meshes.
  Neither was supported as a sufficient standalone route to doubling FPS.
  [Original workload evidence](STATUS.md#first-beta-gameplay-report-2026-10-17).
- **Reducing shadow rate, moving the camera or breaking multiplayer sync
  does not meet this target.** Hans chose
  shadows at 60 too. Earlier camera smoothing also caused drift and a black
  strip by bypassing native camera/shadow/terrain updates; it was removed in
  0.9.10. Do not revive it as a performance fix. [Rules](../AGENTS.md).

The first [native rate-setter check](benchmarks/2026-10-18-native-rates.json)
accepts 10 Hz logic / 60 Hz client on both installed executable variants and
rejects deliberately stale output scales. A 5/60 request is clamped to 5/30.
Calling 5/30 also changes two startup conversion constants by one ULP;
recomputed rates are not an exact rollback and can affect duration rounding.
Only that isolated native leaf ran. Startup-derived timers, rounded duration
fields, the complete loop and matching-peer sync still need qualification;
no runtime switch was installed and no gameplay improvement was measured.

The subsequent [native duration test](benchmarks/2026-10-18-native-durations.json)
confirms those rounding effects with explicit scanner/CRT/error fixture
dependencies. Doubling original rounded integer ticks preserves their old
elapsed duration where storage permits; narrow overflow is refused. Actual
mod field coverage and runtime integration are still open. This is timing
correctness groundwork, with no measured gameplay or FPS improvement.

The [paired-cadence policy screen](benchmarks/2026-10-18-paired-cadence.json)
then investigates deadline admission without doubling game updates. In a
closed loop with controlled costs, reducing reserved idle time and repaying
small overruns admits 60.6 modeled presentations/s at the latest aggregate
anchors, but only 45.45 at the heavier anchor and 30.3 under overload. No
game or GPU ran; this is not a capture replay or FPS forecast. Runtime
estimator/backoff, hook ownership and game-state gates remain to integrate.
The 4 ms allowance is a prediction bound, not a cap on unexpected draw time.
Nothing is installed from this policy test alone.

## What future attempts must do differently

1. **Start from a measured game bottleneck and the current baseline.** Do
   not spend another cycle on a small cache or a faster version of a still
   slower worker without evidence that it can close a meaningful part of
   the gap on the installed backend.
2. **Keep completed-work timing and representative state.** Include producer
   traversal, copying, fresh palettes/shadows, advancing time, effect
   Begin/End, required restoration, waits and final GPU completion. Retain a
   truly unhooked control when external wrappers are involved.
3. **Separate correctness, coverage and speed.** Exact pixels, balanced
   references, rejection of faults and active batching are necessary, but
   none proves a faster game. The anchor bug and failure-recovery defects
   show why representative fallback and failure cases matter.
4. **Do not cherry-pick loading or light scenes.** Fix the replacement save,
   camera, settings and versions for a controlled comparison. Report a
   settled interval and pacing, not maximum FPS. Overlapping scope times
   and different experiments must not be summed into a predicted rate.
5. **Verify the loaded binary.** The launcher replaced beta.13 with published
   0.9.10 twice; the [first](benchmarks/2026-10-18-beta13-restoration.json) and
   [second](benchmarks/2026-10-18-beta13-second-restoration.json) restorations
   preserve receipts. This explains particular invalid version comparisons,
   not all failures: verified beta.13 also remained around 30.
6. **Keep competing GPU work out of offline comparisons.** Interrupted or
   contaminated runs are excluded, not averaged into the result. Record
   instrumentation overhead and the comparison's limits.
7. **Respect Hans's test threshold.** Ask for another gameplay test only
   after a substantive, repeatable offline improvement with relevant
   correctness coverage. Do not ship another diagnostic-only build as a
   likely FPS breakthrough. Gameplay evidence is still required before
   calling the 60 FPS objective achieved.

No runtime build or settings change accompanies this retrospective. The
unmet performance objective and unresolved compatibility work remain in
[BACKLOG.md](BACKLOG.md).
