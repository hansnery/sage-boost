# Offline material-queue experiments

These tests extend the retained GPU-setup fixture with the local game's
`defaultw3d.fxo` and D3DX27 effect lifecycle. No game is launched, no installed
file is changed, and no EA shader bytes are saved. These executables are not
linked into the ASI.

From Git Bash with the documented i686 toolchain:

```sh
cd /c/repositories/sage-boost/perf
mingw32-make bench-material-queue
mingw32-make bench-material-queue MATERIAL_CHURN=10
mingw32-make bench-material-queue MATERIAL_CHURN=0
mingw32-make bench-material-queue MATERIAL_CHURN=100
```

Each target runs lit and shadow workloads on native D3D9 and DXVK, serially.
`DISPATCH_ASSETS` selects the local shader/CRT directory; D3DX27 is loaded
from the CRT's directory. `DXVK_D3D9` selects the local DXVK library. Missing
assets, hardware, supported shader or required state cause failure.
The read-only process guard also refuses before setup or between samples
when the game, either BFME launcher or WorldBuilder is running. It does not
pause, terminate or change any process.

Executable arguments: `Shaders.big msvcr71.dll lit|shadow narrow|all`,
optionally followed by `0`, `10` or `100` for persistent caching. Without
that final argument, every regular draw records every group, as in the
earlier retained experiment. The published measurements use `narrow`.

## Workload and alternatives

The fixture uses 288 groups of 25 distinct synthetic meshes, six bones per
mesh, and the real instancing/extra-frame hooks. An 18-by-16 layout gives
each group its own screen region; groups no longer all overlap. Two
independently generated cutout textures, eight opacity settings and four
diffuse colours exercise material state. Both passes use the original
effect's vertex and pixel shaders. The bound shader hashes are checked
against the plugin's supported table before measurement.

Each baseline pair runs a regular frame with Detail=1 and an extra frame
with the existing extra-frame timing path. Every group applies a material
parameter block, sets synthetic dynamic inputs and its texture, and runs
Begin(flags=6), BeginPass, leader palette SetRawValue/CommitChanges,
EndPass and End. The plugin instances the remaining group members normally.
The fixture models the engine's call structure independently; it does not
execute its parameter-source callbacks, scene traversal or native FX loop.

The recapture alternative records each leader's GPU state in the regular
frame. The extra frame replays it, uploading fresh palettes for all meshes.
The regular capture overhead is included in each pair's measured cost.

The persistent alternative also reuses preparation in regular frames:

1. Apply the same parameter block and dynamic setters as the baseline.
2. Read every used non-bone numeric/struct parameter and texture identity
   into an exact byte key (680 bytes lit, 260 shadow). GetTexture references
   are balanced. There is no hash-only equality decision.
3. On a hit, replay the group with fresh palettes. On a miss, run the normal
   effect/instancing path and replace its snapshot and key.
4. Before a miss following a hit, restore ordinary geometry bindings and
   stream frequencies. Omitting this initially made the fixture reject
   capture, and mixed hit/miss image tests now exercise this boundary.

The `10` mode changes 30 of 288 groups each pair, by alternating opacity,
diffuse colour and texture. The `0` mode is a warm all-hit best case; `100`
invalidates all groups each pair. Key reads/comparisons, input preparation,
miss processing and state reconciliation are all timed. Initial resource
allocation and cold shader compilation are outside timed samples.

The state mask is obtained from the actual effect's recorded command keys
under all eight material combinations. Unsupported command kinds fail.
Besides the existing geometry/shader/constants mask, lit adds the effect's
11 render-state keys, 13 sampler-state keys and two texture slots; shadow
uses seven, five and one respectively. No bone constants are retained.
Every shadow frame clears and redraws the map.

## Correctness checks

- Full image comparisons after changed poses; stale-pose replay must change
  more than 100 texels. Shadow depths must also remain in [0,1].
- Change every material, demonstrate that stale replay gives a different
  image, then require fresh capture to match the baseline.
- Persistent mode compares both regular and extra images with zero,
  partial and complete invalidation, and checks the exact miss counts.
- All recorded render/sampler/texture state is restored at replay
  boundaries. The shared fixture also checks VS/PS float constants,
  declaration, shader, vertex stream/frequencies, method reference balance,
  instanced draw/mesh counts and rejected missing snapshots.

Four warmups precede 21 AB/BA samples; each averages eight separately fenced
regular-plus-extra pairs. Submission includes driver work/stalls;
completion additionally includes the final event-query wait. Neither is
a GPU timestamp or a presentation measurement. All samples are retained.

## Limits on runtime conclusions

This is a controlled, fixed-geometry fixture. Its borrowed object/cache
pointers, group-index identity and fixed input producers are not safe
runtime cache keys. It does not prove geometry/resource lifetime, reset,
arbitrary materials, multipass ordering, visibility/shroud, actual engine
state-cache reconciliation, or fallback after a partially replayed queue.
Snapshot objects are released at the end; the fixture owns the remaining
resources for its process lifetime.

