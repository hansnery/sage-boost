# Offline retained-batch screen

Run `mingw32-make bench-retained` from `perf/`. This runs lit and shadow
fixtures on native D3D9 and the installed DXVK. Run
`mingw32-make bench-retained RETAINED_MASK=all` for the broad state-block
control. Missing game shaders, CRT, DXVK or hardware device fails the run.
`DISPATCH_ASSETS` and `DXVK_D3D9` select other local installations.

The executable arguments are the local `Shaders.big`, `msvcr71.dll`,
`lit` or `shadow`, and `narrow` or `all`. It never starts the game, loads
game.dat, writes EA shader bytes, or changes the installed plugin/INI.

## Compared paths

Both paths use 288 groups of 25 meshes, 7,200 distinct mesh/container/tree
identities, six bones each, the dispatch fixture's real local game vertex
shader, and the plugin's instancing implementation. Geometry is synthetic;
nine poses repeat across the identities. Per-group opacity varies.

- Baseline: a regular draw with Detail=1, then an extra draw using the real
  extra-frame hook path, which already omits per-mesh timers. The test-only
  `fps_extra_bench.c` toggles that path without running the FPS scheduler.
- Retained: the same regular draw, additionally capturing GPU state after
  each leader. The extra draw restores each group's snapshot, stages all
  meshes from their **new** palettes (including the leader), and submits
  two instanced draws per group. GPU state stays bound between those draws;
  the enclosing fixture restores its entry state once at the end.

The regular draw pays for 288 Capture calls. Allocation happens initially,
outside timed samples. `all` uses D3DSBT_ALL. `narrow` records only the state
this fixture changes: VS/PS, declaration, indices, two streams/frequencies,
seven render states, non-bone vertex constants and 32 pixel float constants.
This mask is **not** valid for arbitrary game materials. Explicit Capture
is performed after creating a block as well as on subsequent frames.

Both baseline and retained extra draws are enclosed by the same entry-state
save/restore. This isolates the prototype's internal costs; it does not
prove that replacing a game FX flush with this boundary preserves the
engine's expected final state. Neither path includes effect Begin/End,
the game's parameter callbacks, scene traversal, animation evaluation,
shroud, UI, Present or the scheduler. Do not add lit and shadow results:
each is an alternative workload at the full regular-frame mesh scale,
not measured extra-frame pass counts from the saved game.

## Checks and measurement

Compare all output texels after moving every palette. Lit tolerance is two
8-bit channel steps; depth uses the existing instancing depth tolerance.
Also render with the old palette and require more than 100 changed texels,
so replaying stale poses cannot pass by accident. The map is cleared and
redrawn every frame; no previous shadow map is reused.

Check method references, 576 retained draws/7,200 submitted meshes, restored
VS/declaration/stream/frequencies, byte-identical restored VS/PS constants,
and instancing still enabled. Missing/cleared snapshots and invalid slots
are rejected. These are fixture checks, not complete lifetime/fallback
proof for game objects or device reset. A failed draw aborts the executable.

Four paired warmups precede 21 AB/BA samples, each averaging eight separately
fenced regular-plus-extra pairs. `regular` includes state capture;
`extra` includes palette staging and replay. `pair` also includes the same
synthetic pose change on both paths. `complete` includes the final event
query wait. Neither is pure GPU time; there is no presentation/vsync.
Readback and initial shader compilation are outside timing.

Results: [retained-batch screen](../../docs/specs/2026-10-17-retained-batch-screen.md).
