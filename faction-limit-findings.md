# Faction limit findings — Open-BFME-2

Research date: 2026-10-12, against the upstream checkout at
`C:\repositories\Open-BFME-2`. All paths are relative to that tree; retail
RVA addresses are as byte-verified there.

## TL;DR

There is **no "number of factions" constant in the code**. Factions are
`PlayerTemplate` blocks parsed from the game's `Playertemplate.ini` into
`ThePlayerTemplateStore`, and the template vector grows with the INI.
**But** adding factions via INI alone does not work, for three reasons the
reversed code makes precise:

1. A template is only added to the store's *playable set* if its `Side`
   string is unique (duplicates silently dropped), and selecting any faction
   index not in that set silently falls back to the first playable faction.
2. Both the lobby dropdown and the AI's random-faction pick are filtered by
   the **map's** per-start-position faction list (`AllowedFactions`, written
   by WorldBuilder). Vanilla maps never list a new faction.
3. Every template is projected onto a hard-coded **7-name table**
   (`Men, Elves, Dwarves, Isengard, Mordor, Wild, Neutral`, plus `Goblins`
   special-cased). Create-A-Hero masks, rank icons and online stats all key
   off that id; a new side always gets the fallback id.

The "~11" often remembered as the limit is most plausibly the count of
`PlayerTemplate` blocks in vanilla `Playertemplate.ini` (6 playable factions
plus Wild / Neutral / Observer / Referee / Civilian support templates) — a
data count, not a code constant. This repo ships source only, so the INI was
not counted here.

---

## 1. How factions are stored

- `PlayerTemplateStore` (global `ThePlayerTemplateStore`) — reversed in
  `Code/GameEngine/Source/Common/RTS/PlayerTemplate.cpp:100-116`:

  | offset | member |
  |---|---|
  | +0x04 | `showReload` bool |
  | +0x0C | `m_playerTemplates` — `std::vector<PlayerTemplate>`, element stride **0x1DC** |
  | +0x18 | `playableIndices` — `_STL::set<int>` of store indices that passed the playable filter |
  | +0x24 | `index24` — index of the template with the +0x150 flag set |

- Count is dynamic: `getPlayerTemplateCount()` = `(m_finish - m_start)`.
- Parse entry point: `PlayerTemplateStore::parsePlayerTemplateDefinition`
  (WorldBuilder named it, WB address 0x00A78AA0; retail body reversed in
  `PlayerTemplate.cpp:181`). Handles override (`mode==2`) and reload
  (`mode==5`), otherwise `push_back`s a new 0x1DC-byte template.
- Playable flag is `PlayerTemplate+0x151` (`PlayableSide = Yes`); display
  name at +0x14, name key at +0x10, side string at +0x18.
- Non-playable lookups by name: only `FactionObserver` and `FactionCivilian`
  are hard-referenced in code.

## 2. Gate 1 — playable-set dedup + silent fallback (the "occupies another faction" bug)

This matches the modding symptom: *added faction appears in a dropdown but
selecting it lands on a different faction.*

**Dedup at parse time** (`PlayerTemplate.cpp:206-220`):

```cpp
if (npt.flag151) {                       // PlayableSide = Yes
    for (idx in playableIndices) {
        other = getNthPlayerTemplate(idx);
        if (other && other->side == npt.side) { index = -1; break; }  // dropped
    }
} else index = -1;                       // not playable at all
if (index != -1) playableIndices.insert(index);
```

A new block whose `Side` string equals an existing playable one never enters
`playableIndices`.

**Fallback at selection time** — `GameSlot::setPlayerTemplate`, retail
0x00400E33 (108 B), reversed in
`Code/GameEngine/Source/GameNetwork/GameSlotSetPlayerTemplate.cpp`:

```cpp
_STL::map<int,int> &map = ThePlayerTemplateStore->m_map;  // +0x18 = the set
if (!map.empty()) {
    if (playerTemplate >= 0) {
        it = map.find(playerTemplate);
        if (it == map.end())
            playerTemplate = map.begin()->first;      // ← silent replacement
    }
    if ((TheWritableGlobalData->m_flag9D4 & 3) != 0 && playerTemplate == -1)
        playerTemplate = map.begin()->first;          // Random → first playable
    ...
}
```

