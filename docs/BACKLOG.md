# Handoff backlog (2026-10-15)

What the documentation can't settle on its own, for sage-boost and for the
Realms in Exile repo (hansnery/realms-nightly). Read this together with [STATUS.md](STATUS.md)
and [AGENTS.md](../AGENTS.md) (and the Realms repo's `data\AGENTS.md`).
Strike an item through, with the date, when it's done.

## Full-rate 60 FPS investigation (2026-10-17)

- [x] Beta.2 saved-game test captured 2026-10-17 with full-rate shadows;
  steady section still ~30 presentation calls/s, no extras. See STATUS.md.
- [ ] Resolve busy-scene extra-cost observability: no extras after the
  transition, and the existing regular-draw reference cannot lower the
  14.641 ms estimate into the available budget. Do not assume early-scene
  costs apply to the busy scene or weaken multiplayer deadline margins.
- [x] 2026-10-17: benchmark group-local instancing dispatch on native D3D9
  and DXVK; 1.4-1.7 ms optimistic submission saving is insufficient for a
  standalone beta. Results in `specs/2026-10-17-dispatch-benchmark-results.md`.
- [ ] If a larger combined change makes bulk dispatch worthwhile, complete
  target-ABI, material, reference, state, scope and fallback proofs before
  implementing an installed fast path. Current prototype is offline only.
- [ ] Demonstrate sustained 60 visual FPS with full-rate shadows; still open.
- [x] 2026-10-18: screen including the first mesh in instanced batches;
  leader-only DXVK savings are 0.31-0.46 ms/pair, combined with aggregate
  dispatch 2.94-3.12 ms in two repeats. Regular/extra output, missing-leader
  control, counts, state and references pass. See
  `specs/2026-10-18-leader-batch-screen.md`; no runtime promotion.
- [x] 2026-10-18: extend the native group fixture to bulk after one or two
  members: 10,752 cases per image plus refusal/broken-bridge controls pass.
- [ ] Prove the native leader's final draw and post-draw state/cleanup,
  callback behavior and failure path before any draw suppression. Measure
  native group/GPU integration and its guards; separate logical native
  and modeled GPU tests do not establish an installable bridge.
- [x] 2026-10-18: seven native mesh/draw/cleanup bodies and final stack-frame
  identity pass 38,016 executions per local image, eleven altered-code/data
  controls and two broken gates; see
  `specs/2026-10-18-native-mesh-draw-contract.md`. Controlled dependencies
  leave real GPU integration and failure paths open.
- [ ] Combine native group/mesh with real effect/instancing output checks,
  callback and verification gates, split/partial batches and failure
  handling; remeasure the complete guarded path before runtime promotion.
- [x] 2026-10-18: native mesh/draw/cleanup plus real GPU and timing.c
  wrapping passes image, callback-gate, split/partial-batch, earlier-DIP and
  verification-window controls. Two repeats save 2.81-2.95 ms per pair on
  DXVK; see `specs/2026-10-18-native-mesh-gpu-screen.md`. Native enclosing
  group/events, engine-state reconciliation and failure recovery remain open.
- [x] 2026-10-18: join the native group/events/method ownership to that
  native mesh/GPU fixture. All four backend/workload cases pass 136 paired
  controls plus verification, and two repeats save 2.78-2.99 ms per pair
  on DXVK. See `specs/2026-10-18-native-group-gpu-screen.md`.
- [ ] Prove GPU-operation failure recovery after leader deferral/partial
  submission in the joined fixture; current zero-consumption abort is not
  a runtime fallback. Native source/state/lifetime and exception boundaries
  remain open, and further savings are needed before a gameplay request.
- [x] 2026-10-18: reproduce GPU rejection defects in the joined native path:
  224 controlled observations, with clean unique-position image controls,
  immediate state snapshots and broken-oracle checks. Each backend/workload
  has 28 changed outputs and 28 state leaks. See
  `specs/2026-10-18-native-gpu-fault-audit.md`; recovery is still unfixed.
- [ ] Check all batch setup/restoration results and retain enough staged
  ownership/commit information to replay only unsubmitted meshes after
  coherent device/effect/engine state restoration. Require the fault audit
  to show matching output/state, not merely detect today's counterexamples.
- [x] 2026-10-18: offline skeletal recovery passes 224 single rejected batch
  operations plus eight leader-capture failures with matching output/state
  and exactly 25 units. Missing/duplicate replay controls are rejected.
  Two timing repeats retain 2.90-3.06 ms/pair on DXVK; see
  `specs/2026-10-18-native-gpu-recovery.md`. No production source or install.
- [ ] Qualify native source/effect/engine-state reconciliation and lifetimes
  for staged replay, remaining rigid/persistent/device-loss/exception paths,
  and the bridge's zero-consumption fallback. The single-rejection fixture
  does not close the broader runtime-recovery item or the 60 FPS deficit.
- [x] 2026-10-18: add native FX lifecycle/source walker and pending-state
  application to the GPU fixture. Fix the discovered null-stream offset
  leak in the offline candidate; 248 single failures preserve device,
  effect-value and fixture-engine state. Two final repeats save 2.68-2.93
  ms/pair on DXVK; see `specs/2026-10-18-native-fx-gpu-integration.md`.
- [ ] Replace controlled source computations and qualify side effects and
  lifetimes. Investigate avoidable source evaluation for inputs unused by
  the selected technique; require output/state and meaningful timing proof.
- [x] 2026-10-18: AddBinding full-body audit confirms existing selected-
  technique usage filtering in both local images. A parsing parent can be
  tested instead of the leaf; actual member/array usage remains to inspect.
- [x] 2026-10-18: screen exact sharing of native encoding-1 animation
  decoding, including warm caller caches. Both images pass 303,821 output
  comparisons plus broken/refusal controls; 504 timings retained. Warm
  savings are small and diversity can regress; no promotion. See
  `specs/2026-10-18-native-decode-screen.md`.
- [x] 2026-10-18: audit effect member usage on both D3DX runtimes/backends.
  A used-member control changes commands despite a false usage query;
  reject leaf-query pruning. All 38 techniques audited; see
  `specs/2026-10-18-effect-member-audit.md`. Visibility/setup scopes are
  small and overlap broader scopes; whole-object reuse remains unsafe.
- [x] 2026-10-18: screen native rendering-task sort ownership traffic.
  Both images pass 936 comparisons and refusal/corruption controls; 756
  final timings preserve target function spacing. One-method workloads
  save 0.51-2.04 ms per synthetic 7,680-record workload. See
  `specs/2026-10-18-native-sort-screen.md`; no runtime promotion.
- [ ] Join sort/queue ownership and the native group/GPU candidate, preserving
  exact order, commands, lifetimes and fallback. Measure combined costs;
  isolated sort timings cannot be added or doubled into gameplay gains.
  Qualify installed context guards and exception behavior if worthwhile.
- [x] 2026-10-18: join normal owned queues, native sort/group/mesh/FX and
  GPU rendering, with target-relative code placement. Both images pass
  eight backend/workload cases and sixteen specific corruption controls.
  Two shuffled-input repeats save 5.42-5.84 ms/pair on DXVK; ascending
  input saves 4.48-4.51 ms in one run. Preserve 378 samples. See
  `specs/2026-10-18-native-queue-gpu-integration.md`; no runtime promotion.
- [ ] Screen actual Add_Rendering_Task and the available-capacity vector
  path. The joined fixture uses native record copy/destruction, not native
  insertion/growth/erase. Preserve queue selection, method and mesh
  ownership, capacity checks and native growth fallback; require a combined
  timing gain before runtime integration. Full lifetimes/failures remain open.
- [x] 2026-10-17: larger material-queue fixture passes native/DXVK fresh
  pose, material invalidation, regular/extra image and state checks.
- [x] 2026-10-17: recollect material-queue timings with the game closed.
  The original 504 excluded samples remain separate. Stronger lifecycle,
  extra-input and marker controls reduce DXVK gain to 0.66-1.04 ms/pair;
  insufficient for promotion. See `specs/2026-10-17-material-queue-clean-results.md`.
- [x] 2026-10-17: three guarded CPU marker runs, 315 samples preserved;
  about 1.95 ms total marker work per modeled regular/extra pair. See
  `specs/2026-10-17-marker-cpu-screen.md`. No installed Detail change.
- [x] 2026-10-17: screen aggregate accounting with material reuse; 420 clean
  samples retain mesh counts and measured work. DXVK savings ~3.0-3.5 ms
  per modeled pair; changed interval boundaries documented in
  `specs/2026-10-17-material-bulk-accounting-screen.md`. Favorable screening
  decision superseded by the October 18 clock/state controls below.
- [x] 2026-10-18: advance Time between regular frames and restore the full
  fixture touched-state mask per hit. All image/state/count controls pass,
  but DXVK completion regresses 0.26-0.87 ms per pair. Preserve 672 samples
  and separate build metadata; see
  `specs/2026-10-18-material-cache-boundary-clock.md`. No runtime promotion.
- [ ] Establish meaningful gain for a redesigned candidate with changing
  inputs and credible state reconciliation before further cache integration.
  Source callbacks, lifetimes and GPU-failure proofs remain necessary if
  a future candidate qualifies; the normal group-loop proof is not a cache.
- [x] 2026-10-18: screen exact device-write filtering with the full effect
  lifecycle and advancing Time. Standalone DXVK cost regresses; filter plus
  bulk saves only 1.47-2.11 ms per modeled pair. State/image/count controls
  pass; no runtime promotion. See `specs/2026-10-18-effect-state-filter-screen.md`.
- [x] 2026-10-17: four native event bodies pass nine refusal controls and
  80 callback/buffer/ABI cases on each image. Enclosing loop/SEH remains
  outside this proof; see `specs/2026-10-17-native-event-contract.md`.
- [x] 2026-10-17: native group loop, method-chain helpers and event bodies
  pass 5,376 normal-control-flow/ownership cases per image, eleven verifier
  refusal controls and four deliberately broken-bridge controls. Partial
  batching and callback fallback agree with stock. Exception unwinding,
  source callbacks, GPU failures and engine-cache integration remain open;
  see `specs/2026-10-17-native-group-contract.md`.
- [ ] If clean measurements justify it, prove the native effect/group
  bridge, lifetimes, engine state and diagnostics before runtime promotion.
  Current persistent cache borrows fixed fixture objects only.
- [x] 2026-10-17: eight native FX wrappers/source-walker bodies pass 58
  isolated call-order/ABI cases on both local images; verifier refusal
  controls pass. See `specs/2026-10-17-native-fx-lifecycle-audit.md`.
  This is only a partial contract proof, not the group bridge or speedup.
- [ ] Account for the externally observed return to installed 0.9.10 before
  the next deployment/test. This work did not replace the ASI; INI unchanged.
- [x] 2026-10-17: prototype retained GPU batches with fresh bone palettes,
  per-regular-frame capture cost, native/DXVK output/state checks and paired
  timings. Narrow replay passes but DXVK completion saves only ~0.2-0.4 ms;
  broad state snapshots regress. No runtime promotion or gameplay test.
- [ ] Whole-queue reuse remains unproved: establish callback/visibility and
  material lifetime/state equivalence before a larger prototype. Existing
  adjacent single-method counters do not measure the whole opportunity.
- [x] 2026-10-17: screen newer effect runtime, state-save overhead and
  terrain ambient sampling offline; insufficient savings, no install.
- [x] 2026-10-17: beta.3 fixes ShadowInterval=1 reusing the previous extra
  frame's map. Hook regression and full suite pass; build remains uninstalled
  because it is a cadence correction rather than the requested speedup.
- [ ] Ask for another gameplay test only after evidence of a significant
  improvement (user's request, 2026-10-17); no diagnostic-only build cycle.

## Performance beta 0.9.11-beta.1 (2026-10-17)

- [x] Full plugin suite and offline shader checks (59 variants each on native
  D3D9 and DXVK); new cache and scheduler regression tests.
- [x] 2026-10-17: user-authorized local deployment after backup; installed
  INI preserved and ASI hash verified.
- [x] First beta troop-heavy saved-game test: user reports smoother but
  <=30 FPS; capture shows insufficient extra-draw budget, no safety stop.
- [ ] Comparable SP/MP scenes, transient hitch recovery, sustained battles,
  camera/visual checks, mixed performance-plugin clients and replay test.
- [ ] Verify launcher beta isolation/rollback before authorized publication.
- [ ] Lower-priority coverage experiment (small contribution in this save):
  independently authored normalmapped shadow
  variants for hashes CF3571C7/94092159; see
  [render audit](specs/2026-10-17-render-audit.md).

## Unlocker 0.5.0 LAN validation (2026-10-17)

- [ ] Both PCs run the new DLL with identical Realms data and matching INIs.
- [ ] Host and guest selections of Lindon, Shire, Mordor and Rhun agree on
  both screens; test AI slots and all colours/start positions too.
- [ ] Random resolves consistently; play a sustained game and an eight-slot
  lobby/match without the reported early desync.
- [ ] Check an older DLL cannot join the new lobby; update all launcher users
  together. See [release notes](releases/unlocker-0.5.0.md).

## Before the handoff (the user)

- [ ] **Back up the files outside any repo.** The `.bse` work in
  `realms_nightly\bases` and the Shire base's source map in
  `Realms in Exile Nightly\maps\ai base - shire - standard base\` are under
  no version control, and their only backups live in `%TEMP%`, which Windows
  may clear. A Realms reinstall or launcher update can overwrite them. See
  *Files outside any repo* in the Realms `AGENTS.md` for the full list.
  - [x] 2026-10-15: `rotwk\dinput8.ini` (17 `[Sides]` aliases) and
    `rotwk\mss\bfme2perf.ini`, copied to
    `realms_nightly\sage-boost-backup\2026-10-15\`.

## Nothing recent has been tested in game

No model can run the game; only the user can. Everything below was built
and checked against the data and the tests, not played. A model that builds
new splits on these copies whatever mistake they contain, so test at least
one of each kind first. The checklists are in [STATUS.md](STATUS.md),
*Waiting for an in-game test*.

- [ ] Plugin 0.9.10: the camera stops dead after scrolling, no black strip
  while scrolling fast, units still smooth, the catch-up notice only when
  catching up is sustained.
- [ ] Unlocker with the 32-alias cap: `dinput8.log` shows
  `sides: 17 alias(es)` and `Lorinand -> Elves`.
- [ ] Dol Amroth and Lórinand as lobby factions (fortress, porters, spell
  store, AI base and army, farms on the minimap).
- [ ] The Nazgûl split (Dol Guldur, Angmar, Carn Dûm) and the Misty
  Mountains split (Misty Mountains, Gundabad, Moria).
- [ ] The inflation-filter additions: farm income drops from the 6th
  building for the new factions.
- [ ] The Carn Dûm pads (Angmar subfaction art) once `fortress_carndum.bse`
  is fixed.
- [x] Multiplayer with the plugin, every player running 0.9.10: played
  2026-10-15, worked fine (the user's report).
- [ ] Multiplayer with the plugin on only some PCs (a PC with it and one
  without), and a replay of a game played with it (both in the plugin
  design notes' test plan; not done yet).

## The user's own WorldBuilder work

Models must not regenerate or overwrite these; check them after the user
saves.

- [ ] `fortress_carndum.bse`: 3 `CarnDumFortressExpansionPadSide` at
  (890, 1000), (1000, 890), (1000, 1110); 4
  `CarnDumFortressExpansionPadCorner` at (1077, 1077), (923, 1077),
  (923, 923), (1077, 923); "In Base" on all 7 and the citadel at
  (1000, 1000). Check with `tools/bse_list.py`.
- [ ] AI bases to edit: Ered Luin and Lórinand (unedited copies), Ithilien
  (edited; ask whether it's finished). Optional: a Gundabad base; the Shire
  hobbit farm question (see STATUS.md).

## Starting a new model

- [ ] Tell it explicitly to read, before anything else: `AGENTS.md` and
  `docs/STATUS.md` here, this file, and `data\AGENTS.md` in the Realms repo
  (`realms_nightly`, hansnery/realms-nightly).
  sage-boost also has a `CLAUDE.md` pointing to `AGENTS.md`; the Realms
  repo has only `AGENTS.md`.
- [ ] **The docs haven't been tried by a fresh reader.** They were written
  by agents that knew the whole conversation. Give the new model a small,
  safe first task, e.g. "list what's pending and what you must never do",
  and compare its answer with these files before trusting it with real
  work. Fix the docs where it gets something wrong.
