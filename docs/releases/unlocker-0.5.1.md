# sage-boost 0.5.1 - BFME Online compatibility

The faction unlocker can now load as `mss/bfme2unlocker.asi`, leaving
Tacitus's `dinput8.dll` available for BFME Online. This package includes the
previously tested performance plugin 0.9.11-beta.18 and Realms settings.

## Install

Realms Launcher users: update to Launcher 2.0.8 before updating sage-boost.
For a manual install, copy the archive's `mss` folder into the ROTWK folder.
Preserve existing INI settings. If migrating from the old unlocker, copy your
`dinput8.ini` to `mss/bfme2unlocker.ini`, and remove only sage-boost's old
`dinput8.dll`. Keep Tacitus's DLL. Enable BFME Online in AOTR's launcher if
Tacitus is missing, then reinstall the ASIs if AOTR removed them.

Use one unlocker format at a time. Diagnostics are in
`mss/bfme2unlocker.log` and `mss/bfme2perf.log`.

The user confirmed Realms can log into BFME Online; selecting the physical
network adapter instead of Radmin resolved the connection timeout. Full
multiplayer-match validation remains pending. Use matching sage-boost versions
on every PC in a match. No new performance algorithm is introduced here.
