# External render-thread evaluation

The user has redirected the 60 FPS investigation toward Accelerator and
SAGE Unlocked. Evaluate the released Accelerator as an external program,
using its documented standalone harness, before implementing another local
rendering strategy. This is offline research under the existing development
authorization, not approval to change game timing or install the defaults.

## Alternatives and choice

1. Run Accelerator's own off/on harness against the verified 2.0 release.
   This tests the actual candidate implementation with equal-input image
   hashes, without adopting its code or launching the game. Start here.
2. Build an independent command-queue prototype immediately. It would need
   effect/device ordering, copied payloads, resource ownership and barriers
   before its timing was meaningful. Defer until the external test reveals
   whether this architecture merits that work.
3. Install an FPS unlocker first. It cannot by itself establish sufficient
   CPU headroom or Realms multiplayer behavior. Defer live changes until
   the offline evidence supports a substantive trial.

## Isolation and evidence

Reuse `C:/asd-repositories/bfme2-accelerator`, the existing catalog checkout.
It is a separately maintained fork with local build-path changes. Do not
edit, reset, commit or push it. The fork's harness predates the release;
extract the matching upstream harness from the existing Git object database
into a new temporary directory and compile it using the installed x86 MSVC
compiler. Preserve commit, blob and source hashes. An unsupported interface
or output mismatch is a failed qualification, never an excuse to guess an
ABI or repair external source inside sage-boost.

Use only the hash-verified official release DLL for candidate runs. Copy
the installed local DXVK and D3DX27 dependencies into that temporary folder;
preserve hashes. Keep foreign source, binaries, shaders and rendered images
outside sage-boost. No game or WorldBuilder launch, injection into a live
game, installation, INI change, frame interpolation, camera change or
multiplayer experiment is part of this screen.

Run documented `off` and `on` modes sequentially, with the same number of
frames and options. Check game/launcher absence before each subprocess and
use a timeout. Compare every reported frame hash, not just a final checksum.
Preserve actual commands, return codes, output hashes and timings. Require
the log to confirm the render-thread path was active; an inactive no-op is
not a positive result. Synthetic timings do not establish saved-game FPS.

If the harness provides only correctness/readback timings, classify those
as such. Do not interpret producer enqueue time as completion time or claim
throughput from an artificial pipeline without accounting for the drain.
The goal remains >=59 completed presentations/s over a settled 60 seconds
in the user's heavy scene with fresh full-rate shadows and unchanged game
behavior. That requires a later user capture; this screen cannot finish it.

## Follow-up: actual local effect workload

The released harness's 600-frame on/off comparison matches every hash and
activates the worker, but intentionally toggles it every 50 frames. Its
separate `bench` option measures enqueue overhead without a scene. Neither
provides sustained heavy-scene performance evidence.

Add a separately built, independently written adapter around our existing
`bench_effect.c` helpers. Load the external DLL only by an explicit path in
this developer executable, resolve its harness entry points (install,
toggle and stats), and compare wrapped passthrough with threading using the
local default unit and shadow effect lifecycles. Retain an unhooked state
oracle captured before installation. Read actual device state through saved
original function pointers after queue/GPU drain, so a stale wrapper mirror
cannot establish correctness. Changed inputs must be observable, and a
deliberately poisoned actual constant must be detected by the oracle.

Each timing sample includes 1,222 synthetic material groups, repeated four
times. Alternate mode order, preserve 21 paired samples per technique, and
report caller and drained completion time separately. Generic parameter
setters, no geometry/draws, no native source callbacks and a wrapped-off
timing baseline are explicit limitations. This screen does not prove fresh
shadow images or qualify coexistence with sage-boost's production hooks.
No third-party implementation is copied or linked; only its external test
ABI is used. The loaded DLL and game effects remain outside the repository.

## Result and next boundary

The two final runs pass all state controls and preserve 84 paired samples.
Lit completion deltas are -0.1158 and -0.0824 ms; shadow deltas are -0.4526
and +0.2162 ms (positive means faster). This does not support a gameplay
trial, but geometry and full pipeline overlap are absent from this screen.
Test actual draws and fresh shadow output next before judging the approach.

The work produces evidence about an external binary, not a production
integration. Existing stable branches and the faction unlocker remain
untouched. A separately backed-up restoration returned the installed files
to identical tested beta.13 after the launcher replaced them with 0.9.10;
see the restoration receipt. The comparison has no relaxed gameplay invariant
and no assumed multiplayer fix. Record incompatibilities before selecting
the next experiment.
