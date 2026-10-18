# Offline compatible-member dispatch benchmark

`mingw32-make bench-dispatch` runs synthetic lit and shadow workloads on
native D3D9 and the installed DXVK. It requires the user's `Shaders.big`,
`msvcr71.dll`, a hardware D3D9 device and DXVK; missing resources fail.
Override `DISPATCH_ASSETS` and `DXVK_D3D9` for another installation.

This executable never launches the game or loads its executable. Shader
bytes stay in memory. No EA assets are embedded or written out. The helper
in `inst_dispatch_engine.c` includes the real instancing implementation for
access to its private staging functions, but is not linked into the ASI.

## Comparison

- Current: construct a synthetic mesh label with the local CRT's multibyte
  copy/concatenation, enter the real event stub/timing code, model the event
  buffer copies, acquire the method temporary, enter the real instancing
  stub with its expected registers/stack, and close the event.
- Optimistic bulk: do the same for the first two members (leader capture
  and evaluation), then borrow the method and stage compatible remaining
  records directly. Flushes use the same `draw_batch` and restore state.
  No new game-loop hook, branch signature or continuation is implemented.

The bulk prototype deliberately omits the skipped members' marker work.
It is a screening experiment, not a production implementation: it cannot
preserve the current detailed timing semantics. Do not install it, export
its helper or claim it is ready for gameplay.

Each workload has 288 groups of 25 meshes: 7,200 total, 6,912 instanced,
288 leaders and 576 instanced draw calls. This approximates the beta.2
steady trace's 288 instancing groups and 6,837 instanced meshes per regular
frame. All-lit and all-shadow are alternative workloads at that scale;
their timings must not be added as if each represents a whole separate pass.
There are 7,200 distinct fake objects/containers/palettes, an 11.29 MB data
working set. Each mesh uses six bones and the fixture's 128-triangle grid.

Before timing, the inherited GPU tests must pass. The current and bulk
images must then match bit for bit with nonempty coverage, both for colour
and R32F shadows. Every measured workload checks method-reference balance,
leader/instanced draw counts, bulk consumption, shadow attribution, absence
of stale scopes, restored vertex shader/declaration/stream/frequencies and
the instancing session still enabled. It is not exhaustive validation of
bulk fallback on arbitrary game objects or device errors.

## Measurement

Production files compile with the ASI's `-O2 -mgeneral-regs-only` flags;
only the synthetic fixture uses floating point. Each mode is warmed four
times, then measured in 21 paired samples, alternating AB/BA order. Each
sample averages eight workloads, fencing each one separately before and
after. Shader compilation, file reads and pixel readback are outside timing.

`submit` measures BeginScene through EndScene, including any driver stalls.
`complete` also includes waiting for the D3D9 event query. Neither is pure
GPU execution time. There is no Present or vsync. Report the median of
paired differences, not a percentage FPS forecast. Detail=0 is a control;
the user's installed Detail=1 remains unchanged.

This is intentionally incomplete as a scene model: one material/model,
synthetic geometry and nine repeated poses; no terrain, shroud, effect
Begin/End, actual game caller/SEH, game model-name getter or PIX callback.
It reuses the existing fixture's synthetic leader and direct DIP adapter.
GPU output, identity count and hook overhead are real, but this is not a
replay of the saved game or a formal upper bound on its savings. A positive
result still requires installed-ABI proof, preserved scope semantics and
broader fallback tests before considering a runtime beta.
