# Status and handoff (2026-10-18)

Where the three repositories and the installed game stand, for a model or
person picking the work up. Rules, build steps and branches are in
[AGENTS.md](../AGENTS.md); the Realms side (factions, AI, files outside the
repo, backlog) is in the Realms repo's `data\AGENTS.md`; what the docs can't
settle is in [BACKLOG.md](BACKLOG.md). Re-check anything below against the
files before relying on it.

## Next target: 60 visual FPS with shadows at 60 (2026-10-17)

### Direction change: evaluate external render threading (2026-10-18)

The user requests reassessing BFME2 Accelerator and SAGE Unlocked after the
repeated ~30 FPS gameplay results. Small material/cache experiments are no
longer the primary next step. Study documented approaches and stage external
releases separately; the repository's independent-code and multiplayer rules
remain in force. No alternative is installed into or executed with the game.
Accelerator has now run in isolated offline harnesses (below).

- Accelerator 2.0 queues D3D work to another thread, which directly addresses
  a plausible part of the measured CPU rendering cost. Its shipped README
  explicitly says multiplayer is untested. Its default configuration also
  changes frame scheduling, camera presentation and faction AI; a default
  install is not a render-thread-only experiment. See the
  [upstream README](https://github.com/OhadBaehr/bfme2-accelerator/blob/main/packaging/README.txt)
  and [settings](https://github.com/OhadBaehr/bfme2-accelerator/blob/main/packaging/bfme2_accel.default.ini).
- SAGE Unlocked's author reports a multiplayer-order fix in
  [1.9.4](https://github.com/TheeHorse/SAGE-Unlocked/releases/tag/v1.9.4)
  and subsequent logic corrections in stable
  [1.9.5](https://github.com/TheeHorse/SAGE-Unlocked/releases/tag/v1.9.5).
  The 1.9.4 claim explicitly includes different render rates and a peer
  without the mod: stock logic order and game speed should remain equal.
  Our investigation has reviewed documentation and reports; it has not
  independently validated that claim in a Realms multiplayer match.
  Hans's October 12 [multiplayer report](https://github.com/TheeHorse/SAGE-Unlocked/issues/20)
  describes smooth scrolling but units visibly skipping. It does not
  specify the tested release or report a desync, and provides no sustained
  mixed-client sync measurement. Treat unit presentation and network sync
  as separate questions. Hans's
  [ROTWK effect-speed report](https://github.com/TheeHorse/SAGE-Unlocked/issues/32)
  remains open; it also does not establish a multiplayer sync failure.
  SAGE remains a candidate, not a demonstrated Realms sync failure or a
  validated replacement. Its BFME loader occupies `dinput8.dll`, already used by our
  faction unlocker. Do not overwrite that component to try it.
- Official Accelerator 2.0 build 73 and SAGE Unlocked 1.9.5 ZIPs are staged,
  hash-verified at
  `%TEMP%\sage-boost-alternatives-20261018-v0jfrn7o`; `receipt.json` records
  URLs, archive hashes and member names. A tracked
  [preflight receipt](benchmarks/2026-10-18-alternative-release-preflight.json)
  preserves that inventory. SAGE Unlocked remains unextracted/unexecuted;
  Accelerator's DLL is extracted only into the separate harness directory.
  No external implementation was imported into sage-boost.
- Next: determine whether render threading can be isolated and qualified
  without changing simulation, input or camera, and without overlapping
  competing hooks. Default frame interpolation is not proof of fresh
  shadows at 60. Keep separate stock, beta and alternative measurements;
  resolve a reproducible multiplayer divergence only after one is observed.
  No promised FPS gain, deployment or new gameplay request from this review.

### Independent render packet consumer (2026-10-18)

The optional worker now draws copied palettes through the existing checked
submission engine with its own shader, declaration cache, instance buffer
and counters. It leaves the original timing, instancing and extra-frame
state on the producer. The earlier fixture's thread-ID transfers are not
needed in this mode. While the worker holds an effect open, the producer
executes frame-hook callbacks and flushes diagnostics without changing the
native instancer's totals or leaving stale scopes. These are fixture
callbacks, not the real game's frame or logic functions.

Both executable variants on DXVK and native D3D9 pass **240 exact image/state
cases, 24 real resets and eight zero-reference final releases**. All 72
deliberate fault processes reach the intended checks, including 16 that
restore the old timing-owner transfer or shared extra-frame flag. The
[report](benchmarks/2026-10-18-independent-packet-consumer.json) records
unchanged original-worker hashes across another 104 image/state cases.

This establishes independent fixture state, not a live rendering bridge or
multiplayer qualification. Native FX-group capture, Begin/pass results,
concurrent resource ownership, failure propagation and full scene/Present
remain open. The new consumer has no timing result; previous worker savings
cannot be assigned to it. Production sources and installed beta.13/INI hashes
are unchanged. The full suite passed at `9121da8`; this extension reruns the
affected optional worker paths. No installation or gameplay test follows.

### Worker reset and resource lifetime checks (2026-10-18)

The optional worker fixture now closes publication with two draws pending,
collects and drains them, releases effect/state/cache/default-pool resources,
and joins the worker before resetting the device on its window thread.
Successful reset advances a resource generation, recreates resources and
restarts drawing; checksum-valid old-generation packets are rejected.
Final device Release must return zero on the window thread. Correcting the
fixture's old shader/surface reference leaks was necessary to pass that test.

Both local executable variants pass on DXVK and native D3D9: **24 actual
resets, 240 exact image/state cases and eight zero-reference final releases**.
The [report](benchmarks/2026-10-18-worker-reset-lifetime.json) records 56
deliberate fault processes, including early teardown and omitted generation
checks, plus 24 stale-generation and 48 closed-admission refusals. Failed
Reset HRESULTs are injected; actual device loss/fullscreen recovery is not
tested. Original owner and batch workers preserve their hashes in another
44 image/state cases.

The full `perf/` regression suite passes, including both native executable
contracts and the instancing GPU fixture. Installed beta.13 and INI hashes
still match the existing restoration receipt.

This qualifies owned fixture resources, not the game's lifecycle. Native
teardown hooks, concurrent asset lifetimes, separate frame/render accounting,
effect/Begin contracts and full scene/Present remain open. No new timing or
gameplay FPS claim, installation or gameplay-test request follows.

### Window/device ownership and copied material values (2026-10-18)

Both local executables' complete native Create_Device bodies omit the
MULTITHREADED flag. A worker using the existing game window therefore needs
a verified early creation change; Reset cannot add that flag. The previous
beta.13 log shows the ASI loaded before `d3d9.dll` on that startup path.

The new optional owner fixture initializes its hidden window and device on
the producer with MULTITHREADED, then transfers exclusive drawing to the
worker while the producer pumps messages. It exposed separate timing and
instancing thread-ID assumptions, plus a one-bit opacity-state mismatch
from evaluating the material expression on the worker. Test-only ownership
transfers and copied producer-evaluated material values address these fixture
failures. No floating-point control workaround or production change is made.

Both executable variants pass 96 exact image/state cases, 16 deliberately
wrong outputs, four missing-flag refusals and 16 alias/capacity controls each.
Each positive process dispatches all 590 ordered worker messages on its
window thread. The [report](benchmarks/2026-10-18-window-owner-worker.json)
retains 168 three-mode samples: completion savings are 1.4908-1.9790 ms/pair
lit and 1.4836-1.7123 ms shadow. This includes copied inputs, message handling,
multithreaded device locking and GPU completion, but remains synthetic.
Three original worker variants pass another 68 image/state cases and ten
output faults with unchanged reference hashes. Installed beta.13 and its
INI remain hash-identical to the existing restoration receipt.

Native Reset_Device begins releasing resources before its D3D Reset call;
the eventual queue must drain before that teardown, not merely before Reset.
Live frame/render bookkeeping must also be separated; the fixture's exclusive
thread-ID transfer is not a runtime implementation. Resource recreation,
effect/Begin contracts, full scene/Present and gameplay FPS remain unproved.
No installation or new gameplay test follows from this experiment.

### Native pose preparation overlapping rendering (2026-10-18)

The optional owned-worker fixture now executes verified native vector
decoders to prepare the bone positions that its draws consume. Direct mode
performs preparation and drawing serially on the worker; queued modes
prepare on the producer and copy the results. All paths use the same native
work and output, with no external accelerator DLL. Completed-work timing
includes copies, backpressure and GPU drains.

Across both images and lit/shadow workloads, advancing a warm decoder cursor
by one saves **1.4112-1.9442 ms per synthetic regular/extra pair** against
direct completion. A lower-work zero-advance case on `game.dat` saves
0.7457-1.2160 ms. These are medians of paired differences, not caller-only
timings. Producer preparation takes 2.4055-2.4579 ms/pair for advance one and
1.8517-1.9204 ms for advance zero, before separately recorded waits.

Twelve positive processes pass **144 exact packet image/state cases**, with
18 deliberate setter/stale-source/stale-pose faults caught, 24 borrowed
buffer refusals and 24 capacity/recovery controls. Independent decoded-versus-
raw images establish that the native work affects the picture. Original
matrix-worker and batch-worker regressions pass 24 and 20 image/state cases.
The [report](benchmarks/2026-10-18-native-pose-worker.json) preserves all
252 three-mode samples, numeric native manifests and input/log hashes.

This establishes useful overlap in this fixture, not a gameplay improvement.
Its 43,200 vector decodes per draw use synthetic channels and warm cursors;
neither the count nor cursor distribution comes from gameplay. Decoded next
endpoints feed geometry without animation interpolation or hierarchy
composition. Full scene, resource/effect lifetimes, Begin/pass dependencies
and live device/window/Reset integration remain open. Lit and shadow deltas
cannot be added or converted to FPS. No production code changed; the full
suite passed at the preceding `da85d4e`, and this extension used the optional
worker regressions above. Installed beta.13 and INI hashes still match.
Continue integration; no installation or new gameplay request is justified.

### Readback waits and the unhooked baseline (2026-10-18)

The new optional `bench_external_readbacks.exe` compares external threading,
fixed-effect caller replies, both together, and the external wrapper with
both disabled. A separate `native` process never loads the external DLL
and compares unhooked rendering against the same replies. The latter is
essential: disabling the external worker does not remove its wrappers.

Fresh CommitChanges probes show that shader, declaration, stream, frequency,
index and constant getters each request a drain. Earlier probes let the first
getter hide the later waits. Shader AddRef/Release do not request a drain.
The controlled replies reduce drain requests from 2,887 to five per pair;
these counters do not measure elapsed waiting time. The fixture knows its
shader, stable bindings and exact bone-parameter/register mapping. An initial
incorrect assumption that the instance registers were unused by the original
effect failed the independent state oracle despite matching pixels. The
corrected version includes full bone-parameter updates and real COM references.

Two final repeats per workload pass **144 exact image/state cases** across
external and unhooked processes, eight deliberately poisoned-answer controls,
and changed-pose/material checks. Preserve **192 timing samples** in the
[report](benchmarks/2026-10-18-external-readbacks.json). In the external
four-mode comparison, combined completion saves 2.98-3.18 ms lit and
2.43-2.51 ms shadow against the disabled wrapper. Adding threading to the
fixed replies contributes only 0.15-0.28 ms lit and 0.08-0.35 ms shadow.
Against the truly unhooked path, fixed replies instead regress lit by
0.28-0.41 ms and save only 0.11-0.12 ms shadow. These are medians of paired
differences; they cannot be reconstructed by subtracting separate medians.
Workload/time drift also prevents treating separate processes as a paired
comparison. The earlier multi-millisecond delta is not established as a
gain over unhooked rendering and must not justify a deployment.

The full `perf/` regression suite passes. Installed beta.13 and INI hashes
match the existing restoration receipt. No runtime change, install or
gameplay request. Full native sources,
resource/failure/Reset handling and complete-frame costs remain unqualified.
Further threading work must beat a genuinely unhooked baseline and account
for real producer work; do not extend a compatibility layer based only on
the external wrapper's enabled/disabled comparison. SAGE Unlocked remains
a separate candidate under the multiplayer evidence distinctions above.

### Native source capture into worker packets (2026-10-18)

The offline worker now receives actual native world/view/projection callback
results as copied typed commands. Callbacks run on the producer; only the
worker owns effect handles and issues GPU work. The fixture poisons provider
storage before publication and scratch storage after it, checks changed
matrix generations against an independent reference, and rejects deliberate
wrong-setter and stale-source faults.

Both executable variants pass lit/shadow checks twice: **96 exact image/state
cases**, eight deliberate output faults, 16 borrowed-buffer refusals, 16
queue-capacity/recovery controls and 16 executable-contract refusals. Each
successful run executes 509,760 native matrix callbacks. The original worker
also passes 20 image/state regression cases with unchanged reference hashes.
The [report](benchmarks/2026-10-18-native-source-worker.json) retains 168
three-mode timing samples and earlier superseded pilot provenance.

Producer preparation takes **0.5218-0.5536 ms per synthetic pair**, excluding
5.4178-6.0864 ms collection/backpressure in the saturated workload. Paired
completion savings versus the direct fixture are 0.0687-0.3292 ms lit and
-0.4462 to +0.0966 ms shadow. This is a qualified slice of capture, without a
substantial repeatable speedup or a gameplay FPS result. Matrices and poses
still originate in controlled fixtures; other source families, native object
and resource lifetimes, effect identity/Begin results and live device/Reset
ownership remain open. See the
[scope and reproduction](specs/2026-10-18-native-source-worker.md).

The complete `perf/` test suite passes. No runtime change, installation or
new gameplay request. Installed beta.13
and INI hashes still match the restoration receipt. Continue the threading
integration; sustained 60 FPS remains unproved.

### Owned render-batch worker (2026-10-18)

An independently written offline worker now owns all effect/state/GPU calls
and consumes two bounded slots of copied skeletal palettes and synthetic
material inputs. No live engine objects or external implementation are
transferred. Six runs pass 60 exact image/state cases, 12 borrowed-buffer
negative controls and 12 full-queue/recovery controls; 126 three-mode samples
are retained in the [report](benchmarks/2026-10-18-render-batch-worker.json).

Producer preparation takes **0.7024-0.8115 ms per synthetic regular/extra
pair**, excluding separately measured queue/collection waits. Against a
direct no-palette-copy baseline, completion regresses 0.1288-0.2633 ms lit
and 0.2262-0.6285 ms shadow. This establishes a bounded handoff, not faster
rendering or game FPS. Its possible benefit is overlapping other caller
work; the fixture does not execute real simulation or native data capture.
Full-payload integrity mode adds about 5.9-6.0 ms versus direct rendering;
all ownership/image controls remain checked in both timing modes.

The native lifecycle contract identifies the next integration work:
readiness, source callbacks and native ownership must remain on their
original thread; copied effect values/resource identities need a qualified
handoff, including Begin/pass results and changing effect identity. The
fixture's worker-created device also does not solve live window/Reset
ownership. See [scope and next boundary](specs/2026-10-18-render-batch-worker.md).
No runtime change, installation or new gameplay request. Beta.13 and its INI
hashes still match; sustained 60 FPS remains unproved.

### External render-thread screen (2026-10-18)

The matching upstream harness for official Accelerator 2.0 build 73 runs
offline against the installed DXVK/D3DX27 dependencies. All **600 frame
hashes match** between off/on runs; its log confirms the worker executes.
The harness toggles threading every 50 frames, so total elapsed time cannot
measure sustained threaded throughput. Its enqueue microbench is also not
a full-frame benchmark.

An independently written optional adapter, `bench_external_rt.exe`, now
loads that external DLL by explicit path and exercises the local unit and
shadow effects. Eight in-pass/post-End comparisons match an unhooked
backend oracle, with changed-input and deliberate constant-poison controls.
The oracle bypasses mirrored getters and drains queued resource operations.
No external implementation, shader bytecode or rendered image is committed.

Two runs preserve **84 paired samples**. Median drained-completion savings
per 1,222 synthetic groups are -0.1158/-0.0824 ms lit and -0.4526/+0.2162 ms
shadow. This no-draw lifecycle workload shows no substantial benefit; it
does not reject the wider architecture. Geometry, fresh shadow images,
complete frame submission and coexistence with production hooks remain
unqualified. See the [report](benchmarks/2026-10-18-external-render-thread-screen.json)
and [scope](specs/2026-10-18-external-render-thread-screen.md). No new
performance build or gameplay test is justified yet.

### External draw/shadow and synchronization checks (2026-10-18)

The follow-up adds real geometry and fresh shadow maps to the external
comparison, using our existing 288-group, 7,200-mesh GPU fixture. All 24
final image/state cases match the unhooked oracle exactly. Both changed
poses and changed materials visibly affect the reference output. The same
bulk-after-second prototype runs on both timed paths; this is still not
the installed beta.13 native integration or a complete scene.

Four final runs preserve 84 paired samples: lit completion savings are
**+0.0339/-0.9983 ms**, shadow **+0.3208/+0.5722 ms** per modeled regular/
extra pair. Workload times drift substantially between runs; 84 earlier
samples are retained separately. These isolated passes cannot be added or
converted into an FPS estimate. No substantial repeatable gain justifies
installing the alternative or requesting a game test.

A separate black-box diagnostic identifies synchronization boundaries:
GetVertexShader after queued effect work and GetVertexShaderConstantF
after another CommitChanges each increment the external drain counter in
all eight lit/shadow rounds. Later getters can reuse the earlier drain;
zero then does not mean they are always asynchronous. Drain counts do not
measure waiting time or attribute all benchmark drains.

The instancer uses these queries to capture the current shader and preserve
constants. Investigate submitting a complete render batch, with its state
queries on the same worker, before assuming drop-in threading can overlap
enough CPU work. Immutable inputs, ownership, worker ordering and game-loop
boundaries need proof; do not move live engine objects across threads on
the strength of this fixture. See [draw and barrier evidence](benchmarks/2026-10-18-external-render-thread-draws.json).
Installed beta.13 and INI hashes still match the restoration receipt.

### Shared material cache: do not promote (2026-10-18)

Twelve sequential native D3D9/DXVK lit/shadow runs preserve exact output,
fresh poses, changed-input refusal, lifecycle restoration and collision
controls. Sharing eight fixture keys avoids most per-group recaptures, but
against the already-bulk prototype the DXVK completion medians regress by
0.7895 ms lit and 0.9704 ms shadow per pair. Native D3D9 saves 1.7980 and
1.6710 ms. These are separate modeled workloads, not additive game savings.
The stricter baseline still does not contain the full beta.13 native path.
Preserve the [252 samples and hashes](benchmarks/2026-10-18-shared-material-cache.json)
and reject standalone runtime promotion. Production and installed beta.13
ASI/INI are unchanged; no additional game test is warranted.

**Current installed build: 0.9.11-beta.13**, from pushed commit `5e94796`,
restored again 2026-10-18 17:35 PDT after replacement by 0.9.10.
Tested/build/installed ASI SHA256:
`976f529feab68574dce9bcc2c77af030554fdb29f3e17274bf68cdcff92cdcdd`.
The current INI had lost only the QueueInsert/QueueSort/BulkRender block.
Restoring that block preserves every existing byte and recovers the exact
previous preferences (SHA256 `e5dd4a0ce274f4adc6ad6472dfc9a63017e31b8e076e48f05e24c8c6f758837b`).
The replaced binary, INI and capture are backed up at
`%TEMP%\bfme2perf-0.9.10-installed-20261018-173552-501397`.
The beta shortcut's CheckOnly verifies both hashes and launches nothing.
See [latest restoration receipt](benchmarks/2026-10-18-beta13-second-restoration.json),
the [earlier restoration](benchmarks/2026-10-18-beta13-restoration.json) and
the [original installation](benchmarks/2026-10-18-material-chain-install.json).

## Beta.13 gameplay: active batching, insufficient render budget (2026-10-18)

The user reports high initial FPS followed by a drop to 30. This new run
loads **0.9.11-beta.13**, with the tested ASI and intended INI hashes above;
QueueInsert, QueueSort and BulkRender activate. The closed-game capture is
`%TEMP%\bfme2perf-beta13-test-20261018-162847-140777`. See
[measurements and provenance](benchmarks/2026-10-18-beta13-gameplay-check.json).

- Busy seconds 94..153 contain 1,817 completed presentation calls over
  60 nominal seconds: **30.2833/s**, with four extra frames. Pooled gap p50
  is `(25, 33.334] ms`, p95 `(33.334, 50] ms`. All four extra profiles include
  a fresh shadow pass, and all four verification-error counters are zero.
- This interval contains zero logic ticks at seconds 99..102, consistent
  with a brief pause/inactive simulation. It is not a controlled unpaused
  60-second benchmark. The new save also differs from earlier captures;
  do not claim an A/B performance gain or regression between versions.
- There are 1,576 cost-blocked frames, 61 catch-up blocks, zero pause/stop
  blocks, zero recovery probes and zero extra-frame lateness in this window.
  Initial/transitional late draws outside it receive only grace-period
  pauses. The sustained drop is not a permanent safety shutdown.
- Normal frame time averages 23.2837 ms; measured idle time is 9.8502 ms
  per wait. The final extra-cost estimate is 12.261 ms; with the existing
  3 ms + cost/8 margin and 0.5 ms slack, the admission requirement is
  17.293 ms. Row averages cannot reconstruct individual scheduling choices,
  and that estimate is not a measurement of later unseen extra frames.
- Bulk consumes 9,302,622 regular follower records (5,125.41 per update),
  with **40.71% in multi-method groups**: counts 1/2/3 are
  5,515,536 / 3,752,113 / 34,973; counts 4..6 are zero. This proves use of
  the beta.13 path, not time saved or a share of every eligible record.
- Rendering averages 17.0886 ms, FX batches 9.8458 ms, FX mesh scopes
  4.7737 ms, skeleton render 1.3586 ms and instanced shadow submission
  0.2144 ms. These scopes overlap. Remaining FX group/material setup and
  unbatched work are the next CPU investigation; do not weaken scheduling
  guards or revisit small/rejected filters as if they solved the deficit.

No new runtime build, settings change or gameplay request follows from
this capture. The 60 FPS target remains unmet. Beta.13 stays installed.

### Previous run used the published plugin

The user recreated the save after a Realms update and again reported a
FrameView maximum of 30 FPS. The copied run at
`%TEMP%\bfme2perf-new-test-20261018-161755-746488` identifies **0.9.10**,
with 1,078 timing rows and no presentation-gap or bulk-follower counters.
It therefore does not measure beta.13. Do not infer presentation rate from
`frames + extra_frames` or claim that the beta failed/passed this run.
That run was superseded by the genuine beta.13 capture above.

Launcher source confirms every Play can repair a locally replaced plugin
against its installed release state and renders INIs from cached templates.
Use the desktop **Realms - sage-boost beta test** shortcut for the next
local test. It invokes `tools/launch_perf_beta.ps1`, pins binary and INI
hashes, requires the game/launchers closed, and starts the same Nightly mod
path directly. It refuses changed files instead of repairing them. Its
CheckOnly success, wrong-binary, wrong-settings and missing-mod cases pass;
the actual launch remains for the user. No game was launched, and no
launcher state, published release or stable branch was changed.

The capture analyzer reports beta.13 follower totals by method count,
with separate regular/extra frame denominators. All 20 checks pass and
the copied beta.11 interval reproduces every prior metric unchanged.
The new capture establishes live coverage and the rendering bottleneck;
results from old and recreated saves are not a controlled comparison.

- User requested committing/pushing current work, then a target of at least
  60 FPS. Clean beta history through `ace76a5` pushed to origin/codex/perf-beta.
- Explicit preference: keep shadows at 60 too; no shadow-cadence shortcut.
- Approved next-stage design:
  [60 FPS with full-rate shadows](specs/2026-10-17-60fps-full-shadows-design.md).
- Beta.2 adds extra-frame timings and the full-pass setup audit is complete.
  The saved-game capture is complete (below). The combined budget needs
  roughly 8 ms of savings in
  the captured heavy scene. This is a target, not an achieved result.
- Latest user constraint: request another gameplay test only after evidence
  of a significant improvement; no diagnostic-only install/test cycle.

## Native matrix-source computation screen (2026-10-18)

- Nine verified native bodies now execute offline: matrix/inverse callbacks,
  world/view/projection providers and the matrix/array/row constructors.
  D3DX27 performs the actual inverse and singular fallback; recording
  setters isolate computation from effect storage and rendering.
- Both executable variants pass 54 full-matrix output/identity/change/
  singular checks, eight contract refusals and a wrong-provider negative
  control. Only local image bytes in private RAM are executed.
- Twenty-one samples per workload/image put world + view + projection +
  view inverse at about **0.10 ms per 1,222 modeled groups**. This is too
  small for a standalone computation cache; actual live callback counts
  are unknown. The three reported workloads overlap and must not be added.
- [Raw samples, hashes and limits](benchmarks/2026-10-18-native-matrix-sources.json).
  D3DX storage/lifecycle, other light/skeleton sources and native binding
  remain separate costs. The next candidate must remove substantially more
  material processing without repeating the rejected full-state cache.
  Beta.13 ASI/INI hashes are unchanged; no gameplay request or deployment.

## Material-chain dispatch screen (2026-10-18)

- Beta.13 now promotes the bounded 1..6-method follower guard and appends
  normal/extra consumed-follower counters by method count. Full-suite,
  instruction and all sixteen production-linked GPU recovery runs pass;
  six selected replay corruptions are rejected. Fresh paired completion
  savings are 3.4243 ms lit and 2.6242 ms shadow per synthetic normal/extra
  pair. See [runtime decision and limits](specs/2026-10-18-material-chain-runtime.md).
- Beta.13 is committed, pushed and installed with the receipt above. The
  subsequent gameplay capture establishes live chain coverage but remains
  around 30 FPS; no 60 FPS claim follows from the fixture.

- Recovery qualification now passes sixteen normal runs and rejects 22
  deliberate corruptions. The final checks require nonempty images and
  observable child shader/state behavior, native owning links, partial
  fallback, GPU replay and a successful group after failed Begin. Earlier
  empty-target and misordered-argument runs were discarded. See
  [qualification](specs/2026-10-18-material-chain-recovery.md).
- The recovery qualification was developer-only; the subsequent runtime
  promotion above supersedes that checkpoint. The installed ASI/INI hashes
  still matched beta.11 when the final validation report was generated.

- The copied log's cumulative shadow-state summary leaves zero comparator
  splits after merging 6,119. This does not identify every live group, but
  it removes support for an opacity change as the next optimization.
- Native grouping compares every material method. The installed follower
  bulk dispatcher accepts only one-method groups; leader deferral already
  works independently. Leader counters do not establish follower coverage.
- A developer-only fixture switches this gate in one executable while
  keeping native grouping, hooks, leader deferral, FX state and draw counts
  identical. Controlled owning chains of length 1..6 retain exact boundaries
  and references. Production and installed beta.11/INI are unchanged.
- Sixteen normal-path runs pass, six deliberate faults are rejected, and
  168 paired samples show 2.29-4.75 ms median completion savings per
  synthetic regular/extra pair. This warrants runtime qualification, not
  a saved-game FPS claim. Preserve full method-array boundaries.
- Qualify native child behavior, fallback/failure and live coverage before
  promotion. See [experiment](specs/2026-10-18-material-chain-bulk.md).

## Beta.12 verification-window fix: development only (2026-10-18)

- Share the existing sample-selection predicate between evaluation and bulk
  admission. Selected groups remain native; unsampled groups no longer lose
  bulk solely because a verification window is open. Quotas/timing are intact.
- The policy regression and both-image native D3D9/DXVK lit/shadow controls
  pass, along with the full suite. Preserve old-source controls and timing
  samples; separate old/new runs show drift in unchanged extra-frame work.
- The real capture fills all six samples within one reporting row per
  window, so the forced-window benchmark does not establish a substantial
  saved-game gain. Keep beta.11 installed, with its INI unchanged; no new
  gameplay test request. The last real result remains 30.0667/s.
- A later log audit finds zero remaining shadow-state comparator splits in
  its cumulative summary; investigate material chains before opacity. The
  public three-factor opacity getter remains reference information only;
  no opacity merging or new runtime signature has been implemented. See
  [scope, evidence and limits](specs/2026-10-18-verification-window-bulk.md).

## Typed material-write screen: standalone route rejected (2026-10-18)

- The follow-up uses the typed setter families rather than generic SetValue
  for supported fields. Final native D3D9/DXVK medians save 0.60-0.68 ms in
  the modeled lit workload; shadow deltas (-0.07 to +0.11 ms) straddle zero.
  This does not justify native-hook integration or another standalone beta.
- Nonzero typed round trips, asymmetric matrices, poisoned GPU constants,
  changed poses, recording bypass and invalid-handle HRESULT controls pass.
  The unguarded filter breaks parameter-block replay. Getter equality can
  also hide type conversion, demonstrated with a fractional float/GetInt.
- Preserve 84 samples and source/input hashes. Source/member audit modes
  pass on both backends. Native source computation and full GPU integration
  remain unqualified; installed beta.11/INI and the last 30.0667/s gameplay
  result are unchanged. See [screen](specs/2026-10-18-typed-write-screen.md).

## Material-source write screen (2026-10-18)

- Actual local effect metadata has no exact duplicate annotation/semantic
  requests across 152 technique/runtime/backend observations. The default
  unit and shadow techniques do not request the combined WorldView or
  WorldViewProjection semantics; caching those is not supported here.
- A developer-only read-before-write numeric filter retains changing bone
  palettes and saves 0.79-1.05 ms per modeled 850-group lifecycle workload,
  including live getter/byte-comparison costs. Hostile constant/parameter
  controls pass and detect deliberately stale values. Preserve 126 samples.
- This is below the standalone promotion gate, with generic field setters
  and no geometry or native source computation. The later typed-setter
  screen above reduces the expected benefit; full native callback/lifetime
  and GPU qualification remain open. Do not extrapolate to saved-game FPS.
- Member-audit regressions pass. The existing default runtime comparison
  still exits 2 with three known D3DX27/D3DX43 differences; it is not a
  runtime-replacement compatibility pass. Production/install/settings are
  unchanged; last actual gameplay remains 30.0667 presentation calls/s.
  See [audit](specs/2026-10-18-material-source-demand.md).

## Larger-palette and queue-cleanup screens (2026-10-18)

- Twenty-bone native/GPU workloads now exercise the production hooks and
  derive draw counts from shader capacity. Both-image native D3D9/DXVK
  lit/shadow controls, six-bone DXVK regressions and the full plugin suite pass.
- At twenty bones, 25 units take seven draws either with an ordinary leader
  or fully batched. Existing bulk dispatch still helps, but an additional
  draw reduction cannot be assumed. Optimized pair costs rise from roughly
  7-9 ms at six bones to 15-16 ms at twenty in this controlled workload.
  These are not saved-game FPS or a newly implemented speedup.
- A separate queue-cleanup candidate passes 192 normal-return controls per
  image/run, including exact callback observations and a broken-release
  negative control. Three runs save only 0.0836-0.0926 ms per 7,680-record
  clear. Reject runtime promotion; exception/reentrancy remain unqualified.
- Preserve 252 cleanup and 126 GPU timing samples with hashes. See
  [results and reproduction](specs/2026-10-18-workload-screens.md).
- Installed beta.11 and the INI are unchanged. Last actual gameplay remains
  30.0667 presentation calls/s. No installation or new gameplay request.
  Native material-source computations remain a fixture gap and the next
  CPU-rendering investigation; source-pruning/cache shortcuts stay rejected.

## Installed-hook parity audit after beta.11 (2026-10-18)

- Runtime GPU fixtures now use the actual Draw/DIP site definitions and
  signature installer, plus `pf_bulk_install` for the group entry/loop.
  Native callers, entry trampolines and indirect device lookup execute.
  The earlier fixture bypassed those differences; no ASI defect is proved.
- The failure fixture now passes its proxy through the native device slot
  instead of overwriting the indirect-call displacement with a function
  pointer. The latter failed under the production metadata.
- Both-image native D3D9/DXVK checks pass: eight combined workloads, 384
  fallback cases, 496 GPU-failure cases, 56 broken recovery controls and
  24 broken queue controls. This closes fixture coverage, not the FPS goal.
- The existing capture's shadow mesh accounting suggests bulk leaders
  already run for many groups, although it is not a direct bulk counter.
  Actual instanced shadow submission costs only 0.2014 ms/frame; the larger
  target remains CPU rendering work around it. Zero local lights explain
  the zero reuse counters. Do not add overlapping scopes.
- Production source, beta.11 ASI and the installed INI are unchanged. No
  diagnostic-only installation or gameplay request. Next workload checks
  should include twenty-bone models seen in the live log, instead of only
  the six-bone synthetic model. See
  [audit](specs/2026-10-18-installed-hook-parity.md) and
  [validation](benchmarks/2026-10-18-installed-hook-parity.json).

## Beta.11 gameplay: batching recovered, 60 FPS still failed (2026-10-18)

- User's screenshot shows FrameView **29 FPS**. Game and launcher were
  already closed. Copied the latest log, INI and CSVs before reading to
  `%TEMP%/bfme2perf-beta11-test-20261018-094455-443892`.
- Beta.11 loaded with QueueInsert, QueueSort and BulkRender activated.
  Installed binary and unchanged INI hashes still match the install receipt.
- Seconds 98..157 contain **1,804 presentation calls / 60 seconds =
  30.0667/s**, six extra frames, and six completed extra shadow samples.
  Pooled median gap is `(25, 33.334] ms`, p95 `(33.334, 50] ms`; all four
  logged visual-verification failure counters are zero. Boundary row 158
  exists. Earlier empty-scene/menu rows and gaps are excluded.
- The anchor fix restored instancing: about **7,040 meshes/frame**, zero
  mismatches. Normal frames average **22.457 ms**, render **14.146 ms**,
  shadow **7.162 ms**, FX batch **7.417 ms** and FX mesh **3.689 ms**.
  Scopes overlap; do not add them. Available idle averages **11.083 ms**.
  Cost blocks 1,134 opportunities; catch-up blocks 134. No recovery probes
  ran. The six extra samples at seconds 100/101 do not characterize the
  later heavier scene.
- Scene/camera/pose vary, so these costs are not a controlled before/after
  speedup measurement. The same run exceeds 30 in early near-empty scenes;
  it is not evidence of a universal 30 FPS presentation cap.
- Startup activation and ordinary instancing counts do **not** establish
  bulk-loop coverage: beta.11 does not report its separate leader/consumed
  counters. Audit that coverage and remaining regular-frame CPU work next;
  preserve deadline margins, fresh shadows and all gameplay invariants.
  No further binary/settings change or new test request from this capture.
  Evidence: `benchmarks/2026-10-18-beta11-gameplay-check.json`.

## Earlier beta.10 failure and beta.11 installation (2026-10-18)

- User reports FrameView maximum 30 FPS in the troop-heavy save. Copied log,
  INI and CSVs to
  `%TEMP%/bfme2perf-beta10-test-20261018-091513-787491` before analysis.
  Beta.10 loaded and all three new optimizations activated.
- Seconds 50..109 contain 1,769 presentation calls: **29.4833/s**, zero
  extras. Median/p95 gap bounds both `(33.334, 50] ms`; verification errors
  zero. The 60 FPS objective failed. See
  `benchmarks/2026-10-18-beta10-gameplay-check.json`.
- Only about 285 meshes/frame were instanced, with 7,127 mismatches/frame.
  The new +0x310 check rejects non-null anchor storage even when the native
  pointee is null or self and the world transform remains identity. The
  earlier fixture covered only null storage. New admission and native/GPU
  cases fail with beta.10, reproducing that gap.
- Beta.11 corrects all admission/bulk gates and distinguishes relative
  anchors in the log. Nineteen admission/ownership cases, expanded native
  D3D9/DXVK checks for both images, the full suite and general-register
  instruction inspection all pass. See
  `specs/2026-10-18-anchor-gate-fix.md` and its implementation plan.
- Six serial DXVK runs with mixed anchors retain **8.18-8.66 ms** median
  savings per regular/extra pair for shuffled input, **6.52-7.14 ms** for
  ordered input. This compares the complete optimized path with ordinary
  instancing in a controlled workload; it is not a gameplay FPS estimate.
- Tested beta.11 ASI SHA256:
  `574a085967d477a53037526c1ea7f6b6d257a559a36aab7f76573c4ca7cfe027`.
  Evidence: `benchmarks/2026-10-18-anchor-gate-checks.json` and 126 raw
  samples.
- Committed/pushed as `7d17e24` on `codex/perf-beta`, then installed at
  2026-10-18T09:35:16 local time. The installed binary matches the tested
  hash above. Stable branches remain unchanged. The game was closed.
- Previous beta.10 ASI, INI, log and both CSVs are backed up to
  `C:\Users\Hans\AppData\Local\Temp\bfme2perf-0.9.11-beta.10-installed-20261018-093515-940132`.
  The INI is byte-for-byte unchanged, SHA256
  `e5dd4a0ce274f4adc6ad6472dfc9a63017e31b8e076e48f05e24c8c6f758837b`.
  See `benchmarks/2026-10-18-anchor-gate-install.json` for the receipt.
- The subsequent beta.11 saved-game result above is **30.0667/s** and does
  not meet the objective. Require at least 59/s over a settled 60-second
  interval with reported pacing and fresh shadows before claiming 60 FPS.

## Earlier beta.10 installation and offline results (2026-10-18)

- **0.9.11-beta.10** connects verified startup discovery, native group
  entry/loop hooks, production instancing and aggregate timing. New
  `[Render] BulkRender=1` requires Instancing and all prerequisite hooks.
  See [runtime group hook](specs/2026-10-18-runtime-group-hook.md).
- Group return/unwind releases captured stream ownership. Seven lifetime
  cases include actual Windows unwind, nested/early rendering and failed
  transactions. Incomplete/incoherent GPU failures disable optimization and
  explicitly report the failed frame; production never terminates the game.
- Both images pass actual private-image hook installation, native/GPU
  fallback and failure controls, combined ordering/output checks and the
  full plugin suite. General-register instruction checks pass.
- Six serial DXVK runs through the production wrapper/bridge retain
  **7.40-7.92 ms** median savings per regular/extra pair for shuffled input,
  and **6.05-6.12 ms** for ordered input. 126 raw samples and hashes are
  preserved. This is substantial offline improvement, not gameplay FPS.
- Built ASI SHA256:
  `b936df66dbae8cdf0197a5c4c3331328809eed1f2e238a8f8763ee0362428f5f`.
- Committed and pushed as `ebe5681` on `codex/perf-beta`,
  then installed at 2026-10-18T08:56:44 local time. The installed
  ASI matches the tested build hash above. Stable branches are unchanged.
- Previous ASI, INI, log and both CSVs backed up to
  `C:\Users\Hans\AppData\Local\Temp\bfme2perf-0.9.10-installed-20261018-085644-035758`.
- Every existing INI value and its ASCII/LF format is preserved. Added only
  commented `[Render] QueueInsert=1`, `QueueSort=1`, and `BulkRender=1`.
  Installed INI SHA256:
  `e5dd4a0ce274f4adc6ad6472dfc9a63017e31b8e076e48f05e24c8c6f758837b`.
- The subsequent troop-heavy saved-game capture failed at 29.4833
  presentation calls/s; see the beta.10 gameplay section above. No game was
  launched by the agent. The objective remains at least 59 presentation
  calls/s over a settled 60-second interval, with pacing reported.

## Gameplay capture analysis prepared (2026-10-18)

- `tools/perf_capture.py` reads a copied timing CSV without changing it.
  It requires an explicit interval and a following boundary row, rejects
  missing seconds/counters, and pools presentation-gap bins for median/p95
  bounds. Sixteen tests cover misleading averages and incomplete evidence.
- Rechecking beta.2 seconds 50 through 100 reproduces 1,539 completed
  flip-enabled EndScene calls over 51 nominal seconds: **30.1765/s**,
  no extra frames. Median gap is `(25, 33.334] ms`; p95 is
  `(33.334, 50] ms`. This 51-second baseline is shorter than the acceptance
  interval. See `benchmarks/2026-10-18-gameplay-baseline-check.json`.
- This tooling was prepared before the beta.10 capture reported above.
  Its report does not measure physical display refresh or prove
  visual/network/camera behavior; those remain separate acceptance checks.
  No success claim follows from this tooling work.

## Production bulk instancer built (2026-10-18)

- Beta **0.9.11-beta.9** moves leader eligibility, palette capture, checked
  submission and the bulk record loop into production `inst.c`. Normal
  combined GPU tests execute that unmodified source; the adapter delegates
  to it. See [bulk instancer](specs/2026-10-18-runtime-bulk-instancer.md).
- Both images pass 384 native fallback cases, 496 single-GPU-failure cases
  and 56 broken device-effect controls. Eight combined queue/GPU workloads
  preserve native ordering, output and ownership; 24 broken-record controls
  are rejected. Twelve ownership cases, 19 submission cases and the full
  plugin suite pass. General-register instruction checks pass.
- Built ASI SHA256:
  `e70deacb549d4b74e9db63853c02aa0987419e3025ad5bcea6af977753bb9c3a`.
  Source/build/log hashes and test scope are preserved with the report.
- The implementation is linked but remains disabled: integrate startup
  discovery, the actual native group hook, aggregate timing and explicit
  abandonment/Reset/incomplete-replay failure policy next. No installation
  or fresh performance measurement; last gameplay remains 30.18/s.

## Shared checked submission module built (2026-10-18)

- Beta **0.9.11-beta.8** moves skeletal batch setup/restoration and ordinary
  replay into `instsubmit.c`. The native/GPU adapter and plugin link the
  same compiled object; the adapter no longer has its own transaction copy.
  See [checked submission](specs/2026-10-18-checked-submission-core.md).
- Results distinguish coherent device state from complete unit submission,
  retain committed/replayed counts and identify the first failed operation.
  Nineteen direct cases cover persistent restoration errors, partial replay,
  bounds, null streams, multiple streams and nested-call refusal.
- Both images pass 384 native fallback cases, 496 single-GPU-failure cases
  and 56 deliberately broken device-effect controls across native D3D9/DXVK
  lit/fresh-shadow workloads. The full plugin suite passes; remaining normal,
  fault and recovery benchmark targets also compile with the shared object.
- Production core and executed native fallback adapter instruction checks
  find no x87/SIMD/FP-control instructions. Built ASI SHA256:
  `0fb6048a801d22d0b37e1daf24d1741a288b9de3afdb594b5c1f5d26eea3b68c`.
  Source/build/log hashes and test scope are preserved with the report.
- The module is linked but the runtime instancer does not call it yet.
  Integrate the verified native group bridge, caller ownership/cleanup and
  incomplete/incoherent failure policy next. No installation or new timing;
  last gameplay remains about 30.18 presentations/s, and 60 FPS is unproved.

## Captured stream buffer ownership fixed (2026-10-18)

- Beta **0.9.11-beta.7** keeps the successful GetStreamSource reference
  until the final batch restoration, then releases it on terminal group
  transitions. The old immediate release could destroy the buffer during
  temporary unbinding. See [stream ownership](specs/2026-10-18-instancing-stream-ownership.md).
- The original behavior fails a deterministic lifetime assertion. The fix
  passes eleven cases plus three real buffer lifetime cases per D3D9/DXVK
  backend. Both executable families pass 384 native fallback cases, 496
  single-GPU-failure cases and 56 broken controls across eight workloads.
- The full plugin suite passes. Production instancing and the offline
  recovery object use no x87/SIMD/FP-control instructions. Built ASI SHA256:
  `53efaaa0e0018c403d66913c1b8a639d573208e643e86d3c91cfb8a4653f0f8b`.
  Source/build/log hashes are preserved with the report.
- Installed ASI/INI unchanged; no new timing or gameplay test. The latest
  combined offline saving remains 6.17-7.96 ms per regular/extra pair; last
  actual gameplay remains about 30.18 presentations/s.
- Continue production checked submission/bulk integration. Interrupted
  groups, Reset/device loss, native exceptions and ordinary-replay errors
  still need a runtime policy; this closes normal stream ownership only.

## Bulk-render runtime discovery verified (2026-10-18)

- Beta **0.9.11-beta.6** extends the shared verifier for larger bodies,
  dependency addends and semantic-region bounds/roles. The new bulk finder
  joins 36 hashed regions, 85 identities and 280 dependency fields under
  six signature roots. See [bulk runtime discovery](specs/2026-10-18-runtime-bulk-contract.md).
- Both relocated private images pass 451 no-write refusals, four synthetic
  region controls and discovery after actual insertion/sort hook installation.
  AotR lacks OriginalFirstThunk; resolved IAT entries are checked against
  exports in the already-loaded CRT, not discarded import-name pointers.
- Full plugin suite and general-register instruction checks pass. Built
  beta.6 ASI SHA256:
  `9c8b59837d01735e0b8bd5f094785836ca406b37a4f6c75719d8fd4506dd5e54`.
  Check summaries and source/build hashes are preserved with the report.
- The bulk finder is currently linked only into its private-image test.
  No bulk runtime hook, installation or gameplay request. Latest performance
  evidence is unchanged; last gameplay remains about 30.18 presentations/s.
- Continue production bulk integration, including staged/resource ownership
  and persistent/device/replay failures. The retained `g.prev_vb` issue is
  addressed by beta.7 above; interrupted-render cleanup remains open.

## Native-order sorting integrated into the runtime (2026-10-18)

- Beta **0.9.11-beta.5** adds `[Render] QueueSort=1`. An integer-only C
  sort preserves native ties while keeping queue ownership throughout raw
  record moves. Only two verified pure comparators qualify; unsupported
  inputs call the original. No native sort bodies are cloned at runtime.
  See [runtime queue sorting](specs/2026-10-18-runtime-queue-sort.md).
- Sorting and insertion share the fingerprint/dependency verifier. Both
  images pass 936 native sort comparisons and private-image installation in
  both orders. Per image/order: 528 entry cases plus 456 forced-heap cases,
  including shared holders. Callback/reference traces, ten preflight guards,
  105 sort refusals and unchanged insertion checks pass.
- Full plugin suite, additional shared-holder checks, final build/load and
  eight GPU cases pass; 24 corrupt-record controls fail the expected oracle.
  Two shuffled DXVK repeats retain 7.39-7.96 ms combined savings per pair;
  ordered input saves 6.17-6.22 ms. Preserve 126 samples and hashes.
- Built ASI SHA256:
  `a925172cf19dd7fa950d79d2a969bbb5aedf936572b76a671ebbc1f823cfe137`.
  Installed ASI/INI and stable branches remain unchanged; no gameplay request.
- Next integrate bulk rendering and remaining lifetime/device/replay guards.
  Only insertion/sorting are now in production; combined fixture timing does
  not establish gameplay FPS. Last gameplay remains ~30.18/s, target open.

## Queue insertion integrated into the runtime (2026-10-18)

- Beta **0.9.11-beta.4** adds `[Render] QueueInsert=1`: fill available
  rendering-queue storage directly; growth/unsupported inputs stay native.
  A unique signature and 33 complete normalized body checks guard the hook.
  See [runtime queue insertion](specs/2026-10-18-runtime-queue-insertion.md).
- Both images pass 576 comparisons through the production handler and 576
  through the actual installed entry in private RAM, plus 92 no-write
  runtime refusal cases each. Full plugin suite and eight joined GPU cases
  pass; all 24 corrupt-record controls are rejected.
- Two shuffled DXVK repeats retain 7.54-7.80 ms combined savings per pair;
  ordered input saves about 6.13 ms in one run. Preserve 126 timing samples
  and source/build/control hashes. These replace earlier combined results.
- This build contains the insertion optimization only. Sorting/bulk rendering
  remain offline and require runtime integration and remaining failure/lifetime
  guards. No installation/INI changes or gameplay request. Last gameplay is
  still ~30.18/s; sustained 60 FPS is unachieved.
- Built ASI SHA256:
  `3dd6207681f93389f34e40c412f46988e4b31fb9a365c1989c107fa302f9212c`.
  Next integrate sorting, then the guarded bulk path, before installing the
  complete improvement and requesting the user's saved-game test.

## Deferred units recovered at native fallback (2026-10-18)

- [Native group fallback](specs/2026-10-18-native-group-fallback.md) resolves
  zero bulk consumption with ordinary draws of pending staged units,
  palette restoration and timing-marker cancellation. Completed batches
  are not repeated; the current record and remaining group render natively.
- Both images pass 384 fallback cases across native D3D9/DXVK lit/fresh
  shadows, including partial writes, batch boundaries and following groups.
  All 496 earlier single-GPU-failure cases and 56 broken controls pass.
  Joined insertion/GPU regression and full plugin suite also pass.
- Two shuffled-input DXVK repeats retain 7.20-7.53 ms savings per pair;
  ordered input saves 5.89-6.16 ms in one sensitivity run. Preserve 384
  fallback observations and 126 timings/hashes. This replaces the earlier
  combined screen, not an additional saving or a gameplay FPS result.
- No production/installed/INI changes. Last gameplay remains ~30.18/s.
  Errors during ordinary replay, persistent/device-loss behavior, native
  exceptions and source/lifetime/context limits remain unqualified.
- Next audit the runtime boundaries and build signature-verified guards
  for the qualified paths, preserving native ownership/method/pass limits
  and callback/verification exclusions. Resolve device-failure behavior
  before installing the complete improvement; 60 FPS is still unachieved.

## Native insertion joined to sorting and GPU rendering (2026-10-18)

- [Insertion/GPU screen](specs/2026-10-18-native-insertion-gpu-screen.md)
  executes 33 verified insertion/growth/erase bodies. A direct append uses
  spare capacity, preserving mesh/method ownership and all three selector
  branches; full capacity, oversized stacks and destination aliases fall
  back to native insertion. Heap callbacks are controlled; exceptions stop.
- Both images pass 576 CPU comparisons, five altered-code refusals and a
  corrupt-record control each. Both also pass the joined GPU lit/fresh-
  shadow checks on native D3D9 and DXVK, with 24 broken-record controls.
  The previous queue/GPU regression and full plugin suite pass.
- Two serial shuffled-input runs save **7.14-7.32 ms/pair on DXVK** with
  actual insertion and native erase in the workload. Ascending input saves
  5.99-6.21 ms in one sensitivity run. CPU insertion saves 0.80-0.86 ms per
  7,680-record workload; these isolated numbers are not additive. Preserve
  294 GPU and 252 CPU timings with source/build/control hashes.
- No production/installation/INI changes or gameplay request. Last measured
  gameplay remains ~30.18/s. The roughly 8 ms engineering target and the
  real 60 FPS target remain unachieved; actual input order is unknown.
- Next replace the native group bridge's zero-consumption assertion with
  ordinary rendering fallback that resolves any deferred leader exactly
  once. Qualify source/lifetime/context limits and guarded runtime hooks;
  the current fixture cannot yet be installed as a complete improvement.

## Owned queues, native sorting and GPU integration (2026-10-18)

- [Joined queue/GPU screen](specs/2026-10-18-native-queue-gpu-integration.md)
  collects owned records, sorts three modeled pass ranges, renders those
  exact ranges through the native group/mesh/FX path, then releases them.
  The rendering bodies retain target-relative placement in a shared arena.
- Both images pass lit/fresh-shadow controls on native D3D9 and DXVK:
  eight cases with exact records/ties, ordered groups, object/engine/effect
  state, references and ABI; sixteen corrupt/duplicate-record controls
  fail the required oracle. Earlier group and GPU recovery checks pass.
- Two serial shuffled-input runs save 5.42-5.84 ms/pair on DXVK. Sorting
  and rendering isolation runs each save about 2.6-2.8 ms; an ascending
  sensitivity run saves 4.48-4.51 ms combined. Preserve 378 timings/hashes.
  These replace adding isolated screens; actual queue order is unknown.
- Full plugin suite passes. No production/installation/INI changes and no
  gameplay request. Last gameplay remains ~30.18/s; 60 FPS is unproved.
- Next screen the verified native insertion/resize boundary with available
  queue capacity: it builds/copies/destroys a temporary record per mesh.
  Queue selection, ownership and growth fallback must match. Actual
  insertion, source/lifetime/exception and runtime bridge work remain open.

## Native sorting and effect-member screens (2026-10-18)

- [Native sort screen](specs/2026-10-18-native-sort-screen.md) preserves
  exact native order while removing temporary record-reference traffic.
  Both local images pass 936 comparisons, ABI/ownership/guard checks,
  four altered-code controls and a corrupt-copy control each.
- Two final serial runs preserve 756 timings with the target's relative
  function layout. One-method workloads save 0.51-2.04 ms per synthetic
  7,680-record sort workload; random 128-key input saves 1.44 / 1.41 ms.
  Earlier separate-page timings are excluded. No installed hook or FPS
  result; do not add or double this isolated saving into a gameplay claim.
- [Effect-member audit](specs/2026-10-18-effect-member-audit.md) rejects
  leaf `IsParameterUsed` pruning: both runtimes/backends mark an actually
  used member false, while changing it changes emitted commands. All 38
  local techniques are audited; parent filtering remains necessary.
- Next combine sorting with native queue/group rendering and the guarded
  GPU candidate, including command/order, lifetime and fallback checks,
  then measure combined costs. Further source computation remains open.
- No production, installation or INI changes and no gameplay request.
  Last gameplay remains ~30.18 presentations/s; 60 FPS is unachieved.

## Native animation decode screen (2026-10-18)

- [Exact decoder reuse](specs/2026-10-18-native-decode-screen.md) loads three
  verified native bodies into private RAM and tests immutable synthetic
  streams. Both images pass 8,909 direct and 294,912 workload comparisons,
  ABI/guard checks, six altered-code controls and three broken-cache controls.
- Two serial runs preserve 504 timings. Favorable warm cases save only
  0.154-0.367 ms per synthetic 24,576-call workload; diverse phases can
  regress. Large cold-decode savings do not represent steady playback.
  No runtime promotion or additional gameplay saving is established.
- Native AddBinding already tests selected-technique usage, but sometimes
  tests a parent parameter. Next inspect actual effect member usage and
  shadow scene setup before choosing another optimization. General unused
  input filtering alone is redundant; member-level opportunities are open.
- No production, installation or INI changes. Last gameplay remains ~30.18
  presentations/s; the 60 FPS goal is unchanged and unachieved.

## Native FX/state GPU integration (2026-10-18)

- [Native material lifecycle and state](specs/2026-10-18-native-fx-gpu-integration.md)
  adds nine verified bodies to the joined group/mesh path. Real D3DX calls,
  source selection and dirty vertex/index binding now run through native
  code; the complete fixture has 25 verified bodies in private RAM.
- Stronger snapshots found a legacy unbound-stream offset leak. The checked
  candidate restores offset/stride before unbinding. The failure reference
  now draws all meshes natively; no state mismatch is masked as acceptable.
- All 248 single-rejection cases pass pixels, counts, immediate GPU state,
  effect parameters and fixture engine fields. Sixteen broken controls are
  rejected, including omission of the new offset restoration. Both local
  images pass the full-body verifier and five altered-state controls each.
- Two final serial timing repeats save 2.68-2.93 ms/pair on DXVK; preserve
  168 timings, 240 batch-fault rows, eight capture cases and hashes. This is
  the existing candidate under stronger tests, not another additive gain.
- Full plugin suite and prior GPU recovery regression pass. Native source
  computation, lifetimes, unsupported failure/rigid/exception paths and the
  larger performance gap remain open. Next inspect source computation and
  expensive render work for additional savings before requesting gameplay.
- No production/installed/INI/stable changes. Last gameplay remains ~30.18
  presentations/s; sustained full-rate 60 FPS is still unproved.

## Offline skeletal GPU recovery (2026-10-18)

- [Checked batch recovery](specs/2026-10-18-native-gpu-recovery.md) now
  preserves output, immediate GPU state and exactly 25 units across all
  224 earlier one-shot batch rejection cases, plus eight failures before
  deferring the leader. Missing/duplicate replay and corrupt-count controls
  are rejected on all four backend/workload combinations.
- This candidate snapshots the leader's bone registers, checks setup and
  restoration, and replays only unsubmitted staged poses with the original
  shader. Successful batches are never replayed after restoration errors.
  It is build-only; production `inst.c` still has the audited behavior below.
- Two serial timing repeats retain 2.90-3.06 ms/pair on DXVK. Preserve 168
  timing samples, 224 fault rows and source/binary/log hashes. This retains
  the previous saving; it is not additive and still falls short of 60 FPS.
- Full plugin suite and original four-case fault audit pass. Persistent or
  multiple faults, device loss/reset, rigid recovery, native source/state/
  lifetime/exception behavior remain unqualified. Next close native source
  and engine-state reconciliation for replay and find the missing savings.
- Built beta.3, installed 0.9.10 and INI hashes remain unchanged. No
  installation or gameplay request; last measured cadence is still ~30.18/s.

## Native GPU fault audit (2026-10-18)

- [Rejected-call audit](specs/2026-10-18-native-gpu-fault-audit.md) records
  224 observations across native D3D9/DXVK, lit/fresh shadows and regular/
  extra frames. Each 56-case set detects 28 changed outputs and 28 immediate
  state leaks; 52 remain enabled despite an observed error. Rejected DIPs
  disable instancing but lose staged units. This describes the unmodified
  instancer; the later offline recovery candidate is summarized above.
- Clean original/candidate images and counts match with separately placed
  meshes; immediate device-state snapshots catch leaks hidden by later
  restoration. Native ownership/ABI/scope checks and deliberately broken
  oracle controls pass. A separate proxy leaves the driver's vtable intact.
- This changes the next action from an unspecified failure proof to explicit
  staged-draw ownership, checked setup/restoration and no-duplicate replay.
  Effect/engine state and lifetime reconciliation remain necessary. No
  installation, gameplay request or additional performance claim.
- Full plugin suite and all four joined native/GPU regressions pass. The
  fault audit passes by detecting the documented defects, not by recovering
  from them. Built beta.3, installed 0.9.10 and installed INI hashes match
  their prior values.

## Joined native group/GPU screen (2026-10-18)

- [Native group and mesh together](specs/2026-10-18-native-group-gpu-screen.md)
  now exercise sixteen verified bodies, real diagnostic events and method
  ownership, actual timing hooks and the local effect/GPU path. Two serial
  repeats save 2.78-2.99 ms per pair on DXVK; 168 samples/hashes preserved.
- Each backend/workload passes 136 original/candidate pairs plus a real
  verification window: callbacks and draw observations, empty/small groups,
  native/partial splits, earlier DIPs, Begin failure, state, references and
  ABI. Both local images still pass the separate native group contracts.
- Source/geometry/engine-state helpers remain controlled. GPU failure
  recovery, lifetimes and native exception handling are still open; the
  bridge aborts the offline fixture for an unexpected zero-consumption
  attempt rather than claiming a production fallback. No runtime promotion.
- This strengthens the existing roughly 3 ms result; it is not additive,
  still falls short of the full-rate 60 FPS budget, and warrants no new
  gameplay request or installation.
- Full plugin suite and all four earlier mesh/GPU cases pass after the
  shared fixture changes. Built beta.3, installed 0.9.10 and installed INI
  hashes remain unchanged; the bridge object uses general registers only.

## Native mesh/GPU integration screen (2026-10-18)

- [Guarded native-mesh GPU screen](specs/2026-10-18-native-mesh-gpu-screen.md)
  replaces the modeled mesh callee with seven verified native bodies and
  real timing.c return wrapping. Two serial repeats save 2.81-2.95 ms per
  regular/extra pair on DXVK. Preserve 168 samples and build/log hashes.
- Regular/extra output, fresh shadows, omitted-leader detection, state,
  references and logical counts pass. Additional controls cover callback
  gates, a model split/partial batch, an earlier source DIP and the real
  verification window. Shared native contract tests pass on both images.
- Enclosing group/events, source computation and engine-state application
  remain controlled; native group/GPU integration and failure recovery are
  still required. This retains the earlier candidate's saving, not an
  additive improvement or evidence of 60 FPS. No install or gameplay test.
- Full plugin suite and all four prior leader/bulk GPU cases pass. Built
  beta.3, installed 0.9.10 and installed INI hashes remain unchanged.

## Native final-draw contract (2026-10-18)

- [Native mesh/draw fixture](specs/2026-10-18-native-mesh-draw-contract.md)
  verifies seven complete bodies (3,032 bytes) and switch/data/call roles.
  Both local images pass 38,016 native executions, eleven verifier refusal
  controls and two deliberately broken final-draw guards.
- The exact mesh-call stack identity distinguishes the final draw from
  recursive same-mesh calls, whose ordinary return addresses also match.
  Real entry stubs and modeled Draw return wrapping preserve the contract.
- Normal cleanup, logical draw order, controlled state and ownership match.
  Real GPU/source execution, native exceptions and full group integration
  remain outside this proof. Next combine native group/mesh and real GPU
  batching, including callback gates, verification and failure handling,
  then remeasure with all guards before runtime promotion.
- Full plugin suite passes. Built/installed ASI and INI hashes are unchanged;
  no install or gameplay request. This is correctness evidence, not an
  additional speedup or proof of 60 FPS.

## First-mesh instancing screen (2026-10-18)

- [Leader batching screen](specs/2026-10-18-leader-batch-screen.md): including
  the first mesh removes 288 modeled DIPs per frame. Alone it saves only
  0.31-0.46 ms per regular/extra pair on DXVK. With aggregate dispatch after
  the first member, two repeats save 2.94-3.12 ms. These are synthetic
  full workloads, not additive pass savings or gameplay FPS.
- Regular/extra images, fresh shadows, deliberately omitted-leader control,
  counts, references and frame state pass. Preserve 252 final samples and
  168 earlier exploratory samples separately. No production edit or install.
- Native group tests now cover bulk entry after one or two members:
  10,752 cases per local image, with verifier and broken-bridge controls.
  Native mesh rendering is still a controlled dependency.
- Near the research threshold, but not runtime-qualified. Next prove the
  native leader's final draw and subsequent state/cleanup, then measure
  native loop/GPU integration including guards. A source-level single draw
  is insufficient evidence to suppress it before observing completion.
- Full plugin suite and old bulk-only fixture regression pass. Built beta.3,
  installed 0.9.10 and installed INI hashes remain unchanged. No user test
  requested; last saved-game result remains about 30.18 presentations/s.

## Effect-state filter follow-up (2026-10-18)

- A [device-write filter screen](specs/2026-10-18-effect-state-filter-screen.md)
  keeps source setters and the whole D3DX lifecycle, including advancing
  Time. It matches texture/render-state restoration with a custom block;
  separate original/filtered effects avoid manager-switch history effects.
- Final native/DXVK image, state and count controls pass, including repeated
  identical materials and an external-write negative control. The standalone
  filter regresses DXVK completion by 0.48-0.50 ms per modeled pair; with
  aggregate mesh dispatch, two repeats save only 1.47-2.11 ms. Not promoted.
- Preserve 336 final samples and 294 exploratory/excluded samples separately.
  No production edits, deployment or gameplay test; 60 FPS remains unachieved.
- Full plugin suite and earlier clock/state material-cache regression pass.
  Source/log/sample hashes verify; built beta.3, installed 0.9.10 and installed
  INI hashes remain unchanged.

## Material-cache clock/state follow-up (2026-10-18)

- [Stronger clock and per-group state controls](specs/2026-10-18-material-cache-boundary-clock.md)
  reverse the earlier favorable combined-cache screening decision. Both
  exact keys contain Time; advancing it on regular frames reduces DXVK
  completion savings to 0.61-1.08 ms per modeled pair. With per-hit fixture
  state restoration, two repeats instead regress by 0.26-0.87 ms.
- All native/DXVK output, fresh-pose, input-invalidation, count and state
  checks pass. The final protocol passes 10,309 boundary comparisons per
  case, including deliberate binding corruption and omitted restoration.
  This is still not a native engine-cache/lifetime/failure proof.
- Preserve 336 boundary samples and 336 clock samples with separate build
  metadata. Earlier fixed-clock gains are not a gameplay FPS estimate.
  Last confirmed saved-game result remains about 30.18 presentations/s.
- Do not promote or install this cache candidate. Further work needs a
  changed implementation with measured gain under these stronger controls;
  native contract tests alone do not establish a speedup. No user test
  requested, no production source or settings changed.
- Full plugin suite and the shared retained-batch fixture regression pass;
  built beta.3, installed 0.9.10 and installed INI hashes remain unchanged.

## Material-queue cache experiment (2026-10-17, superseded screen)

- User closed the game/launcher; [clean recollection and stronger controls](specs/2026-10-17-material-queue-clean-results.md)
  are complete. Original protocol saved 3.2-3.9 ms on DXVK, but retaining
  real Begin/End, extra input checks and per-mesh markers reduces the final
  gain to 0.66-1.04 ms per modeled pair. Individual samples regress.
  **Not promoted**: insufficient gain for this runtime cache. No installation
  or gameplay test. A [marker CPU screen](specs/2026-10-17-marker-cpu-screen.md)
  now measures about 1.95 ms for all marker work per modeled regular/extra
  pair, including about 1.29 ms above unhooked modeled copies. Next screen
  aggregate batch accounting with the cache, preserving mesh counts and
  measured work. The [combined aggregate-accounting screen](specs/2026-10-17-material-bulk-accounting-screen.md)
  now passes repeated native/DXVK image/state/count checks and saves
  3.42-3.53 ms lit or 3.03-3.06 ms shadow per modeled pair on DXVK (separate
  workloads, not additive). Those fixed-clock results initially supported
  native integration research; the October 18 clock/state controls above
  withdraw that performance qualification. Full plugin tests pass.
- Native event groundwork: [four complete routine bodies](specs/2026-10-17-native-event-contract.md)
  pass full-body/import/branch checks, nine refusal controls and 80 isolated
  callback/buffer/ABI cases per local image. Active callbacks are observable
  and require original events. The separate group-loop proof below extends
  this groundwork. No production event omission or installation.
- The [native group-loop fixture](specs/2026-10-17-native-group-contract.md)
  now verifies nine complete bodies and compares original/bulk control flow
  in 5,376 cases per image. Method ownership, passes, callback fallback,
  partial batches and normal cleanup agree; eleven verifier mutations and
  four deliberately broken bridges are rejected. Native exception unwinding,
  material source computation, GPU failure and engine-state reconciliation
  remain outside that proof. No production hook or installation.
- The same paired fixture also isolates aggregate dispatch without retained
  material state. Two guarded repeats save only 1.69-2.65 ms on DXVK, below
  the standalone screening gate. All output/state/count checks pass. The
  later clock/state controls also disqualify the combined cache candidate.
- Native integration groundwork: eight complete local FX wrapper bodies
  now pass 58 offline call-order/ABI cases on both executable images;
  altered-code refusal controls pass. [Contract and remaining limits](specs/2026-10-17-native-fx-lifecycle-audit.md).
  This verifies asset initialization, six-channel binding, HRESULT paths,
  statistics and effect-pointer timing. It does not prove the native group
  bridge, source callbacks, lifetimes or a runtime cache. No speed claim.
- Added a larger offline fixture with the actual local D3DX27 lifecycle,
  fresh palettes and material-state capture. A persistent mode also reuses
  unchanged regular-frame groups after exact input checks. Native/DXVK
  output, material invalidation, mixed fallback and state checks pass;
  full plugin tests and earlier retained-fixture comparisons pass.
- [Original results and limits](specs/2026-10-17-material-queue-screen.md). The first 504
  timing samples remain explicitly **excluded**: they overlapped a
  game process started outside this work. A new read-only process guard
  refuses both retained benchmark targets before setup or between samples
  while game.dat, BFME's launcher or WorldBuilder is running. Live refusal
  verified; the subsequent no-game runs also pass the guard.
- No production source/installation changes. No claim of 60 FPS or a
  saved-game speedup. The per-member-marker variant does not meet its gate;
  the combined aggregate variant also fails its stronger follow-up screen.
  No gameplay request yet.
- At 20:00, installation was independently observed back on **0.9.10**
  (file modified 19:57:12), ASI SHA-256
  `db65e51d42bb7f49947631a17b9aeebc0604f3cd9e2c055d8e8a35daf65408e5`.
  This work did not replace it; the external cause is unverified. The INI
  hash is unchanged. Log/INI/CSVs copied before reading to
  `%TEMP%/bfme2perf-observed-20261017-200053`; no busy-scene rows there.
  Beta.2 details below describe the earlier deployment/capture.

## Retained extra-frame batch experiment (2026-10-17)

- Built an offline retained-GPU-setup prototype with fresh palettes and a
  newly drawn shadow map, including regular-frame capture overhead in cost.
  [Results and limits](specs/2026-10-17-retained-batch-screen.md); 336 paired
  samples preserved. Production ASI/INI remain unchanged.
- Narrow snapshots save ~1.3-1.8 ms in native submission, ~0.4-0.7 ms in
  DXVK submission, but only ~0.2-0.4 ms in DXVK completion. Broad snapshots
  regress. Lit pixels match exactly; fresh shadow depths match within the
  established tolerance. The full plugin suite and fixture regressions pass.
- Not promoted or installed: insufficient gain on the installed backend
  for a runtime cache with still-unproved lifetime/material/state behavior.
  No gameplay test requested; full-rate 60 FPS remains open. Adjacent-group
  counters alone do not rule out whole-queue opportunities.

## Dispatch experiment completed (2026-10-17)

- Added an offline benchmark and test-only bulk prototype, outside the ASI.
  [Results](specs/2026-10-17-dispatch-benchmark-results.md) and all paired
  samples are preserved. No production source or settings changed.
- Final repeat runs: 1.4-1.7 ms median submission savings with Detail=1,
  across native D3D9/DXVK and lit/shadow workloads at ~6,900 instanced meshes.
  The optimistic path omits member markers; even this falls short of the
  preselected ~3 ms screening threshold. Deprioritized as a standalone beta.
- Pixel/depth comparisons, reference/count/state checks and the full plugin
  suite pass. Installed beta.2 ASI and INI hashes remain unchanged. No new
  installation or gameplay test requested; 60 FPS is still unachieved.
- Development benchmark/docs committed and pushed on `codex/perf-beta`;
  no tags, stable promotion or public release.

## Further screens and beta.3 correction (2026-10-17)

- [Render setup screens](specs/2026-10-17-render-setup-screening-results.md)
  are complete: D3DX27/43 and state-save controls on native D3D9/DXVK;
  isolated ambient sampler on both local executable images. All paired
  measurements and reproduction tools are preserved. Runtime changes saved
  only ~0.1-1 ms synthetically, and the entire ambient sampler ~0.05 ms.
  Neither is promoted; three particle command comparisons also differed.
- Found and fixed a cadence defect: a regular frame after an extra reused
  its shadow map even at ShadowInterval=1. Beta.3 now redraws at interval 1,
  preserving larger-interval reuse. [Build notes](releases/perf-0.9.11-beta.3.md).
  The new regression fails before the fix; the full suite passes afterward.
- Built 0.9.11-beta.3 but **did not install it**. This is a correctness fix
  that can add render work, not a significant speedup. No gameplay test
  requested. Installed beta.2 binary and INI remain unchanged.
- Beta.2's steady captured scene had no extras, so this defect did not
  cause its 30-FPS result. No measured busy extra cost or significant new
  gain has been established. Full-rate 60 FPS remains an open target.
- Development changes committed/pushed to `codex/perf-beta`; no tags,
  stable promotion, public release, reference edits or game launch.

## Performance beta 0.9.11-beta.2 (2026-10-17)

- Implements the approved diagnostic stage: separately staged extra shadow,
  reflection, views and UI timings, with safety-failure invalidation; completed
  flip-enabled End_Scene call counts and bounded gap percentiles/histogram.
- Full plugin tests and all 59 shader comparisons on both native D3D9
  and DXVK pass; final buffer-size regression checks pass. Build commit
  `cad786f` pushed to origin/codex/perf-beta and installed on 2026-10-17.
  No tag or public release.
- No rendering/cadence/quality changes. Full-rate shadows remain the target.
  60 FPS has not been achieved or claimed. Installed beta.2 ASI SHA-256:
  `05d9b381feb2b7ddc0001466ea8c23333e33ce06679786ca2bd1b4c87de94799`.
- Prior beta.1 ASI, INI, log and both CSVs backed up to
  `%TEMP%/bfme2perf-0.9.11-beta.1-installed-20261017-173715`.
  Installed INI hash verified unchanged; no game launched.
- Standing user authorization (2026-10-17): when committing this ongoing
  development work, push the development branch and install tested plugin
  builds without asking again. Continue backups and INI preservation;
  this does not request tags, stable promotion or public release.
- [Release notes and test procedure](releases/perf-0.9.11-beta.2.md),
  [full-pass audit](specs/2026-10-17-full-pass-audit.md).
- Repeated per-mesh dispatch is now benchmarked above; the standalone
  saving is insufficient. Broad merging has few candidates in this save.

## Updated source investigation (2026-10-17)

- User requested pulling Open-BFME-2; fast-forwarded its master by 6,410
  commits to `ff1544d9fef`, preserving pre-existing untracked work and leaving
  the broken BFME1 submodule and Grok checkout alone. No authored edits there.
- [Updated reference audit](specs/2026-10-17-updated-reference-performance-audit.md):
  FX PerformRendering is now recorded matched (819 bytes), enabling stronger
  investigation of group-local direct staging; stale partial comment noted.
- Native isolated event-constructor/destructor test on AotR and game820 found
  only ~0.24-0.26 ms per 7,000 pairs. Copies alone are not a strong candidate.
  Full group dispatch and ambient-lightmap sampling are now measured above;
  neither provides sufficient standalone savings.
- Installed beta.2 is unchanged. The native dispatch benchmark above did not
  justify proceeding to a runtime hook and its target-ABI proof.

## Beta.2 saved-game test (2026-10-17)

- User completed the next test. Copied INI, log and both CSVs before reading:
  `%TEMP%/bfme2perf-beta2-test-20261017-174157`.
- Confirmed runtime beta.2, MaxFPS=120, FrameSmoothing=3,
  ExtraFrameShadows=1, ShadowInterval=1 and Detail=1. No settings changed.
- Steady interval seconds 50-100 inclusive (51 rows): 1,546 game frames,
  1,539 completed flip-enabled EndScene calls (~30.18 calls/s), zero extras,
  zero invalid extra samples, zero extra lateness and no safety stop.
  Seven renders were skipped by catch-up. Completion-gap histogram p50
  upper bound 33.334 ms, p95 upper bound 50 ms (not scanout measurements).
- Frame-weighted regular CPU wall times in that interval: frame 23.039 ms,
  logic 3.574 ms, render 15.658 ms, shadows 7.482 ms, reflections 2.711 ms,
  views 5.887 ms, UI 0.543 ms. Nested scopes overlap.
- Expected extra cost stayed exactly 14.641 ms, inherited from an extra
  draw at second 28. Available budget averaged 10.763 ms; 1,425 limiter
  frames reported cost blocking. Learned floor was zero and no recovery
  probes ran. The regular-draw reference averaged 18.303 ms, so the
  existing reference-based decay could not lower the stale cost estimate.
  Beta.1's recovery is specifically gated on a nonzero learned floor and
  would not address this case; twice the reference would not fit either.
- The 40 valid extras with anim_on_screen>20 all occurred at seconds 27-29,
  with only 102-138 animated objects; subsequent rows reached hundreds.
  Their mean total was 3.835 ms, shadow marker 1.024 ms, views 1.278 ms,
  UI 0.666 ms. These are transition measurements, NOT the missing busy-scene
  extra cost. No reflection samples, consistent with extra reflections off.
- One 35.844 ms extra draw caused 22 ms lateness at second 113, after
  anim_on_screen returned to zero; existing one-second pause engaged.
  Do not attribute this menu/transition row to the sustained battle.
- Result: the measurement plumbing works, but 60 FPS remains unachieved.
  Do not infer that the busy extra draw costs 14.641 ms or that it would
  fit from the early 3.835 ms mean. Next work must address the unobserved
  busy-scene cost with a defensible sampling budget and/or reduce verified
  per-group dispatch overhead; retain multiplayer deadline safety.

## Performance beta 0.9.11-beta.1 (2026-10-17)

- User approved focused performance beta and larger render investigation.
  Baseline `bb072a9` was verified clean and pushed on `main` and
  `feat/perf-dll` before development. New work is on `codex/perf-beta`.
- Implements bounded recovery from a stale learned extra-frame cost floor,
  a 256-entry LRU declaration cache, and appended timing diagnostics.
- Full `perf/` test suite passed. All 59 existing instanced shader variants
  passed offline comparisons on native D3D9 and DXVK. The new mandatory CPU
  cache test covers 600 identities, hot reuse, negative caching and COM
  reference balance. Recovery tests cover a transient, repeated expensive
  probes, interrupted eligibility and millisecond-clock wraparound.
- Installed 0.9.11-beta.1 at the user's request on 2026-10-17. Only
  `rotwk/mss/bfme2perf.asi` replaced; SHA-256 matches the tested build
  `4dc932daa1bc9924a49d47b95ea6025620332cfb674476bbcf624abfb76928f1`.
  Installed INI hash unchanged. Previous 0.9.10 binary, INI, log and both
  CSVs backed up to
  `%TEMP%/bfme2perf-0.9.10-installed-20261017-171107`.
  The user performed the saved-game test recorded below. Beta commits
  through `ace76a5` pushed on 2026-10-17; no publication or tag.
  Stable branches remain at the baseline.
- [Release notes](releases/perf-0.9.11-beta.1.md),
  [approved design](specs/2026-10-17-perf-beta-design.md),
  [next render audit](specs/2026-10-17-render-audit.md).
- Both missing shader hashes identify normalmapped shadow-pass variants;
  a new independently verified shadow family is the next candidate.
  Skeleton threads are deferred on measured cost and current FP rules.
- User reports current sage-boost works online (2026-10-17), but mostly at
  30 FPS. The captured log's match mode is not confirmed. Beta gameplay,
  mixed performance-plugin versions and replay validation remain pending.

### First beta gameplay report (2026-10-17)

- User tested a troop-heavy saved game: felt smoother, reported no FPS
  above 30. Match/network mode and the FPS counter are not confirmed.
- Copied log, CSVs and INI before inspection to
  `%TEMP%/bfme2perf-beta1-test-20261017-171637`. Log confirms beta.1 and
  existing MaxFPS=120, ExtraFrameShadows=1, Instancing=1 settings.
- 91 CSV rows with anim_on_screen >20 and frames >=15: 2,739 game frames,
  23 extras; no safety-stop blocks, learned floor 0 and no recovery probes.
  Frame-weighted row averages: render 15.449 ms, shadows 7.021 ms,
  skeleton-in-render 1.540 ms, limiter budget 11.374 ms. These scopes
  overlap and are not additive.
- During seconds 55-114 extra cost remained about 12.46 ms, while the
  average available budget by 20-row group was about 7-12 ms. With margin
  and slack it needs roughly 17.5 ms. This trace shows insufficient room,
  not the stale learned-floor failure. Extras appear in lighter portions.
- Latest instancing summary: 30 declarations, 0 evictions/failures,
  0 mismatches, 54 verify samples and 0 bad. This scenario did not exercise
  cache exhaustion. No controlled baseline capture proves the reported
  smoothness improvement's cause.
- Prioritize reducing shadow/extra-draw cost for this save. Skeleton work
  is higher than in the earlier trace but is still only about 1.54 ms;
  missing-shader counts are small (19,470 versus 8,372,353 instanced meshes
  in the summary), so extending shader coverage alone may have little
  effect here. Do not loosen game-frame deadlines to force extras.

## Current work: unlocker 0.5.0 LAN faction fix (2026-10-17)

- User reported 17 exact faction-index wraps and an early multiplayer desync.
  The LAN nibble codec explains every mapping; the subsequent crash has not
  been reproduced independently.
- 0.5.0 replaces ten human/AI colour/faction/start call sites as one group.
  Packet length is unchanged, including eight-player games. All players need
  the updated protocol. Performance plugin unchanged.
- Source and launcher packaging instructions: [release notes](releases/unlocker-0.5.0.md).
- Full `dll/` test suite passed on 2026-10-17: site verification, engine,
  original hooks, proxy and network tests (110,592 native round trips across
  the three images plus all 65,536 field values).
- Source commit `882b6ab` pushed to `main` and `feat/perf-dll`.
- Published through Realms Launcher Beta as `0.5.0-perf.0.9.10`, release
  `3c1c2b37aedb6612cd4e`, on 2026-10-17. Only `dinput8.dll` changed; the
  previous published ASI and both INI templates are byte-identical. Signed
  player download ranges for all four files were verified against SHA-256.
- Local game installation remains unchanged; no Git tag created.
- Required user test before public rollout: two PCs, explicit high-index
  factions and Random/AI, correct colours/starts, then a sustained match.
- The installed-build details below describe the previous deployment.

## Unpushed commits

Pushing is the user's call. List them again with `git log --oneline @{u}..`
in each repo.

| repo | branch | local commits not on origin |
|---|---|---|
| sage-boost (`C:\repositories\sage-boost`) | `feat/perf-dll`, `main` | none (all pushed 2026-10-15; `main` = `feat/perf-dll`) |
| Realms in Exile (`...\realms_nightly`, [hansnery/realms-nightly](https://github.com/hansnery/realms-nightly); was `...\realms_nightly\data`, hansnery/data, until 2026-10-15) | `master` | check with `git log --oneline @{u}..`; the move and the Dorwinion fix were pushed 2026-10-15 |
| realms-worldbuilder (`C:\Jogos\AgeoftheRing\realms-worldbuilder`) | `main` | none (all pushed 2026-10-15) |

The six files that can show as modified (`docs/plans/...`, `perf/Makefile`,
`perf/src/bfme2perf.def`, `tools/port_sites.py`, `tools/test_port_sites.py`)
differ only by CRLF line endings; `git diff --ignore-cr-at-eol` is empty.
Don't commit them.

## Installed in the game

Game folder: `C:\Jogos\AgeoftheRing\rotwk` (Age of the Ring's ROTWK 2.01
`game.dat`, Realms in Exile loaded on top).

### Faction unlocker: `dinput8.dll` + `dinput8.ini`

- Build from `feat/perf-dll` at `2259a46` (installed 2026-10-15 02:09). It
  still reports version 0.4.0; the change is the `[Sides]` cap of 32 (was
  16). Previous DLL and INI: `%TEMP%\dinput8-backup-20261015-020959`.
- Last start (02:15): `the verified baseline build`, all 17 aliases
  accepted, `summary: 8 applied, 0 refused, 2 off` (B and C2 off).
- `[Sides]` as installed. The aliases and their comment block were added
  on 2026-10-14 17:21 (backup before: `%TEMP%\dinput8.ini.before-sides-20261014-172145.bak`),
  `DolAmroth` later that day, `Lorinand` and the "at most 32 entries"
  comment on 2026-10-15 02:10. Everything else is the shipped INI.

```ini
Arthedain=Men
Numenor=Men
Ithilien=Men
Hornburg=Rohan
EredLuin=Dwarves
Lindon=Arnor
Shire=Arnor
Dorwinion=Mirkwood
Morgul=Mordor
Annatar=Mordor
WhiteHand=Isengard
Gundabad=Wild
Moria=Wild
Rhudaur=Guldur
CarnDum=Guldur
DolAmroth=Men
Lorinand=Elves
```

  `Rhun` is one of Age of the Ring's own Sides and can't be aliased.

### Performance plugin: `mss\bfme2perf.asi` + `mss\bfme2perf.ini`

- Current observation at 20:00 on 2026-10-17: **0.9.10**, externally replaced;
  hash and preserved logs above. Earlier: 0.9.11-beta.2 from `cad786f`,
  installed 2026-10-17; backup and verification above.
  Previously 0.9.10, built from `05420da` (pushed),
  installed 2026-10-14 18:51. The
  previous build and settings are in `%TEMP%\bfme2perf-0.9.9-installed-1852`.
- `bfme2perf.ini` (LF line endings) differs from the shipped
  `perf/bfme2perf.ini`. Keep the user's values; when a build adds a key,
  add it with its comment and leave the rest alone.
  - `[Timing] Enabled=1 Detail=1` (per-mesh timing on, for measurements).
  - `[Render] MaxFPS=120` (extra frames; 30 in the 0.9.7 backup, 120 again
    since 0.9.9),
    `FrameSmoothing=3` (units drawn between the last two game frames; the
    camera is never moved since 0.9.10), `ExtraFrameShadows=1`,
    `Instancing=1`, `InstancingVerify=1`, `ShadowPassVerify=1`,
    `ReflectionInterval=2`, `ShadowInterval=1`, `LightCull=1`,
    `LightReuse=1`, `LightVerify=1`, `LightSkipShadow=0`.
  - `[CatchUp] Enabled=1 MaxSkips=1 Notice=1 NoticeText=Recuperando atraso
    NoticeMinSkips=6`. `NoticeMinSkips=6` came with 0.9.9 (the notice
    showed for single skipped frames). The user had raised `MaxSkips` to
    3 (in the 0.9.7 and 0.9.9 backups) and set it back to 1 on 2026-10-14
    19:19; confirmed 2026-10-15: keep `MaxSkips=1`.
- Logs next to it: `bfme2perf.log`, `bfme2perf_timing.csv`,
  `bfme2perf_scopes.csv` (overwritten on every start; copy them to `%TEMP%`
  before a new build if they matter).

## Waiting for an in-game test

Plugin 0.9.10:

- Scroll and stop: the camera stops dead (no drift).
- Fast right-mouse scrolling: no black strip at the screen edge.
- Moving units still look smooth at `FrameSmoothing=3`.
- The catch-up notice shows only when catching up is sustained (6 skipped
  frames within 2 s), and the battle keeps its speed.
- Multiplayer: a network game with every player on 0.9.10 worked fine
  (played 2026-10-15). Still open: a game where only some PCs have the
  plugin, and a replay.

Realms in Exile (details in its `AGENTS.md` change log, all "not yet
confirmed in game"):

- Dol Amroth as a lobby faction (fortress, porter, spellbook incl. the
  Rebuild fix, AI base), and its farms on the minimap.
- Lothlórien / Lórinand split (`Lorinand=Elves` alias now loads).
- Nazgûl Realms split (Dol Guldur, Angmar, Carn Dûm) and Misty Mountains
  split (Misty Mountains, Gundabad, Moria).
- Inflation filter: income of the new factions' farms drops from the 6th
  building on (the filter additions of 2026-10-14).
- Carn Dûm expansion pads draw `UBFoundation`/`UBFoundationP` like the
  Angmar subfaction's, and the plot model disappears once something is
  built on a pad. See the castle problem below first.

## Waiting for the user (WorldBuilder and decisions)

- **`fortress_carndum.bse` (castle file, check first).** The scripted patch
  (2026-10-14 ~17:37) wrote 3 side + 4 corner pads with the "In Base"
  property and 8 `CastleTemplates` entries. The file on disk now is a later
  save (17:39, 8,807 bytes, not recorded by any agent; most likely the user
  in WorldBuilder): the citadel at (1000, 1000), 7 pads all
  `CarnDumFortressExpansionPadSide` (also at the 4 corner positions), none
  with `objectBaseName`, and `CastleTemplates` lists only the citadel. Every
  other castle has its pads in `CastleTemplates`, so the pads probably
  don't unpack in game. The user chose (2026-10-15) to fix it themselves
  in WorldBuilder: 3 `CarnDumFortressExpansionPadSide` at (890, 1000),
  (1000, 890), (1000, 1110); 4 `CarnDumFortressExpansionPadCorner` at
  (1077, 1077), (923, 1077), (923, 923), (1077, 923); "In Base" on all 7
  and the citadel. Don't overwrite it with the script. After their save,
  check with `tools/bse_list.py` and `tools/bse_fielddiff.py`.
- **AI bases** in `realms_nightly\bases` (the user edits them; don't
  regenerate or overwrite them):
  - Ered Luin: still a byte copy of Realms' old `ai base - dwarves - at and
    infantry`; to be edited.
  - Lórinand: still an unedited copy of `ai base - elves - lw rush`; to be
    edited.
  - Ithilien: edited 2026-10-14 16:26 (the Gondor buildings and farms of
    Realms' old `motw - standard base` are gone; a `GondorStatue` and a
    `RivendellDunedainGathering` remain, `MenFortress` centre). Ask whether
    it is finished.
  - Carn Dûm: done (`CarnDumFortress` centre, 17:04).
  - Optional: a Gundabad base (it uses the Wild goblin spam layouts now).
  - Optional "Shire hobbit farm": the Shire layout already places 2
    `ShireHobbitFarm` and no `ShireHobbitHole`, while the Shire change-log
    entry says the opposite. Ask what is still wanted.
- Optionally open `fortress_carndum.bse` once in WorldBuilder after the fix
  to confirm it loads.

## Backlog

Realms in Exile: see *Backlog* in its `data\AGENTS.md` (capture flags, civilian
castles, redundant alternate maps, buildings outside the inflation filter,
Carn Dûm pad portrait, off-radar buildings, unconfirmed change-log entries).

sage-boost:

- The README's *Not done yet* still says nothing was verified in game; the
  unlocker 0.4.0 (S2/S4, `[Sides]`) and the plugin have been run on Age of
  the Ring with Realms since.
- The history is one commit (2026-10-15); the per-feature branches and
  worktrees are gone (see *Branches* in `AGENTS.md`).
- What the docs can't settle (in-game tests, the user's WorldBuilder work,
  starting a new model): [BACKLOG.md](BACKLOG.md).
- Vanilla BFME2 1.06 for the plugin; the unlocker's manual test checklist.
