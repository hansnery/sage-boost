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
- [x] 2026-10-17: larger material-queue fixture passes native/DXVK fresh
  pose, material invalidation, regular/extra image and state checks.
- [x] 2026-10-17: recollect material-queue timings with the game closed.
  The original 504 excluded samples remain separate. Stronger lifecycle,
  extra-input and marker controls reduce DXVK gain to 0.66-1.04 ms/pair;
  insufficient for promotion. See `specs/2026-10-17-material-queue-clean-results.md`.
- [ ] Investigate marker-processing overhead with preserved diagnostics
  and the native debug contract; do not change the installed Detail setting.
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
