# Status and handoff (2026-10-18)

Where the three repositories and the installed game stand, for a model or
person picking the work up. Rules, build steps and branches are in
[AGENTS.md](../AGENTS.md); the Realms side (factions, AI, files outside the
repo, backlog) is in the Realms repo's `data\AGENTS.md`; what the docs can't
settle is in [BACKLOG.md](BACKLOG.md). Re-check anything below against the
files before relying on it.

## Next target: 60 visual FPS with shadows at 60 (2026-10-17)

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
