# Multiplayer Factions Implementation Plan

**Goal:** Preserve Realms' faction selections across multiplayer lobby updates.

**Architecture:** Replace the human/AI colour/faction and start-position
writer/reader calls plus the faction splitters (ten call sites). A 16-bit word
stores `marker:1 start+1:4 faction+2:7 colour+1:4`, little-endian. Both original
helpers still advance one byte. The high byte is an invalid legacy human
start position, and the new reader rejects all valid legacy encodings.
This preserves the fixed 390-byte lobby packet capacity, including eight
human players. Factions -2..125, colours -1..14 and starts -1..7 are supported.

**Tech Stack:** freestanding i686 C, small assembly adapter, signature scanning,
Python/capstone verification, native C tests against local executable images.

## Constraints

- Never modify or distribute game executables or reference sources.
- Locate all ten sites uniquely on BFME2, ROTWK and AotR; install as one group.
- Keep hero packing untouched. Keep the performance plugin untouched.
- Existing INIs retain their settings; new hooks default on without INI edits.
- Do not launch the game, install into the game directory, push or tag.

## Implementation and validation

- [x] Add ten call-site definitions in `tools/verify_sites.py`, regenerate
  `dll/src/patch_table.c` and `dll/tests/baseline_expect.h`.
- [x] Add `dll/src/network.c` / `.h`: checked two-byte writer and reader,
  bit-field decoder, and an assembly writer adapter taking GameSlot
  from ESI (colour at +0x0C, start at +0x10, template at +0x18).
- [x] Route the ten calls through `dll/src/hooks.c`; make grouped writes in
  `dll/src/engine.c` fail together, including page-protection failures.
- [x] Add `dll/tests/test_network.c`: all faction IDs -2..125, colours -1..14 and starts -1..7,
  all 17 reported regressions, sentinels, malformed/truncated input, overflow,
  guard bytes, actual call-site execution and legacy marker rejection.
- [x] Extend engine tests for each missing/disabled network group member.
- [x] Bump unlocker to 0.5.0. Update README, INI comments, status and release
  instructions with the all-players requirement and untested in-game status.
- [x] Run `mingw32-make test` and `mingw32-make dist` in `dll/`; create a
  DLL-only launcher payload plus release notes, license and SHA-256 manifest.
- [x] Review the final diff, commit explicit source paths locally, and report
  artifacts and the required two-PC lobby/match smoke test.
