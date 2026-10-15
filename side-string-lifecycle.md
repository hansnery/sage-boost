# Side string lifecycle — and a runtime faction-unlocker patch map

Research date: 2026-10-13, against upstream checkout `C:\repositories\Open-BFME-2`
and the retail 1.06 image in its `baselines/bfme2/workshop-vanilla-1.06/files/game.dat`.
All addresses are RVAs (code) / VAs (data), vanilla 1.06, image base 0x400000.
Everything in §1–§3 is byte-verified (disassembly done with capstone on the retail
image); §4–§5 are design notes for the planned DLL.

Companion to [faction-limit-findings.md](faction-limit-findings.md).

## 1. Answer: how a Side string is created

A faction's `Side` string has exactly one birthplace — the `Side = X` field of a
`PlayerTemplate` block in `Playertemplate.ini` — and exactly one storage slot —
`PlayerTemplate+0x18` — from which everything else is copied or derived.

### 1.1 INI load path (byte-verified)

```
INI block token "PlayerTemplate"
  → block-parse node VA 0xDB93BC (initializer 0x007AD550; token → parse 0x001FEFEC)
  → PlayerTemplateStore::parsePlayerTemplateDefinition 0x001FEFEC (556 B)
      reads block name ("FactionMen"), makes NameKey, looks up existing template
      → Rva001FE70F::rva001FE70F 0x001FE70F (99 B) parses the block's fields:
          MultiIniFieldParse add g_00BE2070 (VA 0xBE2070, 62 rows, extraOffset 0)
          add second table (VA 0xC1DB18, extraOffset 0x154)
          INI::initFromINIMulti
          then copies the Side string +0x18 → +0x154  (AsciiString::set)
      → back in the parser: playable-set decision (§2)
```

The generic field parser used for string fields is `0x0042F11E`; it receives the
destination offset through the FieldParse `userData` slot. (Unrecovered as a named
row, but proven by the table: every row using it — `Side`, `StartingBuilding`,
`StartingUnit0-9`, … — carries a template offset in userData that matches the
independently proven layout: display name +0x14, side +0x18, playable flag +0x151.)

### 1.2 The recovered PlayerTemplate field table (VA 0xBE2070)

Dumped from the retail image (62 rows; token / parser / dest offset):

| field | parser | offset | notes |
|---|---|---|---|
| **Side** | 0x0042F11E | **+0x18** | the faction side string; copied to +0x154 after parse |
| PlayableSide | 0x0042E850 (bool) | +0x151 | gate for the playable set |
| DisplayName | 0x0073987B | +0x14 | UnicodeString |
| StartMoney | 0x005FD1B7 | +0x2C | |
| PreferredColor | 0x0042F259 | +0x38 | |
| StartingBuilding | 0x0042F11E | +0x44 | |
| StartingUnit0..9 | 0x0042F11E | +0x48..+0x6C | |
| StartingUnitOffset0..9 | 0x0042F507 | +0x70..+0xDC | |
| StartingUnitTacticalWOTR | 0x0042E896 | +0xE8 | |
| ProductionCostChange / TimeChange / VeterancyLevel | 0x005FDF26 / 73 / DB | map fields | |
| IntrinsicSciences / IntrinsicSciencesMP | 0x00739AF1 | +0x120 / +0x12C | |
| PurchaseScienceCommandSet(MP) | 0x0042F11E | +0x138 / +0x13C | |
| SpecialPowerShortcutCommandSet / WinName / ButtonCount | | +0x140 / +0x144 / +0x148 | |
| **IsObserver** | 0x0042E850 (bool) | **+0x150** | its index lands in store+0x24 |
| ScoreScreenImage / LoadScreenImage / LoadScreenMusic | | +0x164 / +0x168 / +0x14C | |
| HeadWaterMark / FlagWaterMark / EnabledImage / SideIconImage | 0x0042F11E | +0x16C..+0x178 | |
| BeaconName | | +0x17C | |
| InitialUpgrades | 0x0042F196 | +0x180 | |
| DefaultPlayerAIType | 0x0042F11E | +0x1B0 | per-faction AI |
| SpellBook / SpellBookMp | 0x0042F11E | +0x1B4 / +0x1B8 | |
| Evil | 0x0042E850 (bool) | +0x1BC | |
| BuildableHeroesMP / BuildableRingHeroesMP | 0x0042F196 | +0x18C / +0x198 | |
| SpellStoreCurrentPowerLabel / MaximumPowerLabel | | +0x1C0 / +0x1C4 | |
| ResourceModifierObjectFilter / Values | 0x00761CA5 / 0x005FDD33 | +0x1C8 / +0x1CC | |
| MultiSelectionPortrait | | +0x1D8 | |

