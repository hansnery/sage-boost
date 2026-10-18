# Material-chain bulk dispatch screen

The previous shadow-fragmentation lead needs a correction. The copied
beta.11 log's cumulative summary reports 3,081,904 shadow-state comparisons,
6,119 original splits and **zero remaining splits** after the existing
shadow batching hook. This is an untimestamped cumulative summary, not a
measurement covering every second in the settled interval. It contradicts
attributing the recorded fragmentation to opacity without further evidence.

The native group body checks material-method counts and every method
pointer after the mesh-state comparison. Two adjacent groups can share a
model and first method while differing in later methods. The current
follower bulk dispatcher also explicitly requires a method count of one.
Leader deferral and ordinary instancing do not have that restriction.
Consequently the earlier leader-count inference cannot establish that
followers took the bulk loop; multi-method coverage remains unknown.

## Isolated experiment

`tools/prepare_chain_screen.py` generates an ignored copy of our dispatcher
with a switchable method-count gate. It asserts the old expression occurs
once and changes only that expression plus the test callback declaration.
Production source and the installed binary are unchanged.

Both timed paths use the actual installed group/Draw/DIP hook machinery,
leader deferral, ordinary instancing and native group/mesh/FX bodies. The
baseline gate accepts one method; the candidate accepts one through six.
Every other dispatch guard remains. Both paths retain identical GPU draw
counts and geometry. No material groups are merged: the native comparison
of the complete method arrays remains intact.

The test uses native chain-link and clear walkers with controlled owning
SetNext methods. A transferred reference remains owned until its link is
cleared. It checks all references, empty links after native return, record
bytes, effect/engine state, normal/extra output, scope counts and ABI. A
boundary case has the same model and first method but a different final
method; both paths must retain two native groups.

There are 22 chain-specific regular/extra comparisons per run, covering
lengths one through six, in addition to the existing single-method native
controls. The normal-path matrix includes both installed executable
families, native D3D9/DXVK, six/twenty bones and lit/fresh-shadow rendering.
Three negative controls erase the secondary-method boundary, leak a child
link or deny candidate bulk dispatch. Their respective oracles check group
boundaries, references and consumed records.

Fixture repairs during development moved inherited controls after creation
of the outer state block and adopted the existing 4,096-word effect
snapshot capacity. The earlier setup-time call faulted; the lit effect
correctly refused the smaller snapshot. Neither was a production change.

## Results and next gate

All sixteen normal-path matrix runs pass, as do six negative-control runs
that reject their intended faults (three per backend). The generated
dispatcher passes the general-register audit: 445 decoded instructions,
with no x87, SIMD or FP-control instructions.

Eight AotR/DXVK runs retain 168 alternating paired samples. Median CPU
savings are **2.21-4.55 ms per regular/extra pair**; fenced completion
savings are **2.29-4.75 ms**. The paired CPU p10 is positive in every run,
with considerable spread. Six-bone cases save 3.18-3.89 ms of completion;
twenty-bone cases save 2.29-4.75 ms. These ranges describe separate synthetic
workloads, not uncertainty bounds for the user's saved game.

This is large enough to justify further runtime qualification. It is not
yet a production change or an installation recommendation. The native
loop's method-chain setup and teardown surround the draw loop unchanged;
the proposed removal targets per-follower wrappers that existing instancing
already bypasses at the mesh entry. Qualification must still exercise
nontrivial child behavior, zero/partial staging, GPU failures, next-group
state and native exits. In particular, do not assume native Begin failure
or exception paths always clear child links; compare against the actual
native behavior rather than imposing a new cleanup policy.

[Validation, hashes and distributions](../benchmarks/2026-10-18-material-chain-checks.json)
and [all 168 samples](../benchmarks/2026-10-18-material-chain-samples.csv)
preserve the result. The installed beta.11/INI hashes are unchanged.

## Reproduction and interpretation

Build `build/bench_chain_screen.exe` and `build/bench_chain_screen_20.exe`
from `perf/` with the documented i686 toolchain. Run:

```text
python tools/bench_native_fx_gpu.py IMAGE EXE SHADERS CRT shadow narrow 10 leader-bulk-first 6
```

Use `lit` for lit cases and `2` for the two-method timing workload.
`PF_CHAIN_SCREEN_CONTROLS=1` runs only correctness checks;
`PF_CHAIN_SCREEN_BROKEN=1`, `2` or `3` selects a deliberate failure. These
are test-executable environment variables, not plugin settings. DXVK runs
have the user's D3D9 proxy beside the test executable in an ignored folder.
Run serially with the game closed. The test never launches the game.

The paired benchmark alternates the two gates inside one executable, with
four warmups, 21 retained rounds and eight regular/extra pairs per round.
Unlike the previous verification-window cross-run comparison, the printed
saving here is the incremental gate change. Full-group state checks and
test observers are shared overhead. Queue collection/sort is not included.

Child-method subclass behavior and native material-source computation
remain controlled. Normal-path success does not qualify arbitrary chained
callbacks, GPU failure or exception recovery. The timed workload uses 288
groups of 25 synthetic meshes, all with the selected method count; it is
not a saved-game replay or evidence of live coverage. These limits must be
resolved before treating this as an installable significant improvement.

The actual saved-game result remains 30.0667 presentation calls/s. Keep
beta.11 and its INI installed while evaluating the candidate. No new
gameplay test is requested by this screen.

Follow-up: [material-chain recovery qualification](2026-10-18-material-chain-recovery.md)
now covers controlled child shader/state behavior, nonempty output,
partial staging, GPU replay and failed-Begin/next-group links. The runtime
guard remains unchanged until the separate production promotion checks.
