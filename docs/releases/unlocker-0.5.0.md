# Faction unlocker 0.5.0 — Realms LAN faction fix

The LAN lobby previously wrapped faction indices every 16 entries, so
Lindon appeared as Observer and Mordor appeared as Arthedain on another PC.
This build fixes that encoding for human and AI slots and preserves colour
and start position. It does not enlarge the fixed-size network packet.

## Realms Launcher payload

Copy the new `dinput8.dll` into the actual ROTWK engine directory, beside
`game.dat`. On this development machine that destination is
`C:\Jogos\AgeoftheRing\rotwk\dinput8.dll`. Resolve the engine directory on
each user's machine; do not deploy it only into `realms_nightly`.

- Update **every player's** DLL. Version 0.4.x and protocol 1 are incompatible.
- Close the game before the launcher replaces the DLL. Back up the old DLL.
- Preserve the existing `dinput8.ini`, especially its `[Sides]` aliases.
  The new hooks default on when their keys are absent. Do not replace it with
  the generic INI in `dll/dist` (which has no Realms aliases).
- Keep everyone's Realms game data and unlocker settings identical.
- No change to `bfme2perf.asi`, its INI, or `game.dat` is needed.
- Distribute the MIT license with the DLL. No EA assets are in the payload.

The prepared launcher archive contains `payload/dinput8.dll`, `LICENSE`,
these notes and a SHA-256 manifest. `payload/` is a staging folder, not the
literal destination directory. Check the manifest before publishing.

## Startup verification

`dinput8.log` must identify version **0.5.0**, show every `NW1`–`NW4`,
`NR1`–`NR4` and `NS1`–`NS2` patch as applied, and include:

```
network: LAN protocol 1 enabled; every player needs unlocker 0.5.0 or compatible
```

With the existing Realms aliases and default patch switches the summary is
`18 applied, 0 refused, 2 off`. Without aliases it is
`16 applied, 0 refused, 4 off`. If any network patch is disabled or refused,
all ten remain off: do not treat that client as updated for multiplayer.
Restart the game after replacing the DLL; an already-running process retains
its old code.

## Validation and release status

Automated checks exercise the native human/AI caller instruction sequences
from BFME2 1.06, ROTWK 2.01 and AotR. They exhaust all supported faction,
colour and start-position combinations and all 65,536 encoded field values.
They also cover the 17 reported mappings, bounds, invalid input, eight-slot
field capacity and all-or-nothing installation failures.

**A live multiplayer smoke test is still required before public rollout.**
Use two updated PCs with identical Realms data; check both host and guest
selections for Lindon, the Shire, Mordor and Rhun, then AI and Random. Confirm
colours/positions, start a match and play beyond the previous failure point.
Also check an eight-slot lobby/match. This patch fixes the demonstrated
encoding bug; it does not establish that every possible desync is resolved.

This change targets the LAN binary lobby protocol (including services that
carry LAN traffic). It does not modify the separate GameSpy lobby protocol.
Old clients cannot parse the updated host's human slot and new clients reject
legacy slot data; the game may simply fail to list/join that lobby rather
than displaying a version-mismatch dialog.

## Codec and limits

The two original bytes now hold, little-endian:

```
bit 15       : 1 (extended format)
bits 11–14   : start position + 1 (0..8)
bits 4–10    : template index + 2 (0..127)
bits 0–3     : colour index + 1 (0..15)
```

Template indices **0–125**, Observer (-2) and Random (-1) round-trip.
Non-playable templates also consume indices. Colours -1–14 and starts -1–7
are supported. Out-of-range values produce rejected records rather than
wrapping. The high byte is always 0x80–0xC7, an invalid signed start position
for the legacy human-slot reader. Original hero packing is untouched.

Ten uniquely verified call sites form one installation group; all sites and
page protections are prepared before any member is written. The table is
generated from `tools/verify_sites.py`. No executable addresses are hardcoded
into the runtime install path.

## Rollback

Restore the previous DLL on **all PCs together**, keeping their existing
INIs. This restores the old protocol and its faction limitation; high-index
factions are not fixed by rolling back. No game executable was modified.
