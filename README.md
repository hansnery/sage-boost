# sage-boost

**More factions and smoother battles for *The Battle for Middle-earth II* and
*The Rise of the Witch-king*.**

For years, BFME2 modders have run into the same wall: the engine only really
knows the factions EA shipped. A mod could add a new faction to its files, but
the game hid it from the lobby, swapped it for another faction when you picked
it, or left its computer player standing idle all game. Breaking that faction
limit has been one of the community's longest-standing goals.

sage-boost breaks it. It is a pair of add-ons for the original games and for
big mods like Age of the Ring. You copy a couple of files
into your game folder; the game's own files are never changed, and deleting
the files you added puts everything back.

- **Break the faction limit.** New factions a mod adds show up in the skirmish
  lobby, stay selected when you pick them, and can be played by the computer,
  which builds its base and farms like any other AI. They can borrow
  Create-A-Hero and ranks from a related faction. It was first put to work
  in the Realms in Exile mod.
- **Smoother big battles (experimental).** The performance plugin cuts drawing
  work the game wastes, can draw extra frames between the game's own for
  smoother movement, and, when your PC falls behind, skips drawing a frame
  instead of letting the battle slow down. It never touches what the game
  simulates, so it is designed to be safe online, even with players who don't
  have it. It is still being tested, online play included, so treat it as a
  work in progress: the shipped settings are cautious, and results will vary
  from PC to PC.

What it doesn't do: it can't make a weak PC fast in huge battles. When a
battle is too much for your PC, you see fewer frames instead of a slower
game, and there is a limit to how much it can hide. The game isn't included:
you need your own installed copy. The faction unlocker supports skirmish and single player. Version 0.5.0
adds an experimental LAN faction-encoding fix; every LAN player must update.
A two-PC game test is still required before distributing it as a verified fix.

