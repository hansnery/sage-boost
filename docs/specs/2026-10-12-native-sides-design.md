# Skirmish AI for new Sides without donors

Status: Phase R done, gate passed (`DISCREPANCIES.md` section 8). Implemented
in 0.4.0 as patches S2 (reworked) and S4, automated tests passing. Confirmed in
game on Age of the Ring with Realms in Exile (2026-10-12): AIs on its new Sides
build their bases and farms. Two amendments from the research:

- **The pick uses the game's Side table order, not the map's player order.**
  The DLL can ask the engine whether a map has a player for a given Side, but
  it can't list the map's players without reading many more engine
  structures. Every map skirmish player is bare, so the order doesn't change
  what the AI gets. The reserved Sides (`Neutral`, `Civilian`, `Observer`)
  are never picked.
- **S4 also fixes the shipped S2.** The engine matches Sides twice, and S2
  only hooks the second match. S4 hooks the first, and both use the same
  resolution: own Side, then the `[Sides]` donor, then the fallback.

## Goal

A faction with a Side the map doesn't know (for example Realms in Exile's
`Arthedain`, `Numenor`, `Ithilien`) gets a working skirmish AI on every
existing map, without editing the maps in WorldBuilder and without naming a
donor in `[Sides]`.

Out of scope (the user's choice): the new Side's faction id (Create-A-Hero,
rank icons, stats) and its rank thresholds. S1 and S3 stay as they are, and
`[Sides]` donors stay available for them.

## Context

- Target executable: Age of the Ring's `game.dat` (ROTWK 2.01 plus the mod's
  edits), which Realms in Exile runs on; also clean 2.01 (`game820.dat`).
  BFME2 1.06 gets the same patch only if its code matches.
- Realms in Exile's `lotrbfme2ep1.exe` is a Qt launcher that starts the game
  with `-mod`; it is not a game executable.
- AI bases are data-driven: ROTWK reads `AIBase` blocks (`Side = …`,
  `Map = "ai base - …"`, `GameMapToUseOn`) from INI. Realms already defines
  `AIBase … Side = Arthedain`.
- Every Realms map sampled (`map mp adorn river`, `map mp aldamori`) has one
  skirmish player per faction it knows (`SkirmishMen` … `SkirmishGuldur`, 11
  of them) and none for `Arthedain`, `Numenor` or `Ithilien`.
- At game start the engine matches each AI player's Side against the Sides of
  the map's players (`playerFaction` → PlayerTemplate → Side;
  `Player::rva002AC4D4` and its caller in 1.06, S2's site). On a miss the
  player is "quietly made nonskirmish" and gets no base.

## Phase R: research (read-only)

Done on Age of the Ring's `game.dat` and `game820.dat`, plus Realms maps
decoded offline (RefPack). Nothing outside this repository is written.

| # | Question |
|---|---|
| R1 | The matching loop: which map players it considers (the exact predicate), what it copies from a match (scripts, teams, build list, base), and what the miss path does. |
| R2 | How the `AIBase` from INI is chosen: by the player's Side, by the matched map player, or by template; and whether only skirmish AI players use it. |
| R3 | How `SkirmishBuildList` and `SideInfo` (AIData) are looked up. |
| R4 | What a map skirmish player contains in practice: script names and contents, teams, build list. Sample at least five Realms maps and two vanilla ROTWK maps. |
| R5 | Whether a "nonskirmish" AI player has any AI at all. |

Output: a findings section appended to `DISCREPANCIES.md`, the site
signatures for the patch, and the decision below.

**Decision gate.** Build the fallback only if both hold:

1. Base, build list and AIData for an AI player come from its own Side (R2,
   R3), not from the matched map player.
2. A map skirmish player carries only content that is safe for any faction to
   run (R4): map-generic scripts such as the Gollum spawns, no scripts that
   create or reference one faction's objects.

If either fails, stop and report. The next plan is then to create the missing
skirmish players at load time (approach 2), which borrows nothing.

## Behavior

- **When the fallback fires:** an AI player's Side matches no map player that
  the engine's loop considers (R1's predicate). This includes native Sides on
  maps that lack their skirmish player, which previously got no base either.
- **The pick:** the first map player in the map's own order that the loop
  considers a skirmish candidate. Deterministic per map; no configuration.
- **Donors:** with a `[Sides]` donor, S2 still redirects the match to the
  donor's skirmish player. If the map doesn't have that one either, the
  fallback applies.
- **Sharing:** several AI players may resolve to the same map player; vanilla
  does the same when two AI players pick Men.
- **No candidates at all** (a map with no skirmish players): unchanged
  vanilla behavior, logged.

## Configuration

A new `[Patches]` key, `S4` (SkirmishFallback), default on. Unlike S1-S3 it is
installed whether or not `[Sides]` has entries. `S4=0` restores vanilla
behavior.

## Implementation constraints

The same rules as the existing patches:

- each site found by signature in 2.01 and verified on Age of the Ring's
  `game.dat`; refused unless found exactly once with the expected bytes;
- no hardcoded absolute addresses; the existing hook machinery
  (trampolines, `fu_hook_common`) unless R1 shows it can't express the patch;
- the patch is all-or-nothing: if any of its sites is refused, none of them
  is written.

## Logging

At startup, the S4 line in the patch summary, as for every patch. During a
game, one line per fallback, appended to `dinput8.log`:

```
skirmish: Arthedain has no skirmish player on this map, using SkirmishMen
skirmish: Arthedain has no skirmish player on this map and the map has none to borrow
```

Logging must not block or fail the game. If the log can't be opened, the line
is dropped.

## Error handling

- Site not found, ambiguous or with unexpected bytes: S4 refused and logged;
  the rest of the DLL works as before.
- Unknown executable: the whole DLL refuses, as now.
- The fallback never changes a player whose Side already matched.

## Testing

- `tools/verify_sites.py`: the S4 sites for 2.01 and the Age of the Ring check
  (same bytes at the same addresses, no overlap with the mod's edits).
- Unit test of the pick logic in `dll/tests`: no candidates; donor's player
  present; donor's player absent; first-in-order pick.
- `test_engine`: S4 applied on the 2.01 baseline and on Age of the Ring, and
  the summary line counts.
- In-game checklist in the README (manual): a Realms Arthedain AI on the
  unedited `map mp adorn river` builds a base and attacks; the log shows the
  fallback line; with `S4=0` it stands idle as before.