The member viewed as `map<int,int>` here and as `set<int>` in the parse TU is
the same `_Rb_tree` at +0x18 (layout-identical; `find`/`begin`/`empty` behave
identically). Net effect: any faction index outside the playable set — e.g.
a template that was deduped away, or beyond the set — silently becomes the
lowest playable index.

Who can offer a non-set faction: the **Quick Match** side combo iterates *all*
templates (only filtering the per-template `isPlayableSide` flag, not the
set) — `Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/QuickMatchPopulateSideComboBoxRva005BB3A1.cpp:172-200`.
The main lobby panel iterates the set itself (see gate 2), so there the
deduped faction simply never shows.

## 3. Gate 2 — map start-position faction lists (dropdown filter + AI picks)

Each map start position carries a faction-name set (`AllowedFactions`, written
by WorldBuilder per player). Two consumers:

**Dropdown filter** — `Rva00441E4DFaction`, retail 0x00441E4D (129 B),
`Code/GameEngine/Source/GameClient/GUI/Rva00441E4DFaction.cpp`: given the
start-position object and a side name, if its `map<AsciiString,AsciiString>`
at +8 is non-empty, the name (normalized to `"Faction" + Side`) must be a key,
else the faction is not offered. Called from the lobby populate
`Code/GameEngine/Source/GameClient/GUI/AptMpGameSetupRva00440BDF.cpp:1141`
(`if (start && !Rva00441E4DFaction(start, fac->side)) continue;`). That
populate loop iterates `playableIndices` and also honors
`TheWritableGlobalData->factionRestrictions & 3` (disables Random).

**AI / Random pick at game start** — `populateRandomSideAndColor`, retail
0x002444BE (926 B), `Code/GameEngine/Source/GameLogic/System/GameLogicInit.cpp:3806-3900`:

1. Builds the candidate pool from all templates with `isPlayableSide()`.
2. For each occupied slot with template −1 (Random), narrows the pool:
   - to templates whose name is in
     `MapMetaData->m_startPositions[slot->getStartPos()].m_factions`
     (skipped only when that set is empty), then
   - by the slot hero's Create-A-Hero faction mask.
3. Picks randomly; if the picked template isn't playable → −1, loop again.

A vanilla map never lists a new faction, so **AI can never pick it there** —
and skirmish AI additionally builds from the map's per-player base/build
lists, which is the "AI needs a base" observation. WorldBuilder (a separate
exe, not in this repo — its twin functions are unnamed, e.g. WB
`parseStartMoney` twin 0x00A76C10) has its own fixed faction list when
authoring that map data.

## 4. Gate 3 — the fixed 7-name faction table

- Table: `g_rva0033A3F4Table[7]` at retail data VA **0x00DBE9B0**
  (data ledger row 0x009BE9B0, `.data`, provisional, **converged**):
  `"Men", "Elves", "Dwarves", "Isengard", "Mordor", "Wild", "Neutral"`.
