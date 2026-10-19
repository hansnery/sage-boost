# Simulation timing experiment

## Accepted scope

On 2026-10-18 Hans accepted exploring game-logic changes in a separate
sage-boost beta after the visual-only work failed to demonstrate a worthwhile
improvement in the troop-heavy scene. He then clarified that an optional
launcher beta is a future distribution possibility, not a request to change
the launcher or publish now.

This changes the previous exact logic/CRC/frame-counter/frame-limit
invariance requirement for the experimental branch. It does not lower the
performance target: at least 59 presentations/s over a settled 60-second
capture of the troop-heavy scenario, with pacing, fresh full-rate shadows
and no graphics-quality reduction. Normal gameplay speed and balance,
camera/input behavior and multiplayer synchronization remain requirements.
All participants may need the identical experimental build and settings;
mixed stable/beta multiplayer, old saves and replays are unqualified.

Keep stable releases, the faction unlocker and installed preferences intact.
Never launch the game or WorldBuilder. Use offline evidence before requesting
a significant gameplay test. Ordinary authorized development commits/pushes
remain allowed. No launcher integration or publication is part of this work.
The stored goal text's older unchanged-logic clause is superseded by Hans's
explicit decision; its measured performance requirement remains unmet.

## Routes considered

1. **Native rate configuration, first investigation.** The reference exposes
   a function that configures the client and logic rates plus eight related
   float conversions. A 10 Hz logic / 60 Hz client pair fits the existing
   six-phase scheduler. Verify the actual installed executable bodies first.
   This may increase simulation work; it is not a performance claim.
2. **A new client/logic schedule.** More independent presentation scheduling
   could keep the current logic rate, but requires qualifying the fixed six
   phases, client interpolation, network admission and object lifetimes.
   Keep this as an alternative if rate conversion is incomplete or costly.
3. **Bulk data retiming.** Rewriting mod values is the converter's approach.
   It requires broad unit-aware coverage, mod-version maintenance and matching
   peers. Do not apply a blind global multiplier or rebuild Realms archives.

## First executable experiment

Add a standalone CPU correctness harness for the native rate setter. Locate
its entire body in both installed images by a wildcarded reference-body
match, validate control flow and all data operands, then copy only that body
into private test-process memory. Redirect its data to owned test storage.
Do not load the executable entry point, its imports, assets or any game
subsystems. Temporary manifests contain only locations, hashes and operand
metadata; no game bytes persist or ship.

Check the normal 5/30 pair, proposed 10/60 pair, unsupported 5/60 pair,
clamping, nonintegral ratios and return to baseline. Verify every output,
write boundaries and callee-saved registers. Deliberately wrong duration,
client-rate and client-step outputs must fail the oracle. Changed native
instructions, constants and output identities must fail preflight.

Preserve results and hashes in a report. This is a correctness check, not a
benchmark: there is no rendered frame, scene, multiplayer peer or FPS result.

## Integration questions the test does not settle

- Native duration parsers round fractional ticks upward. A 250 ms field at
  5 Hz occupies two ticks (400 ms); at 10 Hz it occupies three (300 ms).
  These are arithmetic examples, not a measured Realms weapon. Decide how
  to retain baseline quantization for gameplay fields before patching them.
- Static initializers derive additional half-rate timers and the W3D frame
  length. Updating the shared conversions after startup cannot update those
  already initialized values automatically.
- The setter does not change the live engine's FPS limit. Qualify the cap,
  startup/reset assignments and coexistence with the current extra-frame and
  catch-up hooks separately; avoid two schedulers adding frames independently.
- The ASI is loaded by audio. The reference initializes GlobalData and some
  INIs before audio, and weapon/object data afterward. Confirm the installed
  image's actual load order and classify already-parsed fields.
- Source shows native network pacing based on the logic rate. Verify both
  installed network bodies, admission/phase behavior and matching-peer
  determinism; do not infer sync from a successful rate-setter call.
- Identify unscaled per-tick increments, animation/particle timing, camera
  and input semantics, construction, cooldowns, economy and script timers.
- Measure real complete-frame cost; increasing the simulation rate can
  consume more CPU. The existing render optimization work remains available.

No runtime hook or installation follows from this first leaf test alone.

## First result and provenance

Both installed images pass the dedicated `make test-native-rates` check.
The full setter matches at research RVA `0x00244F11`; 10/60 is accepted and
5/60 becomes 5/30. Thirteen cases and 512 additional calls per image verify
ten outputs, boundaries and ABI. Twelve total negative controls are rejected.
The [report](../benchmarks/2026-10-18-native-rates.json) retains input/code
hashes, metadata, raw outputs and model limits.

Crucially, the computed 5/30 pair changes two startup float words by one ULP:
logic frames/ms `0x3ba3d70a` becomes `0x3ba3d70b`; client frames/ms
`0x3cf5c28f` becomes `0x3cf5c290`. The repeated-call test restores the
**computed** baseline, not the file's original values. Arithmetic with the
measured factors and the source's ceiling operation predicts an extra tick
for exact-boundary durations. Execute the native integer parser next to
qualify that effect. Never treat a 5/30 setter call as exact rollback.

Read-only reference checkout at inspection:
`ff1544d9fef63a5460d1fcd012cea309c13f7dff`. Relevant paths under
`Code/GameEngine/Source/Common/`:

- `FrameRateConfiguration.cpp`: setter's rate bounds and ten outputs.
- `GameEngineFrameTiming.cpp`: fixed six-phase cycle.
- `GameEngineNetworkHelpers.cpp`: admission and distributed logic phases.
- `NetworkInterfaceFrameAdvance.cpp` and `NetworkInterfaceFramePacing.cpp`:
  logic-rate-based pacing; not yet executed against installed images here.
- `GameEngineInit.cpp`: GlobalData/selected INIs before audio, then object
  stores; source-only ordering, not an installed loader trace.
- `Rva007AB7F0HalfRateInits.cpp` and `Rva007ABC6FFrameRateInits.cpp`:
  separate startup-derived timer fields and W3D frame length.
- `System/INI_parseDurationUnsignedInt.cpp`: native ceiling of the duration
  product; its binary disassembly also uses that factor and ceiling call.
