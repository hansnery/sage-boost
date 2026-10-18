# Render setup screening results

Three further candidates were measured offline on 2026-10-17: a newer
D3DX effect runtime, effect state-save overhead, and ambient-lightmap
sampling. None meets the existing ~3 ms screening threshold for a new
performance beta. No effect-runtime redirection, disabled restoration or
ambient cache was added. The game and installed configuration were untouched.

## Effect runtime

The local AotR executable imports D3DXCreateEffect from `d3dx9_27.dll`.
The updated reference's `FXShaderSetup::Begin_Rendering` in
`Code/Libraries/Source/WWVegas/WW3D2/fxshadersetup.cpp` describes the material
parameter block, texture bindings, dynamic binder and Begin(flags=6) path.
`Rva001514B2ForwardAndApply.cpp` describes the six binder ranges and
per-object CommitChanges path. Neither source alone proves a safe import
replacement in the installed executable.

Microsoft documents that Begin normally saves affected device state and
End restores it; flags 6 omit shader and sampler saves, while flags 7 also
omit render-state saving. A custom effect state manager disables automatic
save/restore. These distinctions matter when interpreting the recorder and
the intentionally unrestored timing control:
[Begin](https://learn.microsoft.com/en-us/windows/win32/direct3d9/id3dxeffect--begin),
[D3DXFX flags](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dxfx),
[effect state manager](https://learn.microsoft.com/en-us/windows/win32/direct3d9/id3dxeffectstatemanager).

The benchmark reads local effects, compares metadata and emitted device
commands, then times the default unit material's lit and shadow techniques
with actual D3DX/device calls. There are no geometry draws or Present.
Each alternative workload uses 668 groups, the captured regular-frame scale.
Lit and shadow results must not be added. Full method and reproduction
instructions: [render setup benchmark](../../perf/tests/RENDER_SETUP_BENCHMARK.md).

Two completed runs per backend, each with 15 paired warmed samples:

| Backend / workload | D3DX43 saving, runs 1 / 2 | Omit render-state save, runs 1 / 2 |
|---|---:|---:|
| Native D3D9 / lit | 0.327 / 0.378 ms | 0.108 / 0.118 ms |
| Native D3D9 / shadow | 0.177 / 0.165 ms | 0.070 / 0.051 ms |
| DXVK / lit | 0.987 / 0.341 ms | 0.125 / 0.100 ms |
| DXVK / shadow | 0.136 / 0.201 ms | 0.098 / 0.089 ms |

These are medians of paired differences, not differences of independent
medians. Preserve the large DXVK lit run-to-run variation; it still does
not approach the threshold. [All 120 pairs](../benchmarks/2026-10-17-effect-runtime.csv).
The final runs followed the plugin suite, without concurrently running it.
The native backend was Windows D3D9; the second was the installed DXVK
copied into an ignored benchmark folder. D3DX versions were 9.08.299.0000
(27) and 9.29.952.3111 (43).

All 38 techniques' metadata matched. Of 76 command comparisons (two
synthetic parameter sets each), three differed, identically on both
backends: gpuparticle Default, Default_L and _CreateShadowMap, with the
second input set. Each difference was one nonnegative finite float
constant; its absolute difference was about 1.0613e9. Do not characterize
this as roundoff or claim runtime interchangeability. The supplied particle
values are synthetic, and no rendered particle comparison was made, so
this does not establish a visible regression in the user's actual scene.

An initial exploratory DXVK recording incorrectly hashed unused buffer
capacity. Querying the exact GetFunction size before reading corrected
that harness defect; only corrected recordings/runs are reported here.

The test does not measure the game's dynamic binding callbacks, model its
parameter blocks, change per-object values mid-pass, or exercise its full
state history. Small synthetic savings plus incomplete compatibility
evidence reject a global runtime swap. Omitting state restoration is only
a cost control and is not an eligible optimization.

## Terrain ambient-light sampling

The refreshed reference's `TerrainLogicAmbientColorRva0027DA6A.cpp` samples
a lightmap before the rest of renderOneObject. A cache was a conditional
candidate for repeated positions across passes.

The launcher located the sampling function independently in AotR and
game820 by wildcarded prefix, then verified all 507 bytes against the
inspected 1.06 body after masking only absolute data/import operands.
Constants, internal branches and external call contracts were checked.
Both target bodies had SHA256
`71c2670cdf4894c4974764d756d79cd7573042819d5142efe903b3e69e113b8c`.

Only that isolated function ran against synthetic terrain, with a zero
border callback and the user's MSVCR71 floor import. No executable startup
or game renderer ran. Uniform colour, coordinate clamps, missing maps and
empty extents passed. No copied executable bytes are stored in the repo.

| Local image | 2048-square map | 256-square map | No map |
|---|---:|---:|---:|
| AotR | 0.04689 ms | 0.04494 ms | 0.00212 ms |
| game820 | 0.04663 ms | 0.04612 ms | 0.00213 ms |

Medians for 1,290 calls, approximating regular-frame renderOneObject count.
Two warmups, then 15 samples of 3,000 workloads in each configuration.
[All 90 samples](../benchmarks/2026-10-17-ambient.csv).
The entire measured routine is too small to warrant caching; a real cache
would retain misses and lookup/invalidation costs. This warm microbenchmark
does not establish actual map presence or hit rate in the user's save.

## Remaining constraints and a discovered correctness defect

The extra-frame estimate is still stale, with no busy-scene measurement.
The current regular-draw reference also exceeds the available budget.
Simply lowering the estimate would guess that unmeasured work fits. No
deadline margin, recovery gate, simulation or camera behavior was changed.

During this audit, the shadow interval gate was found to reuse an extra
frame's map even at interval 1. That would fail the user's full-rate shadow
requirement once extra frames ran. The separately tested beta.3 correction
makes regular frames redraw too at interval 1; larger intervals retain
their existing reuse policy. It increases work in the affected case and
is not a performance gain. See [beta.3 notes](../releases/perf-0.9.11-beta.3.md).

No tested significant performance improvement or sustained 60 FPS has been
established. Keep beta.2 installed and do not request a diagnostic-only
gameplay test. Further progress needs a larger shared render-setup change
with state/output proof, or a defensible way to measure the busy extra draw
within the existing game-frame deadline.