Second table (VA 0xC1DB18, extraOffset +0x154): `IntrinsicSciencePurchasePoints`
(+0x158), `MaxLevelMP` (+0x15C), `MaxLevelSP` (+0x160) — the rank/level block;
+0x154 itself holds the Side copy written after parsing.

### 1.3 What the Side string drives (all byte-verified)

1. **Playable-set dedup** — inside 0x001FEFEC: a new playable template is only
   inserted into `ThePlayerTemplateStore->playableIndices` (set<int> at +0x18) if
   no member template has an equal Side. Duplicates are silently dropped from the
   set (the template itself stays in the vector).
2. **Campaign restriction** — same function: when `GlobalData+0x9D4` is 1..2,
   only Sides `Dwarves` / `Wild` stay playable (string compares against
   VA 0xBE2458 / 0xBE2450).
3. **Faction id** — `PlayerTemplate::rva001FD234` (11 B) passes +0x18 to
   `Rva0033A3F4Lookup` 0x0033A3F4: `"Goblins"` → 5, else case-insensitive index
   into the 7-name table at VA 0xDBE9B0 (`Men, Elves, Dwarves, Isengard, Mordor,
   Wild, Neutral`), else 7. This id feeds Create-A-Hero faction masks
   (`GetFactionMaskType` 0x00219F8E, tested `mask[id>>5] & 1<<(id&31)`), rank
   icons (`AptRankIcon%s%d`) and online stats.
4. **Player copy** — `Player::init` (see `Code/GameEngine/Source/Common/RTS/PlayerInitFromDict.cpp`):
   `side = pt->side` (Player+0x58).
5. **AI base/scripts selection** — `Player::initFromDict` and
   `Player::rva002AC4D4` (0x002AC4D4): the map's sides list carries dict key
   `playerFaction` = a template *name* (`FactionMen`); resolving it and comparing
   its Side with the player's Side picks which map side — base, build lists,
   scripts — the AI player uses. No matching skirmish side → the player is
   "quietly made nonskirmish" (retail debug string).
6. **Lobby dropdown filter** — populate loop iterates `playableIndices`, then
   `Rva00441E4DFaction` 0x00441E4D drops any Side not in the map start position's
   faction set (names normalized to `Faction` + Side).
7. **AI/Random pick** — `populateRandomSideAndColor` 0x002444BE narrows the
   candidate pool to templates whose name is in the start position's faction set,
   then by the slot hero's CAH mask, then picks randomly and calls
   `GameSlot::setPlayerTemplate` 0x00400E33.

## 2. Retail disassembly of the three gates (patch targets)

### 2.1 GameSlot::setPlayerTemplate 0x00400E33 (108 B)

```asm
0x00400E3E  mov esi, [0xDFE0D0]        ; ThePlayerTemplateStore
0x00400E3E  add esi, 0x18              ; the playable set
0x00400E41  cmp [esi+4], 0             ; empty set → do nothing at all
0x00400E4E  test edi, edi / jl         ; negative templates skip the lookup
0x00400E5B  call 0x388F63              ; set::find(templateIndex)
0x00400E60  cmp eax, ebx               ; ebx = end()
0x00400E65  jne 0x400E6D               ; found → keep requested index
0x00400E67  mov eax,[ebx+8]; mov edi,[eax+0x10]   ; miss → edi = begin()->key
0x00400E73  test byte [eax+0x9D4], 3   ; GlobalData flag
0x00400E7C  cmp edi, -1                ; Random
0x00400E7F  jne 0x400E89
0x00400E81  mov eax,[esi]; mov eax,[eax+8]; mov edi,[eax+0x10]  ; → begin()->key
0x00400E89  mov [ecx+0x18], edi        ; store on the slot
0x00400E8C  cmp edi, -2 / jg           ; ≤ observer → clear startPos etc.
```

### 2.2 parsePlayerTemplateDefinition 0x001FEFEC (556 B) — new-template path