Created by **Annatar_BR**. The research is built on
[Open-BFME-2](https://github.com/Open-BFME/Open-BFME-2), and the approach was
inspired by SAGE-Unlocked and bfme2-accelerator (see *Credits and references*).

Working on this repo: [AGENTS.md](AGENTS.md),
[docs/STATUS.md](docs/STATUS.md) and [docs/BACKLOG.md](docs/BACKLOG.md).

## What's in this repository

| component | ships as | goes into | source |
|---|---|---|---|
| Faction unlocker | `dinput8.dll` + `dinput8.ini` | the folder with `game.dat` | `dll/` |
| Performance plugin (bfme2perf) | `bfme2perf.asi` + `bfme2perf.ini` | the game's `mss\` folder | `perf/` |

The two are independent: each works with or without the other. Both patch
the game in memory at startup, find every place they change by a byte
signature in the running executable (never by a fixed address), and leave a
feature off, with a line in their log, when its signature isn't found exactly
as expected. Only the DLLs and their INIs ship; no EA file (`game.dat`,
shaders or other assets) is ever included.

## Install

### Faction unlocker

1. Copy `dinput8.dll` and `dinput8.ini` into the folder that contains `game.dat`.
   For ROTWK that is the expansion's own folder (for Age of the Ring:
   `...\AgeoftheRing\rotwk\`), not the BFME2 folder. A mod's data folder is
   not searched by Windows, so the DLL doesn't work from there.
2. Start the game as usual. A `dinput8.log` appears next to the DLL on every start.

To uninstall, delete the two files (and the log). `game.dat` is never touched,
so checksummed launchers keep working.

> **Status:** builds, and its automated tests pass against BFME2 1.06, ROTWK
> 2.01 and Age of the Ring's executable (see *Faction unlocker: testing*). In
> game, 0.4.0 has been run on Age of the Ring with a mod that adds new Sides:
> AIs on those Sides now build their bases and farms (see *AI players on maps without
> their Side*). Vanilla BFME2 and the rest of the manual checklist below are
> still untested.

**Version gate:** the DLL patches only an executable whose version resource
has InternalName `BFME2` (both games use it) and FileVersion 1.6.x.x (BFME2
1.06) or 2.1.x.x (ROTWK 2.01). Each game has its own signature table. For
anything else (WorldBuilder, another program that happens to load
`dinput8.dll`), it logs "refused" and only forwards DirectInput.

**ROTWK executables.** The retail 2.01 `game.dat` is SecuROM-protected: its
code is encrypted on disk, so no signature is found at startup and every patch
is refused (the game runs unmodified). The DLL works with an unprotected 2.01
executable. The ROTWK signatures were made from the one Age of the Ring ships as
`game820.dat`, and they were also verified against Age of the Ring 2.02's own patched
`game.dat`. That executable already carries Age of the Ring's own hooks and a
Side table enlarged to 14 entries. None of the DLL's sites overlap Age of the Ring's changes,
and nothing in its added code jumps into a hooked prologue.

### Performance plugin

The development branch includes an opt-in **0.9.11-beta.14 paired cadence**
experiment. `[Experimental] PairPacing=1` allows one extra draw per native
update and repays small deadline overruns in later waits. It requires
`MaxFPS>=60`, `FrameSmoothing=3`, `ShadowInterval=1` and
`ExtraFrameShadows=1`; reflection preferences remain unchanged. Missing
requirements leave the old scheduler active. The default is `PairPacing=0`.
Offline adapter tests pass, but gameplay FPS and matching-beta multiplayer
still need in-game verification. This is not a launcher/stable release.
See the [behavior and verification limits](docs/specs/2026-10-19-paired-cadence-runtime.md).

1. Copy `bfme2perf.asi` and `bfme2perf.ini` into the `mss\` folder next to
   `game.dat` (for Age of the Ring: `...\AgeoftheRing\rotwk\mss\`). That
   folder already holds the game's own Miles plugins, such as `mssmp3.asi`.
2. Start the game as usual. The game's audio library (Miles) loads every
   `.asi` in that folder when the game starts its audio, and the plugin
   installs its hooks then. `bfme2perf.log` appears next to it; with the
   shipped INI (`[Timing] Enabled=1`) it also writes `bfme2perf_timing.csv`
   (one row per second) and `bfme2perf_scopes.csv`.

No file is replaced and no export is forwarded. To uninstall, delete
`bfme2perf.asi` (and its INI, log and CSVs). If the game starts with audio
off, Miles doesn't start, the plugin isn't loaded, and the game runs as
without it.

> **Status:** in development (0.9 series; the log's first line names the
> version). Its signatures are made and tested for Age of the Ring's
> `game.dat` (ROTWK 2.01 with the mod's edits) and the ROTWK 2.01
> `game820.dat` it ships; mods installed on top of Age of the Ring run on the former. Vanilla BFME2
> 1.06 isn't a target yet.

## Configuration

### `dinput8.ini` (faction unlocker)

```ini
[FactionUnlocker]
Enabled=1   ; 0 = forward DirectInput only, patch nothing
DryRun=0    ; 1 = locate + verify everything and log it, write nothing
Log=1       ; 0 = no dinput8.log

[Patches]
A=1
C=1
D=1
E=1
B=0
C2=0
S1=1
S3=1
S2=1
S4=1

[Sides]     ; NewSide=DonorSide, empty by default
;Rohan=Men
```

If the INI is missing, everything takes the defaults shown. The INI and log are
named after the DLL file, so a renamed DLL reads `<name>.ini`. What each patch
does is in *What it patches* below.

### `bfme2perf.ini` (performance plugin)

Every key is commented in the shipped [perf/bfme2perf.ini](perf/bfme2perf.ini).
A missing INI or key takes the default. In outline:

| section | keys | what they do | shipped |
|---|---|---|---|
| `[Perf]` | `Enabled`, `Log` | master switch; `bfme2perf.log` | on, on |
| `[Timing]` | `Enabled`, `Detail` | per-second CSV of where frame time goes; per-mesh timing and draw counts | on, on |
| `[Render]` | `MaxFPS` | extra frames: 30 (off), 60, 90 or 120 | 30 |
| | `FrameSmoothing` | what an extra frame moves: 0 or 1 nothing, 2 units ahead, 3 units between the last two game frames; the camera is never moved (every frame shows it where the game has it) | 3 |
| | `ExtraFrameShadows`, `ExtraFrameReflections` | redraw the shadow map or water reflection in extra frames instead of reusing the game frame's | off, off |
| | `QueueInsert` | beta: fill available rendering-queue storage directly after verifying the native insertion code; growth and unsupported inputs use the original function | on |
| | `QueueSort` | beta: preserve native rendering order with raw record moves for verified pure comparators; unsupported inputs use the original sort | on |
| | `BulkRender` | beta: process qualified skeletal rendering groups in bulk, including their first mesh; requires Instancing and verified native hooks | on |
| | `Instancing`, `InstancingVerify`, `ShadowInstancing` | draw repeated unit meshes in a few instanced draws; compare both ways every 5 s; also in the shadow map | off, on, on |
| | `ShadowBatchIgnoreLight`, `NoStencilFlushes`, `ShadowSkipParticles` | the shadow-pass cuts (below) | 1, 1, 1 |
| | `ShadowPassVerify` | every 10 s draw the shadow map with and without a cut and compare | off |
| | `ShadowInterval`, `ReflectionInterval`, `ShadowFullWhileScrolling` | redraw the shadow map or water reflection only every Nth frame (1–4); not exact, see below | 1, 1, off |
| `[CatchUp]` | `Enabled`, `MaxSkips`, `Notice`, `NoticeText`, `NoticeMinSkips` | skip drawing when behind (at most 1–3 in a row); the on-screen notice, its text, and how many frames skipped within 2 s show it | on, 1, on, `Recuperando atraso`, 6 |

`Batching` and `[Skeleton]` are read and logged only; they are not in this
build. The code's default for `NoticeText` is `Catching up`.

## Multiplayer safety

**Faction unlocker 0.5.0: experimental LAN multiplayer fix.** The original
lobby packet encodes a faction in four bits, with two values reserved for
Observer and Random. Higher faction indices wrap: Realms' Lindon (14)
becomes Observer, Mordor (18) becomes Arthedain (2), and Rhun (30) becomes
Observer again. The old arithmetic also carries into the colour bits.

Patches NW1�NW4, NR1�NR4 and NS1�NS2 replace the human and AI slot codecs
as one group. They preserve the existing two-byte colour/faction/start field
size, using seven bits for the template instead of four. Supported template
indices are 0�125, plus Observer/Random; colours -1�14 and starts -1�7 retain
their original ranges. The 390-byte LAN packet is not enlarged. Legacy
packets are rejected by the new reader; a legacy reader rejects the host's
human slot because the encoded start byte is invalid in the old format.

**Every participant must use 0.5.0 (or a compatible protocol-1 build), the
same mod files and matching unlocker settings/Side aliases.** Updating only
the host is insufficient. Old builds cannot join the updated LAN lobby.
The hooks default on even with an existing INI. Do not overwrite the user's
INI when deploying the DLL. This does not patch the GameSpy lobby protocol.

Automated checks cover both executable families and AotR, actual native
caller instructions, all supported faction/colour/start combinations, all
65,536 possible field values, the 17 reported collisions and group failure
handling. A live two-PC lobby/match test remains pending; this is not a claim
that all possible multiplayer desyncs or the reported crash are fixed.
See [the launcher release notes](docs/releases/unlocker-0.5.0.md).

**Standard performance policy (`PairPacing=0`): nothing the game logic or
the sync check sees changes.** BFME2 counts its logic in client frames: six per 5 Hz logic step,
30 per second. Running the frame loop more often would run the game faster,
and online the frame rate is shared by all players. So bfme2perf leaves that
loop alone, and a player with it can play with players without it:

- **Logic, commands, input, the network and the multiplayer sync check (CRC)**
  run in every game frame exactly as stock, at the game's own rate. The
  plugin never writes the game's frame counters or its frame limit.
- **Extra frames** run only the display draw, in the time the frame limiter
  would otherwise spend waiting, and only when they fit before the next game
  frame. They turn themselves off for the session if any check fails (a logic
  random value drawn, a game frame word or the client frame changed, a logic
  CRC change under `-verifyClientCRC`), and pause when one made a game frame
  late, since a late frame would lower the frame rate everyone plays at.
- **Catch-up** skips only the render (shadow map, reflection, scene, UI,
  present) of a frame that is behind schedule. Every frame still runs its
  whole update, so logic sees the same frames with the same inputs. The
  frame rate the network sees goes back near 30, so a slow PC stops slowing
  the others. It never skips in menus, while loading or paused, while placing
  a building or scrolling, and at most `MaxSkips` frames in a row.
- **Instancing and the shadow-pass cuts** are render-only: instancing changes
  only how unit meshes are sent to the GPU, and none of the shadow-pass
  sites is reached from logic. At the shipped settings each is built to give
  the same picture, and each has a verify mode that compares both ways in
  game and switches off what differs.

`ShadowInterval` and `ReflectionInterval` are also render-only, but not
exact: shadows or reflections can lag by up to N−1 frames. Logic changes that
would need every player on the same version (target search, pathfinding)
are deliberately out of scope. The safety comes from this design and the
run-time checks above. A network game with every player on 0.9.10 has
been played without problems; replays and a network game between a PC
with the plugin and one without are still in the test plan of the
[design notes](docs/specs/2026-10-13-perf-dll-design.md).

On the development beta branch, 0.9.11-beta.3 corrects full-rate shadows:
`ExtraFrameShadows=1` together with `ShadowInterval=1` redraws the map on
both regular and extra frames. Earlier versions could reuse an extra
frame's map on the next regular frame even at interval 1. This correction
is tested offline but not installed or released, and does not establish
a performance improvement. See the [beta.3 notes](docs/releases/perf-0.9.11-beta.3.md).

**Experimental beta.14 exception:** `PairPacing=1` permits small predicted
deadline overruns and repays them in later waits. The standard strict
fit-before-next-frame policy above does not describe this opt-in mode.
Native rates and counters remain unchanged, but gameplay speed/input and
matching-beta multiplayer still require testing. Do not infer compatibility
with other performance builds from the earlier multiplayer result.

## Building

Both need a **32-bit** MinGW-w64 GCC (e.g. [winlibs](https://winlibs.com/) i686,
MSYS2 `mingw-w64-i686-gcc`, or `i686-w64-mingw32-gcc` on Linux) and a POSIX
shell (Git Bash, MSYS2). Python 3 with `pefile` and `capstone` runs the
unlocker's verification step. Both are freestanding (no C runtime) and import
only kernel32.

```sh
cd dll
make            # -> build/dinput8.dll   (mingw32-make on Windows)
make test       # site verification + engine tests + proxy tests
make dist       # -> dist/ with dinput8.dll, dinput8.ini, LICENSE
make CROSS=i686-w64-mingw32-   # prefixed cross compiler

cd perf
make            # -> build/bfme2perf.asi
make test       # row, config, site, hook, load, fps, instancing, shadow-pass, catch-up tests
make dist       # -> dist/ with bfme2perf.asi, bfme2perf.ini
```

The unlocker's `make test` needs the BFME2 baseline image (`GAME_DAT=...` to point
elsewhere). The ROTWK baseline (`ROTWK_DAT`, default
`C:/Jogos/AgeoftheRing/rotwk/game820.dat`) and Age of the Ring's `game.dat`
(`AOTR_DAT`) are tested when present and skipped otherwise. The plugin's site
test uses the same `AOTR_DAT` and `ROTWK_DAT` when present. The tests only ever
read these images; none of them is copied into this repository.

The plugin's instanced shaders are generated at development time by
`tools/inst_shaders.py` from `perf/shaders/inst.hlsl` into
`perf/src/inst_shaders.h`. It reads the local `Shaders.big` to recognise the
game's shaders and compiles ours with Windows' `d3dcompiler_47.dll`; only a
hash of each game shader is stored, never its bytecode. Two off-line GPU
checks need a GPU and the game's `Shaders.big`: `make inst-gpu` (the instanced
shaders against the game's) and `make shadow-fx` (no shadow-map technique
reads the lighting or fog colour, which `ShadowBatchIgnoreLight` relies on).

## Faction unlocker in detail

It patches the game in memory at startup so that factions added via
`Playertemplate.ini` actually work. They show up in the skirmish lobby's
faction dropdown, picking one keeps it (instead of silently landing on another
faction), and AI/Random slots can pick them too. It supports vanilla BFME2
**1.06** and ROTWK **2.01**, including Age of the Ring's modified executable.
Nothing on disk is modified.

### What it patches

Each patch is found by a wildcard byte signature, never by a fixed address. It
is applied only if that signature matches **exactly once** in `.text` and the
bytes at the site are what the patch expects; otherwise it is skipped and
logged, and the game runs without it.

| INI key | default | effect | BFME2 1.06 site (RVA) | ROTWK 2.01 site (RVA) |
|---|---|---|---|---|
| `A` | on | Selecting a faction keeps it. Retail replaces any index outside the playable set with the first playable faction (`GameSlot::setPlayerTemplate`). | 0x00400E65 `75 06`→`EB 06` | 0x00402C95 `75 09`→`EB 09` |
| `C` | on | A `PlayerTemplate` whose `Side` repeats an existing playable Side still enters the playable set. Retail drops it silently. | 0x001FF1F9 `0F 84 …`→`90`×6 | 0x001FEA5F, same bytes |
| `D` | on | The lobby faction dropdown ignores the map start position's `AllowedFactions`. | 0x004426DA `74 4F`→`90 90` | 0x00444DFA `0F 84 BE 00 00 00`→`90`×6 |
| `E` | on | AI/Random faction picks ignore the map start position's `AllowedFactions`. | 0x00244632 `0F 84 8B…`→`E9 8C 00 00 00 90` | 0x0022D5C0, same bytes |
| `B` | off | Random is no longer forced to the first playable faction when the game's faction-restriction flag (GlobalData+0x9D4 & 3) is set. | 0x00400E79 `03`→`00` | 0x00402CAB `03`→`00` |
| `C2` | off | Ignore the Dwarves/Wild-only side restriction mode while parsing PlayerTemplates. | 0x001FF151 `7E 30`→`EB 30` | 0x001FE9B7, same bytes |
| `S1` | on* | Faction id of a Side (Create-A-Hero masks, rank icons, stats): an aliased new Side gets its donor's id. Hooks the lookup's entry. | 0x0033A3F4 prologue → `jmp` stub | 0x0033BDA3 |
| `S3` | on* | Per-Side rank skill-point thresholds: an aliased new Side uses its donor's. Hooks the function's entry. | 0x00200157 prologue → `jmp` stub | 0x001FFA44 |
| `S2` | on† | Which of the map's skirmish players an AI player is set up from (`findSkirmishSide`). Hooks the function's entry. | 0x002AC4D4 prologue → `jmp` stub | 0x002ACFA3 |
| `S4` | on† | Whether a computer player gets a skirmish AI at all: the Side match inline in `Player::initFromDict`. Hooks the compare call. | 0x002AFE61 `call` → stub | 0x002B096A |

| `NW1`, `NW2` | on | Write extended human/AI faction, colour and start bits. | 0x00447E91 / 0x00447F3F | 0x0044AB5E / 0x0044AC0C |
| `NR1`, `NR2` | on | Read and validate extended human/AI fields. | 0x0044877C / 0x00448A73 | 0x0044B445 / 0x0044B73C |
| `NS1`, `NS2` | on | Decode human/AI colour and faction independently. | 0x004487AA / 0x00448AA1 | 0x0044B473 / 0x0044B76A |
| `NW3`, `NW4` | on | Consume the already-written start byte. | 0x00447E9F / 0x00447F4A | 0x0044AB6C / 0x0044AC17 |
| `NR3`, `NR4` | on | Decode the human/AI start-position byte. | 0x0044881A / 0x00448B11 | 0x0044B4E3 / 0x0044B7DA |

All ten network patches are installed together. Disabling or failing any one
leaves the entire network group off, including on a page-protection failure.

\* S1 and S3 are installed only when `[Sides]` has at least one entry (see
*Strategy 2* below). Without it they are skipped and the game code stays as is.

† S2 and S4 are installed together or not at all (turning one off, or one
being refused, leaves the other out too), and they don't need `[Sides]`. See
*AI players on maps without their Side* below.

The research behind these is in [faction-limit-findings.md](faction-limit-findings.md)
and [side-string-lifecycle.md](side-string-lifecycle.md). Patch D differs from
those docs (they had it inverted); see [DISCREPANCIES.md](DISCREPANCIES.md).
The ROTWK sites were re-derived from the 2.01 code (DISCREPANCIES section 7).
On Age of the Ring, A, C, D and E change the mod's lobby behavior like any other game's.
If the mod relies on map faction restrictions, set `D=0`/`E=0` or try `DryRun=1` first.

### Reading the log

A healthy start on vanilla 1.06 ends with `summary: 16 applied, 0 refused, 4 off`
(S1 and S3 count as off while `[Sides]` is empty), or `18 applied, 0 refused, 2 off`
with side aliases. Every patch gets a block like:

```
[A] KeepSelectedFaction: applied
    GameSlot::setPlayerTemplate keeps a faction index that is not in the playable set
    signature matches: 1
    site RVA 0x00400E65 (baseline 0x00400E65: same)
    bytes 75 06 -> EB 06
```

`REFUSED (...)` names the reason: the signature was not found or matched more
than once, the original bytes differed, a branch target fell outside `.text`, or
`VirtualProtect` failed. A refused patch blocks its group partners; unrelated patches can still apply. The `game:`
line says whether the executable is the exact build the signatures were made
from (`the verified baseline build`) or another build of the same version. The
`sides: the game's own Sides (n): ...` line lists the Side table as read from
the running executable: 8 names on BFME2, 10 on ROTWK, 15 on Age of the Ring.

With `[Sides]` entries the log lists each alias (`sides: Rohan -> Men`, or
`sides: rejected ...` with the reason), and the hook blocks name their stub and
trampoline addresses. When the game exits, an `exit:` block is appended with how
often each hook ran and how often it replaced the Side, for example
`[S4] SkirmishAI: 12 calls, 3 with the Side replaced`. If a hook shows 0 hits
although you played the new faction, that code path didn't see the new Side.

While a game runs, S2 and S4 append a line when they step in, e.g.
`skirmish: Rohan has no skirmish player on this map, using Men's`, or
(once per Side) that a map has no skirmish player for a Side, its donor or
any game Side, so that AI stays idle.

### Adding a faction (strategy 1: reuse an existing Side)

The supported route for now is a new `PlayerTemplate` that **reuses an existing
`Side`**, e.g. `Side = Men`. It inherits everything keyed by the Side string:
map faction lists, the AI's skirmish base and scripts, Create-A-Hero faction
bits, rank icons and stats. Its own block controls the display name, spell
book, buildable heroes, starting units, and so on.

Test mod: in your mod's copy of `data\ini\playertemplate.ini`, duplicate the
whole vanilla `PlayerTemplate FactionMen … End` block, **append the copy at the
end of the file** (so existing faction indices don't shift), and change these
lines:

```ini
PlayerTemplate FactionMenTest              ; was: PlayerTemplate FactionMen
    Side            = Men                  ; keep: reuse the Men side
    PlayableSide    = Yes                  ; keep
    DisplayName     = INI:FactionMenTest   ; no string label: shows as MISSING:'INI:FactionMenTest'
    ; ... rest of the FactionMen block unchanged ...
End
```

Load the mod the way you normally load an INI mod. The `MISSING:` display name
is intentional: it makes the clone easy to tell apart in the dropdown.

### Adding a faction (strategy 2: a new Side)

A `PlayerTemplate` can have a genuinely new Side, such as `Side = Rohan`. The
data keyed by the Side string then belongs to it alone. The engine also has
**hardcoded** per-Side code that only knows the names in its Side table (7 in
BFME2, 9 in ROTWK, 14 in Age of the Ring): the faction
id table and the rank thresholds. In vanilla these treat an unknown Side as
"no faction", so Create-A-Hero and rank lookups fall through. (The maps'
skirmish players are a separate matter: S2 and S4 handle them with or without
a donor, see the next section.)

The DLL solves this with a **donor**. You name a vanilla Side whose hardcoded
behavior the new one borrows:

```ini
[Sides]
Rohan=Men
```

Wherever S1 and S3 sit, the engine is handed the donor's Side string instead of
`Rohan`; everywhere else it still sees `Rohan`. So a `Rohan` faction:

- gets Men's faction id. Create-A-Hero heroes allowed for Men are allowed for
  it; rank icons and stats count it as Men.
- uses Men's rank skill-point thresholds.
- as an AI on a map without a `SkirmishRohan` player, is matched to the map's
  Men player (S2/S4).
- keeps **its own** `PlayerTemplate` (name, spell book, heroes, starting units,
  buildings), and its own entries in any INI data keyed by its Side name.

What the **mod** must provide for the new Side (the DLL cannot invent data):

- the `PlayerTemplate` with `Side = Rohan` and `PlayableSide = Yes`, appended
  at the end of `playertemplate.ini`.
- `AIData` entries for the AI: a `SideInfo Rohan` block and a
  `SkirmishBuildList Rohan`, typically copied from the donor's. These are looked
  up by the Side name, not through the hooks.
- anything else your INIs key by Side (for example `Side = Rohan` on the
  faction's objects and upgrades), as for any faction.

Keep D and E on (the default). Map start positions restrict factions by the
vanilla list, so without D/E those maps can hide the new faction. The log warns
if aliases exist while D or E is off.

Rules for `[Sides]`: names are ASCII letters, digits and `_`, at most 31
characters, compared case-insensitively. There can be at most 32 entries. A new
Side may not be one of **the game's own Sides** or another alias. The DLL reads
the game's Sides at startup from the running executable's own Side table, plus
`Goblins` and the reserved `Neutral`, `Civilian` and `Observer`. So on BFME2
`Men`…`Wild` are taken, on ROTWK also `Angmar` and `Arnor`, and on Age of the
Ring also its own `Dunland`, `Guldur`, `Rhun`, `Mirkwood`, `Dale` and `Rohan`.
Rejected entries are logged and skipped. The donor should be one of the game's
Sides (the log warns otherwise), including `Goblins`. If the table can't be
read, the whole `[Sides]` section is ignored rather than risk shadowing a Side
the executable already knows.

On Age of the Ring, its 14 table Sides already work natively (the mod enlarged
the table itself). `[Sides]` only helps for Sides beyond those, e.g.
`Harad=Mordor`.

Limits. These follow the donor by design, and they are not verified in game yet:

- The War of the Ring (Living World) mode is untouched.
- Exact-name checks elsewhere in the engine (e.g. a second name table at
  VA 0x00DB9D18, used through a data reference) are not hooked. If something
  still treats the new faction as "none", the `exit:` hit counts help narrow
  down which path it took.

### AI players on maps without their Side

Every multiplayer map has one skirmish player per Side it was made for
(`SkirmishMen`, `SkirmishMordor`, …, set up in WorldBuilder). When a game
starts, the engine looks for the one whose faction has the AI player's Side.
If there is none, retail quietly turns the computer player into a
player without AI: it stands idle all game. That happens to every Side a map
predates, such as a mod's new factions on the original maps, or even Arnor on
some original ROTWK maps.

S2 and S4 change only that lookup. An AI player whose Side has no skirmish
player on the map is matched to:

1. its `[Sides]` donor's, if it has one and the map has it;
2. otherwise the first Side of the game's own table that the map has (the
   table order is logged at startup; `Goblins` comes last; `Neutral`,
   `Civilian` and `Observer` are never used).

If neither exists, nothing changes. A Side the map has is never redirected.

The match only decides which map player the AI's start comes from. On the
maps checked (`tools/mapinfo.py`, DISCREPANCIES section 8) those players are
bare: one empty team, no scripts, an empty build list. Everything else keeps
coming from the AI player's **own** Side, unchanged: its `PlayerTemplate`, and
the AI data the mod defines for that Side (`AIBase` blocks in ROTWK's
`skirmishaidata.ini`, `SideInfo`, `SkirmishBuildList`). A new Side still needs
those AI entries for the AI to build a base. Mods that already define AI data
for a new Side no longer need its skirmish player added to every map in
WorldBuilder.

The AI builds through its builder's command set, so every object its AI data
names must have a construct button there. A mod that used to give its new
factions an existing Side as a workaround can have such a mismatch hidden
until they get a Side of their own.

Not covered: a new Side's own faction id (S1's donor does that), and the two
other places that compare a player's Side (a script action's object walk and
one more call; DISCREPANCIES section 8).