- Lookup `Rva0033A3F4`, 0x0033A3F4 (60 B),
  `Code/GameEngine/Source/Common/Rva0033A3F4Lookup.cpp`: `"Goblins"` is
  special-cased to return 5 (shares Wild's slot); anything else scans the
  table case-insensitively; miss returns 7.
- Per-template wrapper 0x001FD234 returns the table id for a template's side,
  **7 when there is no template** (`RVA002444BE_FACTION_NONE`).
- Sibling `Rva003BD52F` (61 B, `Code/GameEngine/Source/Common/GapBodiesGE09.cpp:54`)
  returns 6 when absent.
- Consumers:
  - **Create-A-Hero faction masks**: `CreateAHeroManager::GetFactionMaskType`
    0x00219F8E (85 B) returns a mask tested as
    `mask[faction >> 5] & (1 << (faction & 31))` — one 32-bit word covers ids
    0–31, and populateRandomSideAndColor filters by it whenever a hero is
    chosen. A new faction's id 7 is only "allowed" if that bit is set in the
    hero's mask.
  - **Rank icons**: `AptRankIcon%s%d` built from `g_00DBE9B0[side]`
    (`Code/GameEngine/Source/Common/Rva00559B64GetImage.cpp`).
  - **Online stats**: wins/losses-vs tables bind to the same data
    (`UserPreferencesRva00535CE4.cpp`, `UserPreferencesWinsLossesVs.cpp`,
    `Rva005C1AE4Points.cpp` — three TUs, one converged address).
- Consequence: a new side name always projects to id 7 ("none") and sits
  outside every fixed-size structure above.

## 5. What is *not* a limit

- `MAX_SLOTS = 8` — lobby/skirmish slots (simultaneous players, per-TU enums,
  e.g. `SkirmishGameInfoCtor.cpp:16`).
- `MAX_PLAYER_COUNT = 20` — fixed 20-slot in-game `PlayerList`, fixed in class
  layouts (doc `docs/doxygen/pages/subsystems/rts_model.md` "Hard-coded" note;
  template ids are 16-bit).
- Slot faction is a full 32-bit `Int` (`GameSlot+0x18`); the GameSpy options
  string writes `PlayerTemplate=%d` — no bit-packing cap. Lobby validation
  bounds-checks against the dynamic store count
  (`GameInfoPlayerCounts.cpp:1050`).

## 6. What a runtime patch would need to change

Ordered by the gates above:

1. **Parse/selection** — relax or remove the side-name dedup in
   `parsePlayerTemplateDefinition`, and make `GameSlot::setPlayerTemplate`
   (0x00400E33) accept any valid index (0 ≤ idx < template count) instead of
   falling back to the set's first element.
2. **Map data** — either extend `AllowedFactions` in every map (data edit, or
   a tool to patch maps), or change the two consumers (`Rva00441E4D` and
   `populateRandomSideAndColor` 0x002444BE) to ignore/treat-empty the map
   lists for unknown sides.
3. **Fixed table** — extend `g_rva0033A3F4Table` at 0x00DBE9B0 with new side
   names and teach the two lookups (0x0033A3F4, 0x003BD52F) the new extent;
   keep `Goblins→5` semantics or give Goblins its own slot. Note the address
   is **converged data** shared by four TUs (lookup, rank icons, 2× stats).
4. **Heroes/stats** — Create-A-Hero masks: either set the new factions' bits
   in mask data, or extend the mask test; rank icons and stats gain entries
   per new id.

Reality check: upstream verifies every body byte-identical to retail, so
these changes cannot land there — they are exactly the divergence this
project is for. The 32-bit CAH mask word caps the *hard* extension at 32
factions without also changing the mask layout.

## 7. Reference files

| What | File / address |
|---|---|
| Store layout + parse/dedup | `Code/GameEngine/Source/Common/RTS/PlayerTemplate.cpp` |
| Selection fallback 0x00400E33 | `Code/GameEngine/Source/GameNetwork/GameSlotSetPlayerTemplate.cpp` |
| Lobby faction combo (set iterate + map filter) | `Code/GameEngine/Source/GameClient/GUI/AptMpGameSetupRva00440BDF.cpp:1100-1152` |
| Start-position faction check 0x00441E4D | `Code/GameEngine/Source/GameClient/GUI/Rva00441E4DFaction.cpp` |
| AI/Random pick 0x002444BE | `Code/GameEngine/Source/GameLogic/System/GameLogicInit.cpp:3788-3900` |
| Fixed table + lookup 0x0033A3F4 | `Code/GameEngine/Source/Common/Rva0033A3F4Lookup.cpp` |
| Same-table lookup (ret 6) 0x003BD52F | `Code/GameEngine/Source/Common/GapBodiesGE09.cpp:54` |
| Rank icons from table | `Code/GameEngine/Source/Common/Rva00559B64GetImage.cpp` |
| CAH masks 0x00219F8E | `Code/GameEngine/Source/Common/Rva00219B9EAccessor.cpp:497-522` |
| Quick Match combo (iterates all templates) | `Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/QuickMatchPopulateSideComboBoxRva005BB3A1.cpp` |
| Lobby slot validation | `Code/GameEngine/Source/GameNetwork/GameInfoPlayerCounts.cpp:1049-1057` |
| RTS model overview doc | `docs/doxygen/pages/subsystems/rts_model.md` |
