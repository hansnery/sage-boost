# Updated Open-BFME-2 performance investigation

## Reference update

At the user's explicit request, fast-forwarded the reference checkout from
`37ee1f9762ef5cbd1022402d3d387100e813a936` to
`ff1544d9fef63a5460d1fcd012cea309c13f7dff`: 6,410 commits. Command:
`git -c submodule.recurse=false pull --ff-only --no-recurse-submodules origin master`.
This is the requested exception to its read-only rule; no authored changes
were made there. The pre-existing untracked `reverse/zh_sweep/` remains.
The separate Grok worktree was not operated on. Its shared repository's
remote refs naturally changed with fetch, but its checkout/branch did not.
The broken BFME1 submodule gitdir link was not repaired or recursed into.

## Stronger evidence for batching

Newly available `Code/Libraries/Source/WWVegas/WW3D2/Rva0017438BPerformRendering.cpp`
is recorded as matched, 819 bytes at research RVA 0x0017438B in the reference
ledger. Recovery commit `108dc1d11cd`, later link repair `387bdbef108`.
Its retained "BANKED PARTIAL ONLY" source comment conflicts with that newer
ledger/history; do not treat it as an entirely clean ABI specification.
The previous audit's statement that only a partial attempt was available is
now outdated. The installed AotR/game820 instructions still need independent
verification before any new hook is installed; BFME2 reference addresses
must never become installation addresses.

The recovered loop establishes a much narrower optimization boundary than
replacing the scene renderer: after group equality, material setup and
BeginPass, each single-mesh iteration constructs a diagnostic label/event,
passes an owning method reference to Render_With_FX_Material, destroys the
event and advances the task iterator. The existing plugin already omits the
mesh render callee for compatible instancing members but retains that outer
iteration. It is reasonable to prototype a direct run of those same members.

Keep original groups, original order and Begin/End boundaries. A run must
stop before verification sampling, unsupported shader/layout, model/base
vertex changes, failed staging, device change or an exceptional path.
Account for the owning method temporary: removing both AddRef and Release
is different from skipping just the callee (which currently decrements it).
All detailed scope/count diagnostics must retain documented meaning. No
runtime shortcut was installed by this investigation.

The beta.2 steady trace averages about 7,602 mesh markers and 668 groups per
regular frame, with about 6,837 meshes counted as instanced (these CSV values
are rounded per-frame counters; they are estimates). This establishes scale,
not savings. Existing batching handles much of the expensive mesh callee
already; the remaining loop must be benchmarked as a whole.

## Measured and deprioritized: event buffer copies alone

`BFME2ScopedRenderEvent.cpp` and `BFME2ScopedRenderEventDtor.cpp` establish
label/group buffer initialization. `BfmeDX8Callbacks.cpp` and the D3D setup
in `dx8wrapper.cpp` show that the downstream PIX callback can be null even
though the constructor still performs its copies. Installed AotR and
`game820.dat` constructor/callback disassembly confirmed this shape.

An isolated native harness read the local executable into private memory,
found event entry points using the plugin signatures, resolved the user's
MSVCR71 strncpy import and relocated only these inspected routines' absolute
operands. PIX callbacks were explicitly null. It called only the event
constructor/destructor; no executable entry point, game initialization or
game loop ran. No game file was changed or saved. Output label/group strings
were checked before timing. Five runs of seven million pairs each yielded:

- AotR: 0.240-0.264 ms per 7,000 pairs.
- game820: 0.237-0.245 ms per 7,000 pairs.

This is a warm-cache microbenchmark during a background repository update,
not a frame-time comparison. It excludes the caller's multibyte label
construction, plugin hooks/timing, actual mesh work and GPU activity.
It is sufficient to reject **buffer-copy elimination alone** as the main
route to several milliseconds of savings, not to bound the entire loop.
Exploratory harness is retained locally in `perf/build/event_cost_probe.c`
and `.exe` (ignored, not shipped). No EA bytes are embedded in the source.

## Other source findings and limits

1. **Narrow ambient-lightmap sampling cache, conditional opportunity.**
   `RTS3DSceneRenderOneObject.cpp` samples terrain ambient colour before its
   object/shroud/light work. `TerrainLogicAmbientColorRva0027DA6A.cpp` describes
   a matched bilinear sampling body with four texel accesses and floor calls.
   A bounded cache for the exact same position and unchanged map across passes
   could avoid repeats without changing output. However, a null ambient map
   returns immediately, and the capture does not establish that a map is in
   use or that this cost is material. First measure map presence/hit rate and
   cost; preserve the original function on misses and invalidate on map writes,
   reloads and relevant FP context changes. No sharing across moved extra-frame
   positions by object identity alone.
2. **Animation sharing is a larger, constrained option.** Matched
   `Animatable3DObjClass::Update_Sub_Object_Transforms` and
   `HLodUpdateSubObjectTransforms.cpp` show parent/slave dependencies, captured
   bones, subobject visibility/factors and transform propagation. Similar model
   names do not imply identical poses. The measured ~1.6 ms regular skeleton
   scope limits its standalone upside in the earlier capture; it cannot simply
   be moved to workers or cached without input/dependency proof.
3. **Do not bypass whole renderOneObject calls.** The matched 2,550-byte
   `RTS3DSceneRenderOneObject.cpp` is stronger evidence than the old
   present-unmatched W3DScene donor used by the prior audit. It performs shroud
   queries, writes the drawable's shroud-clear frame, observes scene membership,
   and balances material passes. A cached whole call could change gameplay-
   visible state. Current narrow light optimizations remain distinct.
4. **Do not retain bindings across End_Scene blindly.** `dx8wrapper.cpp`
   releases vertex/index/texture/material references after each pass and resets
   its light-environment cache. Saving those operations changes resource
   lifetime and cache assumptions; it is not just redundant driver traffic.

## Next experiment

Update: the [dispatch benchmark](2026-10-17-dispatch-benchmark-results.md)
is complete. Its optimistic 1.4-1.7 ms saving falls short of the screening
gate; no installed fast path was added. The proposal below records the
experiment that led to that decision.

Prioritize a native benchmark and target-disassembly proof of the complete
compatible-member loop, comparing current per-member hook dispatch with
bulk staging through existing instancing code. If the isolated saving is
small, stop before adding runtime hooks. Keep shadows full rate and retain
all gameplay/deadline safeguards.

The extra-frame estimate blind spot remains separate: beta.2 observed no
busy-scene extras, so its 14.641 ms estimate is not a measurement of current
busy-scene cost. Do not claim that the early 3.835 ms mean applies there.
CPU-side wall times also include possible driver waits; these captures alone
do not prove that every millisecond is CPU computation or rule out GPU stalls.

No new performance binary, settings changes, installed changes, or 60 FPS
claim result from this source investigation.