### Manual test checklist

Run with the test mod above and the default INI:

- [ ] `dinput8.log` shows `the verified baseline build` and `summary: 16 applied, 0 refused, 4 off`.
- [ ] Skirmish lobby: the faction dropdown lists `MISSING:'INI:FactionMenTest'` next to Men.
- [ ] Selecting it keeps it in the slot panel (it doesn't jump to another faction).
- [ ] Start the game: you play the clone (check its spell book/heroes if you changed them), with no crash on map load.
- [ ] An AI slot set to Random resolves to a real faction; over a few games it sometimes picks the clone. The game runs normally.
- [ ] A map whose start positions restrict factions still offers every faction (patch D).
- [ ] `Enabled=0`: the log says `master switch off`, the clone is gone from the dropdown, the game runs normally.
- [ ] Without the DLL: the game is unchanged (sanity baseline).

Strategy 2: copy `FactionMen` to `FactionRohanTest` with `Side = Rohan`, copy
Men's `AIData` `SideInfo`/`SkirmishBuildList` to `Rohan`, and set `[Sides]
Rohan=Men`:

- [ ] The log shows `sides: Rohan -> Men` and `summary: 18 applied, 0 refused, 2 off`.
- [ ] The faction is in the dropdown and stays selected. You can play it on a vanilla map.
- [ ] An AI slot set to it builds a base and attacks. The log has
      `skirmish: Rohan has no skirmish player on this map, using Men's`, and
      after quitting the `exit:` block shows S2 and S4 hits > 0.
- [ ] Create-A-Hero: a Men-allowed hero can be picked for it (S1).
- [ ] The same mod with `S4=0`: the log says S2 is not installed (its partner
      is off), and the AI on the new faction does nothing (vanilla behavior).
      This confirms the hooks matter.

AI without a donor, on a mod with a new Side that has AI data (on Age of the
Ring: DLL in `AgeoftheRing\rotwk\`, empty `[Sides]`):

- [ ] On a map with no `Skirmish<Side>` player for it, an AI of the new Side
      builds its base and attacks. The log says it used another Side's
      skirmish player.
- [ ] An AI of a Side the map has (e.g. Men) plays as before, with no
      `skirmish:` line for it.

### Faction unlocker: testing

- `tools/verify_sites.py` (Python, capstone) checks each game's baseline image
  directly. It confirms the version resource, finds every signature exactly once at its
  baseline RVA, asserts the original and patched bytes, disassembles each site
  before and after, reads the Side table like the DLL does, checks that no
  signature matches the patched image (so a second run is a no-op), and
  regenerates `dll/src/patch_table.c` and `dll/tests/baseline_expect.h`
  (`--write`). For Age of the Ring's `game.dat` it also checks:
  - every site is at the clean 2.01 RVA with the clean bytes;
  - which signature bytes Age of the Ring changed (only wildcarded ones: the table size in S1);
  - that no branch in any executable section, including the mod's added
    `.danetta`/`.angmar`, lands inside a hooked prologue.

  This is the single source of the signature tables. Change signatures there,
  not in C.
- `dll/tests/test_engine.c` maps each game's executable into private memory (no
  code runs) and drives the real engine. Every scenario runs on both
  baselines, and Age of the Ring's executable gets its own pass (its 15 Sides,
  `Rohan=Men` refused, `Harad=Mordor` accepted). Scenarios: all patches on, a second run, defaults,
  everything off, a dry run, an ambiguous signature, a bad branch target, a site
  pre-patched by another tool, and the full startup sequence with and without
  an INI, checking the log text. With `[Sides]` it checks each hook site, its
  stub, its trampoline and the exit statistics. It also checks that only the
  patch-site bytes change and that WorldBuilder is refused.
- `dll/tests/test_hooks.c` **executes** the hooks. It copies the real S1–S4
  code bytes out of `game.dat`, redirects only the wildcarded addresses (the
  compare calls, the name table, a global) to fakes, lets the engine hook the
  copies, and runs them. The checks: `Rohan` resolves to Men's id 0, `Gondor=Mordor`
  to 4, `Arnor=Goblins` to 5, an unaliased Side still gets 7, and registers and
  flags survive the stub. For S2/S4, a fake `findSkirmishSide` body stands
  behind the real prologue, and a fake map lists its skirmish players. The
  checks: own Side, donor, first game Side and "nothing on the map" each pick
  the right player, both through S2's `this` and S4's argument; and S2 is
  refused when S4 is off or not found. It also unit-tests the `[Sides]` parser
  and the resolver.
- `dll/tests/test_proxy.c` loads the built DLL into a non-game process. It
  checks `Enabled=0`, the version refusal, all six exports by name and ordinal,
  and that `DirectInput8Create` really returns a working `IDirectInput8A` from
  the system DLL.

### How it works

- `dll/src/main.c`: `DllMain` runs the startup sequence and always returns
  `TRUE` (the game imports dinput8 statically and would not start otherwise).
  The six exports (`DirectInput8Create`, `DllCanUnloadNow`, `DllGetClassObject`,
  `DllRegisterServer`, `DllUnregisterServer`, `GetdfDIJoystick`, ordinals 1–6
  as in the system DLL) forward to `%SystemRoot%\System32\dinput8.dll`. That DLL
  is loaded on the first call, so there's no `LoadLibrary` under the loader lock.
- `dll/src/unlocker.c`: reads the INI, opens the log, applies the version gate,
  runs the engine and logs the results.
- `dll/src/engine.c`: finds `.text` from the in-memory PE headers and locates
  **all** enabled sites before writing any. Each write re-verifies the original
  bytes, then does `VirtualProtect` → write → restore → `FlushInstructionCache`.
  Conditional `jcc rel32` → `jmp rel32` rewrites recompute the displacement so
  the target is unchanged.
- `dll/src/hooks.c`: S1–S4. An entry hook moves the first whole instructions of
  the function (5 to 10 bytes, all position independent, and nothing branches
  into them; `verify_sites.py` checks both) into a trampoline. It then replaces
  them with `jmp stub` plus `int3` padding. A call hook redirects one `call` to
  the stub, which continues at the original callee. Each stub saves all
  registers and flags, swaps the `const AsciiString &` Side argument on the
  stack for the donor's, restores everything and continues. The continuation
  is set before the site is written. S2's stub swaps the saved `ecx` (the
  `Player *this`) for a stand-in whose Side is the resolved one.
- `dll/src/skirmish.c`: the S2/S4 resolution rule. It asks "does this map have
  a skirmish player for Side X" by calling `findSkirmishSide`'s trampoline
  (the unhooked function) with a stand-in `this`.
- `dll/src/sides.c`: the `[Sides]` table. Each donor is a static string in the
  game's `AsciiString` layout with a reference count that never reaches zero,
  so the game can copy it and release the copy, but never frees it.
- No C runtime. The DLL is built `-ffreestanding -nostdlib` and imports only
  kernel32, so nothing but this code runs around `DllMain`. It creates no
  threads and patches static code bytes only.

#### Deviations from `HANDOFF.md`

- **One DLL instead of proxy + payload.** Loading a separate payload from the
  proxy's `DllMain` would mean `LoadLibrary` under the loader lock, so the patch
  code lives inside the proxy (HANDOFF Phase 1 allows "doing the work inline").
- **Patch D** was re-derived. See [DISCREPANCIES.md](DISCREPANCIES.md).

- **Strategy 2 uses donor aliases, not a relocated name table (patch F).** The
  7-name table has about 20 direct code references and parallel arrays indexed
  by faction id (Create-A-Hero masks, rank data). Growing it would mean
  patching all of them and inventing data for the new id. Aliasing the three
  consumers gives a new Side a consistent, already-valid id. See
  [DISCREPANCIES.md](DISCREPANCIES.md).

### Not done yet

A new Side with its *own* faction id (instead of the donor's), WorldBuilder
support, and the Living World (War of the Ring) faction paths. Nothing (BFME2,
ROTWK or Age of the Ring) has been verified in game yet; the S1–S4 execution
test uses the 1.06 code only, though the 2.01 functions are byte-identical
apart from addresses and the table size.

## Performance plugin in detail

bfme2perf was written for big battles in large mods such as Age of the Ring.
There the main thread, not the GPU, sets the pace: the shadow-map pass, the
main view, the water reflection, logic and the UI share each 33 ms frame.
The plugin removes drawing work that changes nothing on screen, and lets the
picture run smoother or skip ahead without touching what the game simulates.

- **More than 30 FPS (`MaxFPS` 60, 90 or 120).** Extra frames are drawn
  between the game's own frames, in the time the frame limiter would
  otherwise wait, and only when one fits before the next game frame. Logic,
  input and the network stay on the game's 30. `FrameSmoothing` chooses how
  an extra frame moves units: extrapolated ahead, or drawn between the last
  two game frames. The camera is never moved; every frame shows it where the
  game has it. What the game advances once per
  drawn frame is held in extra frames, so it keeps its speed: particles, the
  view update, the scene's update list (W3D emitters, dynamic light fades),
  and the command-button clocks for production, upgrades and recharges,
  which are drawn in every frame. Extra frames help most in light and medium
  scenes; in the heaviest battles game frames already take more than 33 ms
  and leave no idle time.
- **Instancing of repeated units (`Instancing`).** In each group of identical
  unit meshes the game draws the first, and the rest go out in a few
  instanced draws with the plugin's own vertex shaders, including the Low
  shader quality variants and, with `ShadowInstancing`, the shadow map.
  `InstancingVerify` draws a few groups both ways off screen every 5 s and
  switches off, for the session, a shader kind that differs twice.
- **Shadow-pass cuts.** Three cuts in the shadow-map pass, each built to give
  the same shadow map at its shipped setting (`NoStencilFlushes=2` and
  `ShadowSkipParticles=2` go further and are not exact):
  - `ShadowBatchIgnoreLight`: units of the same model and material form one
    group even when their lighting differs, since the shadow map's shaders
    read neither lighting nor fog colour. The game's and the mod's effect
    files are checked while the game loads; it stays off if any other could
    be loaded.
  - `NoStencilFlushes`: big monsters (trolls, mûmakil, ents, eagles…) are no
    longer drawn alone between two flushes that only stencil shadow volumes
    need.
  - `ShadowSkipParticles`: the particle walk, which draws nothing into the
    shadow map, is skipped.

  `ShadowPassVerify` draws the map with and without a cut every 10 s,
  compares, and switches off a cut that differs twice.
- **Catch-up (`[CatchUp]`).** When frames take longer than 33 ms, the
  battle runs slow. Catch-up skips the render of a frame when the game has
  fallen behind its schedule by more than a render's cost, so the game keeps
  full speed and you see a few frames fewer. A small notice shows on screen
  while it skips. See *Multiplayer safety* for when it never skips.
- **Shadow and reflection intervals.** `ShadowInterval` and
  `ReflectionInterval` redraw the shadow map or the water reflection only
  every Nth frame and reuse it in between. These save the most, but moving
  shadows can trail by up to N−1 frames, so they are off (1) by default.
- **Timing.** `bfme2perf_timing.csv` records where each second's frame time
  went (logic, shadow pass, reflection, main view, UI, present, and more), so
  settings can be compared on the same replay.

The plugin is built the same way as the unlocker: freestanding C, kernel32
only, every hook found by signature and logged in `bfme2perf.log` as applied
or not, with its RVA. The hook stubs touch only general registers, and
floating point lives in one unit (`fpsmath.c`).

Tests (`make test` in `perf/`) cover the CSV rows and INI parsing, every site
against Age of the Ring's `game.dat` and `game820.dat` when present, and the
hook stubs, extra frames, instancing, the shadow-pass cuts and catch-up run
against fakes, plus loading the built `.asi` into a non-game process. The
design, the measurements and the proofs for each feature are in
[docs/specs/2026-10-13-perf-dll-design.md](docs/specs/2026-10-13-perf-dll-design.md)
and [docs/specs/2026-10-13-perf-instancing-design.md](docs/specs/2026-10-13-perf-instancing-design.md),
with the step-by-step plans in [docs/plans/](docs/plans/).

## How this differs from the projects that inspired it

SAGE-Unlocked and bfme2-accelerator showed that BFME2 can be changed at run
time from a DLL, with patches found by signature rather than fixed addresses.
We studied their READMEs and general approach. Our purposes and methods
differ:

- **Purpose.** The faction unlocker exists for modding: making factions
  added in `playertemplate.ini` really work in the lobby, for the AI, in
  Create-A-Hero and for ranks. bfme2perf aims at big battles for a player who
  may be the only one with the plugin in an online game.
- **Logic is never retimed.** Because BFME2 counts logic in client frames,
  frame-rate work that changes when or how often frames run relative to the
  logic steps changes what the simulation sees, and online players can drift
  out of step. Our design notes record that bfme2-accelerator's logic spread
  and render thread moved where frames land relative to logic ticks, which
  online showed as teleporting units and animations out of step. (Its
  current README says it holds the logic at 30 steps while drawing 60.)
  bfme2perf keeps the game's own frames, and with them logic, input, the
  network and the sync check, exactly as they are. It only adds display-side
  frames in idle time, or leaves out the drawing of a frame.
- **Effects stay at game speed.** Effects that advance once per drawn frame
  would run faster with more frames; at 120 FPS, light pulses once faded up
  to four times as fast before the plugin held them. Extra frames hold every
  such update listed above.
- **Loading.** The unlocker is a `dinput8.dll` proxy, as SAGE-Unlocked is.
  bfme2perf loads as a Miles audio plugin from `mss\`, with no injector, no
  replaced file and no forwarded export.
- **Independently written.** Everything here is written from scratch in C
  from our own research. No code is taken from SAGE-Unlocked, bfme2-accelerator
  or Open-BFME-2.

## Credits and references

Created by **Annatar_BR**.

- **[Open-BFME-2](https://github.com/Open-BFME/Open-BFME-2)** (GPL-3.0) is
  the reference for this project. It rebuilds BFME2 1.06's `game.dat` from
  C++ source byte for byte, and its decompilation is where the research
  starts: the function names, data layouts and behaviour this project's
  patches and hooks rely on, re-found by signature in each executable. The
  unlocker's tests read the vanilla 1.06 baseline image it keeps. Because
  that project reproduces retail exactly and accepts no behaviour changes,
  changes like these can only live in a runtime DLL. This repository is a
  separate project with its own history; it contains no Open-BFME-2 code.
- **SAGE-Unlocked** ([TheeHorse/SAGE-Unlocked](https://github.com/TheeHorse/SAGE-Unlocked),
  GPL-3.0) inspired the `dinput8.dll` proxy copied into the game folder,
  patches found by behaviour or signature instead of fixed addresses, and
  INI configuration.
- **bfme2-accelerator** ([OhadBaehr/bfme2-accelerator](https://github.com/OhadBaehr/bfme2-accelerator),
  MIT) inspired the wildcarded byte-signature hooks, features that stay off
  when a signature is missing, per-feature toggles, and its frame-rate work,
  which bfme2perf's extra frames and catch-up approach differently (above).

Both components were first developed and tried with the Realms in Exile mod,
running on Age of the Ring. *The Lord of the Rings: The Battle for
Middle-earth II* and *The Rise of the Witch-king* belong to Electronic Arts.

## Layout

| path | what |
|---|---|
| `dll/src/` | unlocker: proxy, engine, hooks, side aliases, version gate, config, log; `patch_table.c` is generated |
| `dll/tests/` | unlocker: engine, hook and proxy tests; `baseline_expect.h` is generated |
| `dll/dinput8.ini` | default unlocker config shipped with the DLL |
| `perf/src/` | plugin: loader, site finders, hooks and stubs, timing, extra frames, instancing, shadow-pass cuts, catch-up, notice; `inst_shaders.h` is generated |
| `perf/tests/` | plugin tests, plus the off-line GPU checks (`inst_gpu/`, `shadow_fx/`) |
| `perf/shaders/inst.hlsl` | the plugin's instanced vertex shaders |
| `perf/bfme2perf.ini` | default plugin config shipped with the `.asi` |
| `tools/verify_sites.py` | unlocker verification harness and table generator |
| `tools/inst_shaders.py` | generates `perf/src/inst_shaders.h` (dev time, not shipped) |
| `tools/` (others) | research helpers: disassembly, cross-references, porting 1.06 functions to other images, map/castle readers and a RefPack writer, `.big` and `asset.dat` readers, a minimap audit of mod INIs; see [tools/README.md](tools/README.md) |
| [docs/](docs/) | design specs and plans (native Sides, the performance plugin, instancing) |
| [AGENTS.md](AGENTS.md), [docs/STATUS.md](docs/STATUS.md), [docs/BACKLOG.md](docs/BACKLOG.md) | rules, branches, build and deploy steps; what is installed and pending; what still needs the user (in-game tests, WorldBuilder work) |
| [HANDOFF.md](HANDOFF.md) | the brief the unlocker was built from (completed) |
| [faction-limit-findings.md](faction-limit-findings.md), [side-string-lifecycle.md](side-string-lifecycle.md) | research |
| [command-button-limits.md](command-button-limits.md) | research: the 33-button CommandSet and the porter's 12-button side bar (not acted on) |
| [DISCREPANCIES.md](DISCREPANCIES.md) | where the research and the image disagreed |

## Legal

MIT ([LICENSE](LICENSE)). Written from scratch from our own research and the
Open-BFME-2 decompilation. No code from bfme2-accelerator, SAGE-Unlocked or
Open-BFME-2. Never redistribute `game.dat` or any EA asset; ship the DLLs and
INIs only.
