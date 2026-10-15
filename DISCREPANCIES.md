# Discrepancies between the research docs and the retail image

Checked 2026-10-12 against the baseline `game.dat` (FileVersion 1.6.2429.30210,
TimeDateStamp 0x00564544) with `tools/verify_sites.py` (capstone 5.0.7). Where
the docs and the image disagree, the image wins and the patch was re-derived.

## 1. Patch D is inverted in the docs. Re-derived at the lobby call site.

**Docs** (`side-string-lifecycle.md` §3, `HANDOFF.md` §4): 0x00441E62
`75 04` → `EB 04`, "always takes the empty-set allow path".

**Image** (`Rva00441E4DFaction` 0x00441E4D):

```
00441E5E  83 7E 04 00     cmp dword ptr [esi+4], 0   ; faction set size
00441E62  75 04           jne 0x441E68               ; non-empty -> do the find
00441E64  B0 01           mov al, 1                  ; empty -> allow
00441E66  EB 59           jmp epilogue
00441E68  ...             set::find("Faction"+Side) -> al = found
```

`EB 04` makes the jump unconditional, so the function *always* does the
membership test and never takes the allow path. That is the opposite of what
the docs say it does. It would also hide every faction on start positions with
an empty faction list, which vanilla treats as "all allowed". The fix that
matches the stated intent would be `75 04` → `90 90` (fall through to "allow").

**Why the patch site moved as well:** the helper has two callers in the image:

| caller | context |
|---|---|
| 0x004426D1 | lobby faction-combo populate (`AptMpGameSetupRva00440BDF.cpp:1141`, `if (start && !Rva00441E4DFaction(start, fac->side)) continue;`) |
| 0x0059E868 | `Rva0059E861` wrapper ← 0x0059E9B5, next to the Living World starting-restriction code (`LivingWorldStartingRestrictionParse.cpp`) |

Patching inside the helper would also switch off the Living World (War of the
Ring) check, which is out of scope and still unverified (`side-string-lifecycle.md`
§5, last open item). The DLL therefore patches the lobby caller only:

```
004426D1  E8 77 F7 FF FF  call Rva00441E4DFaction
004426D6  84 C0           test al, al
004426D8  59 59           pop ecx ; pop ecx
004426DA  74 4F           je 0x44272B                ; not allowed -> skip faction
```