```asm
0x001FF11F  or edi, 0xffffffff         ; edi = -1 ("reject") register
0x001FF122  cmp byte [ebp-0xA4], 0     ; IsObserver (+0x150): index24=idx, reject
0x001FF1BB  cmp byte [ebp-0xA3], 0     ; PlayableSide (+0x151): false → reject
0x001FF1C8  eax = store+0x18 set; esi = begin()
0x001FF1D4  cmp esi, eax; je 0x1FF144  ; empty set → keep index
; dedup loop:
0x001FF1DC  push [esi+0x10]; call 0x1FD3C6   ; getNthPlayerTemplate(idx)
0x001FF1F2  call 0x69D6                      ; other->side == npt.side ?
0x001FF1F7  test eax, eax
0x001FF1F9  je 0x1FF141                      ; equal → index = -1 (reject)   ← PATCH C
0x001FF1FF  next; loop until end(); jmp 0x1FF144
0x001FF144  mov eax,[0xDFE758]; mov eax,[eax+0x9D4]
0x001FF14F  test eax,eax; jle 0x1FF183       ; restriction off              ← PATCH C2
0x001FF153  cmp eax,2; jg 0x1FF183
0x001FF158  AsciiString == "Dwarves" (VA 0xBE2458)
0x001FF16C  AsciiString == "Wild"    (VA 0xBE2450)
0x001FF183  cmp [ebp-0x10], edi; je skip
0x001FF199  call 0xBC15D                     ; set::insert(index) at store+0x18
```

### 2.3 Rva00441E4DFaction 0x00441E4D (129 B) — lobby map filter

```asm
0x00441E5B  add esi, 8                 ; holder+8 = the position's faction set
0x00441E5E  cmp [esi+4], 0
0x00441E62  jne 0x441E68               ; non-empty → do the find             ← PATCH D
0x00441E64  mov al, 1; jmp epilogue    ; empty → allow
0x00441E9F  set::find("Faction"+Side) → setne bl; mov al, bl
```

### 2.4 populateRandomSideAndColor 0x002444BE (926 B) — AI/Random map filter

```asm
0x00244615  call 0x3024BC              ; TheMapCache->findMap(map)
0x0024461C  cmp eax, edi; je 0x2446C3  ; no metadata → keep full pool
0x0024462C  lea esi, [metadata + startPos*0x14 + 0x54 + 8]
0x0024462F  cmp [esi+4], edi
0x00244632  je 0x2446C3                ; empty faction set → keep full pool  ← PATCH E
; else narrow candidates by set membership, then hero mask (0x219F8E), then
; random pick → call 0x400E33 (setPlayerTemplate)
```

### 2.5 The 7-name table and both lookups

Table at VA 0xDBE9B0: 7 name pointers + **NULL terminator at slot [7]**, then
unrelated data (`NONE/HOLD/KILL/SPAWN` …) — in-place growth would overwrite the
neighbor, so a patcher must relocate.

```asm
Rva0033A3F4Lookup 0x0033A3F4 (60 B):
0x0033A3F8  push 0xC0EBCC ("Goblins"); call 0x69B1; je → return 5      ← optional off
0x0033A40D  push dword ptr [esi*4 + 0xDBE9B0]   ; table base (disp32 @0x0033A410)
0x0033A418  call 0x37980 (compareNoCase)
0x0033A422  cmp esi, 7                          ; bound (imm8 @0x0033A424)
0x0033A427  return 7 on miss

Rva003BD52F 0x003BD52F (61 B):
0x003BD53C  mov esi, 0xDBE9B0                   ; base (imm32 @0x003BD53D)
0x003BD543  loop until *p == NULL (self-terminating), _strcmpi via IAT [0xBBA518]
0x003BD536  return 6 on NULL arg / miss
```

## 3. Runtime patch map (accelerator-style DLL, vanilla 1.06)

| # | address | original bytes | patched | effect |
|---|---|---|---|---|
| A | 0x00400E65 | `75 06` | `EB 06` | `setPlayerTemplate`: a faction index outside the playable set is accepted instead of being replaced by the first playable one. This is the "selecting it lands on another faction" fix. |
| B (opt) | 0x00400E79 | `03` | `00` | disables the Random→first-playable fallback (only ever fires when `GlobalData+0x9D4 & 3`). |
| C | 0x001FF1F9 | `0F 84 42 FF FF FF` | `90 ×6` | parse-time Side dedup disabled: every playable template enters the set, duplicates included. |
| C2 (opt) | 0x001FF151 | `7E 30` | `EB 30` | disables the Dwarves/Wild-only restriction mode. |
| D | 0x00441E62 | `75 04` | `EB 04` | lobby dropdown ignores map AllowedFactions — every playable faction selectable on every map. |
| E | 0x00244632 | `0F 84 8B 00 00 00` | `E9 8C 00 00 00 90` | AI/Random pick ignores map AllowedFactions — AI pool is all playable templates. |
| F | 0x0033A410, 0x0033A424, 0x003BD53D | `B0 E9 DB 00` / `07` / `B0 E9 DB 00` | new table VA / N / new table VA | extend the 7-name table: VirtualAlloc a `char*` array of N names + NULL, point both lookups at it, raise the 0x0033A3F4 loop bound. Needed only for CAH masks / rank icons / stats of genuinely new Sides. |
| F2 (opt) | 0x0033A404 | `75 04` | `EB 04` | removes the Goblins→5 special case so "Goblins" can be a normal table name. |

