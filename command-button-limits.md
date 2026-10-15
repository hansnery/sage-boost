# Command button limits: buildings and the porter

Research date: 2026-10-15. Read only; nothing in this repo or in the game
was changed. The user decided not to act on it for now.

Sources: the Open-BFME-2 decompilation at `C:\repositories\Open-BFME-2`
(BFME2 1.06; paths below are relative to its `Code\` folder), the BFME2 1.06
baseline image, Age of the Ring's `rotwk\game.dat` (ROTWK 2.01 with the
mod's edits), ROTWK's 2006 `Worldbuilder.exe`, the stock
`rotwk\apt\InGameSideCommandBar.big` and the Realms in Exile INIs. RVAs
are of those images.

## TL;DR

- **A CommandSet holds 33 buttons in ROTWK (32 in BFME2).** It is a fixed
  array in the CommandSet object plus a fixed table of slot names `"1"` to
  `"33"` in the INI parser. A `34 = ...` line is not a slot at all. EA
  raised it from 32 in ROTWK; Age of the Ring did not (WorldBuilder from
  2006 has the same 33-slot table).
- **The porter's build buttons go to the side command bar**, the strip
  shown for builders, not the portrait. It shows the `Radial = Yes` buttons
  of the command set, in slot order. The code caps it at **15**; the stock
  Apt movie has only **12** button frames, so 12 is what you get today.
- Going from 12 to 15 would be a data-only change (an edited
  `InGameSideCommandBar.apt` with three more frames). Past 15, or past 33
  for a command set, needs code changes in many places.

## Buildings: the CommandSet size

| | BFME2 1.06 | ROTWK 2.01 (AotR `game.dat`) |
|---|---|---|
| CommandSet INI field table | RVA `0x838DD0`, `"1"`..`"32"` | RVA `0x84F3D8`, `"1"`..`"33"` |
| slot parser (userData = slot index) | `0x40A092` | `0x40C9E1` |
| button array in the CommandSet object | `m_command[32]` at +0x14 | `m_command[33]` at +0x14 |
| `InitialVisible` field | +0x94 | +0x98 |
| ControlBar command windows | 32, ControlBar +0xDC..+0x15C | 33, ControlBar +0xDC..+0x160 |

In the decompilation (BFME2, so 32 everywhere):

- `GameEngine/Source/GameClient/GUI/ControlBar/ControlBarReloadNotice.cpp`:
  `ControlBar::parseCommandSetDefinition` and
  `CommandSet::m_commandSetFieldParseTable`, one row per slot name.
- `GameEngine/Source/GameClient/GUI/ControlBar/CommandSetGetCommandButton.cpp`:
  the CommandSet layout (`m_command[32]` at +0x14) and `getCommandButton`,
  which first asks GameLogic for a per-slot override, then reads the array.
- `GameEngine/Source/GameClient/GUI/ControlBar/ControlBarCommand.cpp`: the
  ControlBar's `GameWindow *windows[32]` at +0xDC and the 32-iteration
  loops over them.
- The slot count is repeated as a local constant (`MAX_COMMANDS_PER_SET`,
  `32` or `0x20`) in `BuildAssistant.cpp`, `AIPlayer.cpp`,
  `AIGroupGetCommandButtonSourceObject.cpp`, `AIUpdate.cpp`,
  `AutoAbilityBehaviorDtor.cpp`, `CommandButtonHuntUpdateXfer.cpp`,
  `ScriptActions_doRemoveCommandBarButton.cpp`, `ControlBarMultiSelect.cpp`,
  `ControlBarSwitchContext.cpp`, `ControlBarUpdateConstruction.cpp`,
  `ControlBarUpdateVirtual.cpp` and others (`grep -rn MAX_COMMANDS_PER_SET`).

Raising the limit would mean a new field table, a larger CommandSet
allocation with `InitialVisible` moved, more ControlBar windows (and the
`.wnd` buttons behind them) and every one of those loops, several of them
in logic code (AI, script actions, save/load), which the multiplayer
safety rule rules out for the plugin.

## The porter: side command bar

`AptInGameSideCommandBar` (decompilation:
`GameEngine/Source/GameClient/GUI/InGame/Tactical/AptInGameSideCommandBar.cpp`
and `AptInGameSideCommandBarUpdate.cpp`). `Impl::UpdateButtonVisiblity` is
not decompiled; it was read from the images.

| | BFME2 1.06 | ROTWK 2.01 (AotR `game.dat`) |
|---|---|---|
| `Impl::Update` (shows/hides the bar) | `0x5287EA` | `0x52F27D` |
| KindOf test in `Update` | `0x528819` | `0x52F2AC` |
| `Impl::UpdateButtonVisiblity` | `0x5285EF` | `0x52F082` |
| 15-button cap (`cmp ebx, 0xF`) | `0x528610` | `0x52F0A3` |

How it decides what to show:

1. **Who gets the bar.** `Update` shows it only for a selected object whose
   template has KindOf bit 14, `DOZER` (template +0x109, mask 0x40), owned
   by the local player. `PORTER` (bit 143) is never tested. Every RiE
   porter has `KindOf = ... DOZER PORTER`, so porters qualify as builders.
2. **Which buttons.** `UpdateButtonVisiblity` walks the ControlBar command
   windows in slot order (all 33 in ROTWK) and takes each one whose
   CommandButton has `Radial = Yes` (CommandButton +0x101; the field table
   names it). `Command_PorterConstruct*` buttons have it;
   `Command_EvilStop` and `Command_PorterExtinguishFire` don't, so they
   never appear in this bar.
3. **How many.** The bar has a fixed `SideBarSlot m_buttons[15]` (12-byte
   slots at +0x24, count at +0xD8) and stops at 15. It also stops at the
   first slot whose button frame the Apt movie never registered
   (`OnAptInGameSideCommandBarButtonFrameLoaded`, which passes `index`).
4. **What the movie provides.** The stock
   `rotwk\apt\InGameSideCommandBar.big` holds one `InGameSideCommandBar.apt`
   with `Button0`..`Button11` and `SideCommandBar01.tga`..`12.tga`: 12
   frames. Neither the AotR nor the RiE folders override it.

So a porter shows its first 12 `Radial = Yes` buttons. Example: RiE's
`MordorPorterCommandSet_SauronEra` has 12 slots: 10 are radial (nine
`Command_PorterConstruct*` and `Command_MordorPorterSauronEraChoiceButton`)
and fill 10 of the 12 frames; Stop and Extinguish Fire are not.

### Latent off-by-one

`OnButtonFrameLoaded` and `OnButtonFrameUnloaded` accept `index` 0 to 15
inclusive (`index < 0 || index > 15`), one past the 15-slot array. Index 15
would write its frame pointer over the count at +0xD8. The stock movie only
sends 0 to 11, so it never happens; an edited movie must not send 15.

## Reproducing

```sh
python tools/disasm.py C:/Jogos/AgeoftheRing/rotwk/game.dat 0x52F082 --len 0x130
python tools/disasm.py C:/Jogos/AgeoftheRing/rotwk/game.dat 0x52F2A9 --len 16
python tools/disasm.py C:/Jogos/AgeoftheRing/rotwk/game.dat InitialVisible --string
```

The CommandSet and CommandButton field tables are 16-byte rows
(`token, parse, userData, offset`) ending in a zero row; find them from the
VA of a token string (`InitialVisible`, `Radial`). The KindOf name table is
a pointer array starting at `"OBSTACLE"`, `"SELECTABLE"`.