**Patch D as implemented:** RVA 0x004426DA, `74 4F` → `90 90`. The effect is
the one the docs intended (the lobby dropdown ignores the map's AllowedFactions)
and the Living World path is left alone.

## 2. Address notation is mixed

`HANDOFF.md` §3 says "the addresses below are RVAs". That holds for the **code**
addresses (0x00400E33, 0x001FF1F9, …: VA = 0x400000 + RVA, e.g. `setPlayerTemplate`
runs at VA 0x00800E33). The **data** addresses (0xDFE0D0 `ThePlayerTemplateStore`,
0xDFE758 GlobalData, 0xDBE9B0 faction table, 0xBE2070 field table, 0xBE2458
"Dwarves") are VAs, as they appear in instruction operands. Call targets in the
§2 listings (`call 0x388F63`) are RVAs. No patch is affected; the DLL never uses
absolute addresses.

## 3. Minor listing slip in `side-string-lifecycle.md` §2.1

`mov esi, [0xDFE0D0]` is at 0x00400E38, not 0x00400E3E. The line at
0x00400E3E is `add esi, 0x18`; the doc lists both at 0x00400E3E.

## 4. The baseline image is a modified executable

The baseline's version resource has `CompanyName = *DEV!ANCE*`. Its entry point
is an ordinary MSVC CRT start inside `.text`, and the last section is a rebuilt
import table, so `.text` is plain code on disk. That is why the DLL can patch
from DllMain. An executable whose code is encrypted on disk and decrypted later
by a protection stub would show no signature matches at DllMain time; the DLL
then refuses every patch and the game runs unmodified. Only the baseline image
has been verified.

## 5. Dates

`side-string-lifecycle.md` gives its research date as 2026-10-13; `README.md`
and `faction-limit-findings.md` say 2026-10-12. Cosmetic.

## Everything else matched

Patches A, B, C, C2 and E: RVAs, original bytes and patched bytes are exactly as
documented (`E9 8C 00 00 00 90` keeps E's branch target 0x002446C3). So are the
F/F2 operand locations (0x0033A410, 0x0033A424, 0x003BD53D, 0x0033A404). F/F2
themselves were not implemented; see section 6.

## 6. Strategy 2: donor aliases instead of relocating the name table (F/F2)

The docs plan a new Side by growing the 7-name faction table at VA 0x00DBE9B0
(patch F) and adjusting the Goblins special case (F2). The image argues against
that:

- The table has about 20 direct code references, and faction ids index
  parallel data (Create-A-Hero masks, rank data). An 8th id would need every
  reference repointed and new data for every array. Nobody has mapped all of
  that data.
- There is a second 7-name table at VA 0x00DB9D18, referenced only from data
  (0x009BA0A0), and an Isengard/Mordor pair at 0x00DBE378 with no references.
  Growing the first table would leave these inconsistent.
- `Rva003BD52F` (the 0x003BD53D operand) has no callers in the image.

What a new Side needs is a valid, consistent answer from the code that
hardcodes the vanilla names. The consumers found:

| hook | site (RVA) | consumer |
|---|---|---|
| S1 | 0x0033A3F4 (entry) | Side → faction id (`Goblins` → 5, table index, miss → 7). Callers: 0x001FD238 (wrapper 0x001FD234, used by the random pick 0x00244525, the lobby 0x00440849/0x004414E3, 0x0044C364, 0x0052BC83, 0x005C0A9C), 0x001FD391 (id → first template), 0x0043A834. |
| S2 | 0x002AC55F (call) | `Player::rva002AC4D4` skirmish-side match; its only caller (0x002B0249 in 0x002AFCDF) takes the non-skirmish path on a miss, which leaves the AI without a base. |
| S3 | 0x00200157 (entry) | per-Side rank thresholds (struct +0x18 primary, +0x14 fallback, then Men … Wild). |

Each takes the Side as a `const AsciiString &` argument. The implemented
approach swaps that one argument for a donor's at these three places, so a new
Side gets the donor's id, skirmish side and thresholds. Its own Side string is
unchanged everywhere else, so data keyed by Side name (AIData `SideInfo`,
`SkirmishBuildList`, object `Side =`) stays the mod's own. The patches the docs
call F/F2 are therefore not implemented. `verify_sites.py` checks that each stolen
prologue is whole, position-independent instructions with no branch landing
inside it.

## 7. ROTWK 2.01 (not covered by the research docs)

The research covers BFME2 1.06 only. The ROTWK sites were derived from the
unprotected 2.01 executable Age of the Ring ships as `game820.dat` (FileVersion
2.1.2614.37001, InternalName `BFME2`, TimeDateStamp 0x460DA09E). The retail
`game.dat` (Age of the Ring keeps it as `game.other`) has an encrypted `.text`
(entropy 8.0) and cannot be patched from a DLL.

- C, C2, E, S2 and S3: the 1.06 signatures match 2.01 unchanged, each exactly once.
- A and B: same function (`GameSlot::setPlayerTemplate`, 0x00402C5A), compiled
  with other registers. A is `75 09` at 0x00402C95, B the `03` at 0x00402CAB.
- D: the lobby call site at 0x00444DFA. It calls the same AllowedFactions
  helper (0x00444548 is instruction-for-instruction 1.06's 0x00441E4D), but the
  branch is a long `je` (`0F 84 BE 00 00 00`, rewritten to six `nop`s). An extra
  check before it (`call 0x00575942`) can skip the helper.
- S1: same function at 0x0033BDA3, with 9 table entries instead of 7. The table
  is at VA 0x00DA3AC0: Men, Elves, Dwarves, Isengard, Mordor, Wild, Angmar,
  Arnor, Neutral. The signature now wildcards the size in both games. Note: the
  1.06 table's 7th entry is `Neutral`, which the earlier docs did not list.
- S2's callee 0x000065AA is 1.06's 0x000069D6 (`compare(const AsciiString &)`).

Age of the Ring 2.02's `game.dat` is `game820.dat` plus the following changes:
- two executable sections (`.danetta`, `.angmar`) and the large-address-aware flag;
- about 25 code edits, mostly `jmp`s into those sections;
- in S1, the table size raised to 14. The table is enlarged in place with
  Dunland, Guldur, Rhun, Mirkwood, Dale and Rohan (Arnor moves to 13), and
  their name strings live in `.angmar`.

That is the patch F the 1.06 research planned, done by the mod. None of the
DLL's sites overlap these edits. That is why the DLL reads the Side table at
runtime (from S1's signature) rather than carrying a fixed list: Age of the
Ring's own Sides must not be aliasable.

## 8. Skirmish players: how an AI player gets its AI (ROTWK 2.01)

Research for `docs/specs/2026-10-12-native-sides-design.md`, done on
`game820.dat`. Every function below is byte-identical in Age of the Ring's
`game.dat`. RVAs are 2.01's.

**There are two Side matches, not one.** `Player::initFromDict` (0x002B07EF)
first runs an inline loop (0x002B091C-0x002B0995) over every map player in
`TheSidesList` (`[0x00DE77A0]`, count at +0x7C0). For each, it resolves
`playerFaction` to a PlayerTemplate and compares the template's Side (+0x18)
with the player's Side (`Player+0x58`), at 0x002B096A. A hit sets a "found"
flag. Finishing the loop without one sets "not found". Only then does it reach
`findSkirmishSide` (0x002ACFA3, called once, from 0x002B0D5E), the function
S2 hooks. It runs the same comparison (0.3.0's S2 site, 0x002AD02E) to return
the map player's index.

- **A computer player with "not found"** calls `Player::setPlayerType(0, false)`
  (0x002AA450) and becomes a human-type player with no AI object. That is the
  "quietly made nonskirmish" case: the AI stands idle.
- **With "found"**, `setPlayerType(1, true)` creates the skirmish AI (an
  0xA4-byte object). `initFromDict` then copies from the map player at
  `findSkirmishSide`'s index: its build list, and every team it owns, renamed
  to `team` + the player's name.

**Consequence for the shipped S2.** S2 only redirects the second match. A
`[Sides]` alias whose Side has no map player still fails the inline loop, so
the AI is made nonskirmish before S2 runs. BFME2 1.06 has the same inline
comparison at 0x002AFE61. The README's claim that S2 gives an aliased Side the
donor's skirmish base is therefore wrong for maps without the new Side's own
skirmish player, which is every map. The fix is part of patch S4.

**The base and the AI data come from the player's own Side.** `AIBaseBuilder`
(0x005BD03A) asks the AIBase store (`[0x00DE8BE4]`) for the bases of
`Player+0x58`, then picks one whose `GameMapToUseOn` is the current map or
`<ANY>` (0x005BCA8E). The match copies nothing else from the map player, and
the player's Side is never changed. So `AIBase`, `SkirmishBuildList` and
`SideInfo` blocks for a new Side are used as written. Only four call sites in
the image compare a player's Side with the string compare (0x004065AA): the
two matches above, an object walk in a script action (0x003C6B62), and
0x003EACB5.

**Map skirmish players are bare.** `tools/mapinfo.py` was run on 12 random
Realms in Exile maps, 6 from ROTWK's `maps.big` and 3 from
`_patch201maps.big`. Every `Skirmish<Side>` player has a name, a faction, one
default team (`teamSkirmish<Side>`), no scripts and an empty build list. Map
scripts belong to the neutral player, `PlyrCreeps`, `PlyrCivilian` or a
`Player_N`. Even original 2.01 maps lack some Sides' players (for example no
`SkirmishArnor` on `map mp adorn river`).

**Decision gate: passed.** Borrowing another Side's map player gives the AI
player an empty build list and one empty team, nothing faction-specific. Its
base, build list and AI data stay its own.

**`findSkirmishSide` reads its `this` only once**, as `this+0x58` (the Side),
at entry. The rest of its body uses globals. So the DLL can ask "does this map
have a player for Side X" by calling it with a stand-in object whose +0x58 is
X. Patch S4 relies on this.

**What 0.4.0 does with this.** S2 is now an entry hook on `findSkirmishSide`
(1.06 0x002AC4D4, 2.01 0x002ACFA3; it moves the 5-byte `mov eax, imm32`). The
new S4 is a call hook on the inline comparison (1.06 0x002AFE61, 2.01
0x002B096A). Both resolve the player's Side the same way: its own Side if the
map has a skirmish player for it, else its `[Sides]` donor if the map has
one, else the first Side in the game's table (then `Goblins`; never
`Neutral`, `Civilian` or `Observer`) the map has. If none matches, nothing
changes. "The map has one" is answered by calling `findSkirmishSide`'s
trampoline with a stand-in `this`. S4 hands the comparison the resolved Side;
S2 hands `findSkirmishSide` a stand-in `this` for it. The player's own Side
object is never written. The two are installed together or not at all, and
neither needs `[Sides]`.

The order is the Side table's, not the map's. The DLL can ask about a Side
but cannot list a map's players without walking engine structures it has not
verified. The spec's amendment 1 records this.

Two README claims from 0.3.0 were wrong: that S2 alone gives an aliased Side
the donor's skirmish base (it didn't, see above), and that such an AI runs
the donor's skirmish scripts (map skirmish players carry no scripts, and the AI
data is looked up by the player's own Side).
