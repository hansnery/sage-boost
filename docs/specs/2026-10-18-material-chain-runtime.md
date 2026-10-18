# Runtime bulk dispatch for material chains

Beta.13 promotes the [qualified material-chain gate](2026-10-18-material-chain-recovery.md).
The native renderer already establishes equal method counts and identical
complete method arrays before the follower loop. `bulk.c` now accepts counts
one through six instead of only one. Zero and out-of-range counts remain
ineligible. Every other dispatch and submission condition remains intact.

Native Begin/Pass/Dynamic/End and link ownership retain their existing
behavior. This does not merge different materials, reduce geometry or
shadow work, change camera motion, or retime simulation/network/input.
Existing instancing already skips matching follower mesh bodies; the
incremental change avoids the native wrappers around those skipped calls.

## Coverage reporting

`pf_inst_bulk_advance` records accepted followers by native method count.
Twelve raw per-row columns are appended to the timing CSV:
`bulk_followers_1..6` and `extra_bulk_followers_1..6`. Normal/extra counts
are separate. Divide the normal counts by the row's game-frame count when
estimating coverage per frame. These are consumed records, not proof of
successful drawing; deferred leaders are excluded. Existing error and
presentation counters retain their meanings.

The counters are independent of the 64-name scope table, which filled in
the previous capture. Method-count coverage can now be measured instead
of inferred from leader deferrals. They do not affect eligibility or
scheduling. No INI key or default is added.

## Validation

- The full `mingw32-make test` suite passes, including CSV values/ordering,
  absent instrumentation, maximum row width, plugin version/load, native
  hook compatibility, group lifetime and Windows exception unwind.
- Sixteen GPU recovery runs link the actual production `bulk.o`, for both
  executable images, native D3D9/DXVK, six/twenty bones and lit/fresh shadows.
  Each runs chains of two through six, nonempty native pixel references,
  immediate device/effect/chain snapshots, partial staging, GPU failures,
  following groups and retained links after failed Begin. All pass.
- Six selected broken-replay controls are rejected on the specified pixel,
  logical-count or immediate-state oracles. The preceding qualification
  separately rejects all 22 child/link/gate/replay corruptions.
- Ten audited runtime/adapter objects contain no x87, SIMD or FP-control
  instructions. The approved scalar-SSE math file is unchanged.
- The switchable screen still compares old/new gates in one executable;
  its generated dispatcher body has the same hash as the original screen.
  The new counters participate in both paths and their exact method/extra
  attribution is asserted during all 22 chain comparisons.

Fresh twenty-bone/six-method DXVK timings retain 42 alternating paired
samples. Median CPU saving is **3.4398 ms** for lit units and **2.9999 ms**
for shadows, per synthetic normal/extra pair. Fenced completion saving is
**3.4243 ms** and **2.6242 ms**, respectively. Paired CPU p10 is positive
(2.5119 ms lit, 2.2231 ms shadow). These separate workloads must not be
added together as if they were one saved-game frame.

[Validation and hashes](../benchmarks/2026-10-18-material-chain-runtime-checks.json)
and [42 timing samples](../benchmarks/2026-10-18-material-chain-runtime-samples.csv)
record the tested build. Native group/chain/mesh/FX bodies execute against
the real local GPU/effect; child subclasses and source computations remain
controlled. No new claim about arbitrary native child exceptions follows.

## Deployment decision and limits

The repeatable 2.6-3.4 ms completion saving per pair warrants a substantive
beta candidate under the user's standing installation authorization.
The timed workload has 7,200 synthetic meshes, all in six-method groups;
live coverage remains unknown until the next capture. This is not a 60 FPS
claim or a prediction for the saved game. The last settled gameplay result
is still 30.0667 presentation calls/s.

Beta.13 also carries the previously tested beta.12 verification-window fix:
selected samples remain native while unsampled groups can retain bulk
dispatch. Preserve the installed INI, full-rate shadow preferences and
stable branches. Install only the tested ASI after a versioned backup and
hash verification; never launch the game automatically.