Notes:

- **What deliberately stays data-driven:** `PlayableSide`/`IsObserver` flags
  (INI), hero-mask narrowing in 0x002444BE (per-hero CAH data), the 8-slot and
  20-player limits (unrelated to factions), and all count bounds (vector is
  dynamic; lobby validation `>= getPlayerTemplateCount()` needs no patch).
- **Byte-identity caveat:** patches A–E change `game.dat` code the upstream
  project byte-verifies; they can only live in this project's runtime DLL, never in
  upstream. A/E/D/C are `jcc`→`jmp`/NOP flips of retail instructions — cheap to
  also express as signature patterns (the accelerator/SAGE-Unlocked approach)
  so they survive a rebased build.
- **Slot caveat for A:** with the fallback gone, a stale index (e.g. from a
  saved options string referencing a modded template list) is stored as-is;
  lobby validation still rejects out-of-range values, but an in-range dead
  index now sticks. Acceptable for a mod tool; revisit if it shows.

## 4. Two implementation strategies for "more factions"

**Strategy 1 — clone with a reused Side (patches A + C only).** Add a
`PlayerTemplate` block whose `Side = Men` (any existing side). With C it enters
the playable set; with A it can be selected; it inherits everything keyed by
Side: map AllowedFactions, the AI skirmish side (bases, build lists, scripts),
CAH mask bits, rank-icon names, stats. Zero map edits, zero table work. Limit:
it is cosmetic-and-roster-wise a "Men variant" — display name, start money,
spell book, heroes etc. all come from its own block and can differ freely.

**Strategy 2 — genuinely new Side (patches A, C, D, E, F + map data).** Unique
`Side = MyFaction` needs: the table extension (F) so the CAH/stats id isn't the
fallback 7, and per-map data if D/E are not installed (WorldBuilder writes
`AllowedFactions` per start position and the map needs a skirmish side with
`playerFaction = FactionMyFaction` for the AI to have a base). With D+E the map
gates are bypassed entirely, but the AI-base lookup (§1.3 item 5) is still
side-string matching against the map's skirmish sides — a vanilla map has no
skirmish side for the new side, so the AI player falls back to
"nonskirmish" (no base). Long-term the DLL would also want to redirect the
skirmish-side match (0x002AC4D4 / `Player::initFromDict`) to a donor faction's
skirmish side for unknown sides — same pattern as the other patches, address
TBD from 0x002AC4D4's callers when that work starts.

WorldBuilder is a separate exe (`worldbuilder.exe` ships next to `game.dat` in
the baseline) with its own fixed faction list; Strategy 2 map authoring needs
either a WB patch of its own or hand-edited map files.

## 5. Open items

- CAH mask storage: where the per-class/subclass mask words live (INI
  `CreateAHero*` data vs `.data`) — decides whether Strategy 2 heroes need data
  edits or another patch.
- 0x002AC4D4's caller 0x002AFCDF (unclaimed) — the skirmish-side redirect point
  for Strategy 2's AI-base fallback.
- Signature patterns for each patch site (robustness across game versions) —
  the A/E/D/C sites are short and distinctive; build them when the DLL project
  starts.
- Verify there is no fourth gate in the campaign/LivingWorld paths
  (`LivingWorldCampaign.cpp` resolves factions separately).

## Corrections (appended 2026-10-12)

- **Patch D in §3 is inverted.** At 0x00441E62, `jne` jumps *to* the find path;
  `EB 04` would always run the find and never take the empty-set "allow" path.
  `0x00441E4D` also has a second caller (Living World, via 0x0059E861). The DLL
  instead patches the lobby call site: 0x004426DA `74 4F` → `90 90`. Details in
  [DISCREPANCIES.md](DISCREPANCIES.md) §1.
- §2.1: `mov esi, [0xDFE0D0]` is at 0x00400E38 (0x00400E3E is `add esi, 0x18`).
