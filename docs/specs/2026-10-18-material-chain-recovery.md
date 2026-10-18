# Material-chain recovery qualification

This follows the [gate timing screen](2026-10-18-material-chain-bulk.md).
The candidate remains an ignored generated dispatcher; this qualification
does not change the installed plugin or its INI.

## Contract being changed

The candidate admits method counts two through six into the existing
follower loop. The verified native group body still compares the complete
method array, links it, begins the effect/pass, and performs all End/clear
operations. Existing ordinary instancing already skips the mesh bodies of
matching followers. The incremental change removes their native label,
scope and temporary reference wrappers. Event callbacks, capture, geometry,
single-pass, identity-anchor and submission guards remain unchanged.

No new native child-class implementation is substituted. The chain fixture
deliberately supplies controlled callbacks, while executing the verified
native chain walkers, group/mesh/FX bodies, installed entry hooks and real
local effect/GPU. This tests the boundary contract, not every game's
possible material subclass or source callback.

## Failure and state tests

`native_chain_fixture.h` shares the screen's owning links. The native
SetNext caller transfers an already acquired reference; links retain it
until the native clear walker releases it. `test_chain_recovery.c` runs
lengths two through six. Child Begin/Pass/Dynamic/End callbacks maintain
phase state and nest real `D3DRS_COLORWRITEENABLE` and material diffuse,
ambient and opacity changes. Native Dynamic commits the child inputs to
the real effect. EndPass restores states in reverse child order. Every
case requires more than 100 covered pixels, including its ordinary native
reference, so matching empty images cannot qualify the candidate.

The inherited GPU fault proxy rejects each operation in the first and
second batches, with original native rendering of 25 distinct unit poses
as the pixel/count reference. It also rejects pre-deferral palette capture
and null-stream restoration. Each submission/replay boundary snapshots all
native fields, effect parameters (including bones), device state, owning
links, child phases and render state. Exact restoration is required there.

Partial-staging controls reject a member before and around capacity
boundaries, both with an untouched next slot and with a poisoned partial
write. They verify pending replay, consumption, counts, pixels and the
following group. Final chain state agrees with the ordinary native path.
Clean optimized cases must consume every follower, preventing a disabled
candidate gate from accidentally passing as a slow fallback.

Ordinary rendering leaves the last mesh's palette in the effect; existing
instancing leaves the leader's. The test does not incorrectly require those
final palettes to match. Full palette equality is checked immediately
around each batch/replay; final comparison checks the chain and its device
state. Both paths retain the existing pixel and logical-count oracles.

Failed native Begin retains the owning links. Each comparison records that
state, then executes a successful group with those same retained links and
requires normal clear/refcount behavior. The two failure paths start with
identical material inputs/palettes; otherwise the preceding successful
group would contaminate the comparison. No extra cleanup is imposed on
the game. Native exception behavior is outside this new chain fixture;
the existing production lifetime test covers the unchanged wrapper's
return, nesting and Windows unwind behavior.

## Reproduction

Build `build/test_chain_recovery.exe` and `build/test_chain_recovery_20.exe`
from `perf/` using the documented i686 compiler. Then run serially:

```text
python tools/test_chain_recovery.py IMAGE EXE SHADERS CRT lit narrow 10 leader-bulk-first
```

Use `shadow` for fresh shadows. Test both executable images and native
D3D9/DXVK; the DXVK copy is beside the executable in an ignored directory.
The launcher guard refuses competing gameplay. No game is started.

The optional `--fault N` argument (`N=1..7`, before `IMAGE`) selects inherited broken count,
batch/pending replay and state-restore controls. `PF_CHAIN_SCREEN_BROKEN=2`
leaks owning links and `=3` disables the candidate gate.
`PF_CHAIN_RECOVERY_BROKEN=1` corrupts the child's shader opacity; `=2` omits
its state restore. These environment variables affect this test executable
only. Deliberate-failure runs skip the independent single-method controls;
every normal run still requires them.

## Oracle repairs during qualification

The first negative-control runner placed the corruption argument before
the native manifests, producing a file-open failure. Those runs were
discarded. The dedicated launcher now appends it to the complete executable
command, and failures must occur after native FX initialization.

An initial child-mask corruption survived. A coverage check then showed
that masks excluding alpha could leave this lit fixture's target empty,
even while draw counts and state checks passed. The final child masks keep
alpha, normal opacity stays one, and child shader-input corruption must
change output. The repaired six-bone lit counterexample changes 167 pixels
and is rejected by the pixel oracle. Earlier normal results without the
coverage check do not count as final qualification.

On DXVK, the omitted null-stream seed control reaches the required-call
coverage assertion: the selected seed API was omitted, so its planned
rejection never occurred. Native D3D9 rejects that mutation through the
immediate-state check. Do not describe the DXVK rejection as a measured
state leak; both are specific oracles for the intentionally removed call.

## Promotion boundary

The final matrix passes all sixteen normal runs and rejects all twenty-two
intentional corruptions at their specified oracles. Across both executable
images, both backends, both bone counts and lit/shadow workloads, this
includes 4,480 batch API failures, 160 pre-deferral capture failures, 320
null-stream failures, 3,840 partial-fallback cases and 80 failed-Begin pairs.
Each normal run also passes the 136 inherited native group controls.
[Results and hashes](../benchmarks/2026-10-18-material-chain-recovery-checks.json)
record the final sources/binaries and unchanged installed beta.11/INI.

The shared-helper regression also passes the original 22 chain comparisons
and 136 native group controls on ROTWK/native six-bone shadows and AotR/DXVK
twenty-bone lit rendering. Any runtime patch still requires versioning, full-suite
and instruction checks and validation of the actual linked dispatcher.

Live method-count coverage remains unknown: the copied gameplay scope
table is capped at 64 names and contains no bulk scope entry. Absence of
that name does not establish absence of bulk work. Any eventual substantive
runtime change should report actual consumed followers by method count,
rather than inferring them from leader deferrals. The last real saved-game
measurement is still 30.0667 presentations/s.
