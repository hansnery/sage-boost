# Handoff backlog (2026-10-15)

What the documentation can't settle on its own, for sage-boost and for the
Realms in Exile data repo. Read this together with [STATUS.md](STATUS.md)
and [AGENTS.md](../AGENTS.md) (and the Realms data repo's `AGENTS.md`).
Strike an item through, with the date, when it's done.

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
- [ ] Multiplayer with the plugin: a network game between a PC with it and
  one without, and a replay (in the plugin design notes' test plan; never
  done).

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
  `docs/STATUS.md` here, this file, and `AGENTS.md` in the Realms data repo.
  sage-boost also has a `CLAUDE.md` pointing to `AGENTS.md`; the Realms
  repo has only `AGENTS.md`.
- [ ] **The docs haven't been tried by a fresh reader.** They were written
  by agents that knew the whole conversation. Give the new model a small,
  safe first task, e.g. "list what's pending and what you must never do",
  and compare its answer with these files before trusting it with real
  work. Fix the docs where it gets something wrong.