Both alternatives and their baseline restore the same enclosing GPU state.
That does not prove the engine would expect that final state. Persistent
hits also omit regular per-mesh markers; a production path must preserve
meaningful diagnostics and account for that cost. No diagnostic setting is
changed in the installed game.

Lit and shadow each use the complete 7,200-mesh workload. **Do not add their
savings**, add these savings to the earlier dispatch/cache experiments, or
claim a saved-game FPS from them. The persistent cache's hit rate in the
user's save has not been measured. Animation evaluation, camera, shroud,
scene construction, UI, Present and scheduler costs are outside the test.

The first 504 recorded timing samples were excluded after a concurrent
game process was discovered. See the
[results and exclusion](../../docs/specs/2026-10-17-material-queue-screen.md).
Do not present them as clean performance evidence.

## Stronger lifecycle and marker controls

`MATERIAL_PROTOCOL=lifecycle` (with an explicit `MATERIAL_CHURN`) retains
real Begin/End on hits and prepares/validates inputs in extras as well.
Changed extra inputs use the existing normal draw/capture path before any
replay. It sends neither CommitChanges nor EndPass for a cached pass.

`MATERIAL_PROTOCOL=lifecycle-markers` also retains each mesh label, modeled
native event copies, production marker hooks and scope accounting. Markers
cover staging and batch submission. Every regular frame verifies exactly
7,200 scopes with no stale scope; extras keep existing counting semantics.

Both modes validate individual effect End boundaries with an opposite
entry cull state and test a material change between regular and extra draws.
Those additional render-state reads stop before measured warmups. The
existing frame-boundary state checks still run on both timed paths.

[Clean results](../../docs/specs/2026-10-17-material-queue-clean-results.md)
supersede performance qualification of the original screen. Retaining
lifecycle and marker work reduces DXVK gain to roughly 0.7-1.0 ms in the
final repeat runs. No runtime cache is promoted. Fixed-object lifetime,
native group behavior and engine state remain unproved.

## Aggregate logical-mesh accounting experiment

`MATERIAL_PROTOCOL=lifecycle-bulk-markers` retains the stronger lifecycle
and key protocol but measures one staging/submission interval per hit and
attributes its actual time to the logical mesh count. Native-style individual
event copies are omitted only in this callback-free fixture. This is a
different measurement boundary, not identical native event behavior.
The test-only timing object preserves counts and per-pass totals; it is
never linked into the ASI. Regular misses keep original per-member events.

[Repeated results and native integration gates](../../docs/specs/2026-10-17-material-bulk-accounting-screen.md)
originally showed about 3.0-3.5 ms per modeled pair on DXVK. The subsequent
[clock/state controls](../../docs/specs/2026-10-18-material-cache-boundary-clock.md)
remove that qualification: the earlier clock was fixed and state restored
only at the frame boundary. No runtime cache is promoted.

`MATERIAL_PROTOCOL=lifecycle-bulk-only` isolates aggregate bulk dispatch
without retained material state. The first two mesh dispatches and every
effect Begin/BeginPass/EndPass/End stay original; only remaining compatible
members use the existing offline bulk prototype. Common `retained_*` CSV
columns denote this optimized path. Cache-specific validation is skipped;
fresh-pose, complete image, state and logical count checks remain.

## Advancing clock, per-group state and device-write filtering

`lifecycle-bulk-state` captures/restores the full fixture touched-state mask
per replay hit. `lifecycle-bulk-clock` advances Time between pairs, leaving
it fixed within each pair; every regular exact key misses. The combined
`lifecycle-bulk-state-clock` retains both controls. Negative restoration and
binding-corruption checks run outside measured samples. The combined mode
regresses on DXVK; see the clock/state report linked above.

`state-filter`, `state-filter-bulk` and `state-forward` instead keep every
effect Begin/BeginPass/CommitChanges/EndPass/End and source setter. Separate
effect instances compare the default manager against a custom callback
manager attached before its first default Begin. Both advance Time.
Render-state and texture restoration costs are included, with a complete
mirror invalidation at frame boundaries. The filter object uses production
compiler flags but borrows the fixture's fixed resources and state mask.
It has no runtime native-state interception or failure/lifetime policy.

The filter-only variant keeps original member events. The bulk variant
uses aggregate accounting after the first two members. The forward control
issues every manager request. Common CSV `retained_*` fields mean the
alternative path; these modes contain no retained-material cache.

Correctness compares full extra images/fresh poses and frame state/counts,
plus 32 consecutive/changed-material in-pass and post-End state cases.
A stale mirror after an external texture write is detected; invalidation
recovers. Actual callbacks from the game are still outside the fixture.
[Results and exploratory failures](../../docs/specs/2026-10-18-effect-state-filter-screen.md)
do not qualify any of these variants for installation.
