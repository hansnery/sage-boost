# Larger unit batches through palette textures

Hans accepts near-60 as the milestone and targets 120 FPS. Current constant
palettes fit fifteen six-bone units or four twenty-bone units per draw in
the existing fixture. The larger-palette workload already showed increased
cost, but did not test a different upload mechanism.

## Choice and scope

Screen a full-precision vertex texture for fresh quaternion/translation
palettes. A group of 25 units can then use one instanced draw instead of
splitting at the vertex-constant limit. Alternatives are retaining material
setup (the stronger three-extra screen regressed on DXVK) or changing engine
timing further (does not remove this measured rendering work). Neither
alternative establishes cheaper larger-palette drawing.

This is a standalone offline feasibility screen, not a runtime hook. Reuse
the project's own instanced HLSL, compiled for vs_3_0 with palette reads
replaced by point-sampled, full-precision texture reads. Read local game
effects only to identify the existing shader roles; save no EA bytecode.

Microsoft documents vertex textures and format capability checks in
[Vertex Textures in vs_3_0](https://learn.microsoft.com/en-us/windows/win32/direct3d9/vertex-textures-in-vs-3-0).
The executable must check actual device caps, format, texture creation and
draw results rather than infer support from that documentation.

## Fair comparison and controls

Both paths directly draw the same 288 synthetic groups of 25 units using
the same local effect Begin/End, poses, geometry, instanced declaration,
instance-index stream and full enclosing state restoration. Stage every
fresh palette on the CPU for both. Constants upload per batch; the texture
path uploads 288 rows of one 1024-by-512 A32B32G32R32F image per frame and selects each
group's row with one constant. Charge staging, upload, rendering and final
GPU completion. Resource creation and shader compilation stay outside timing.

Compare six- and twenty-bone lit/shadow cases on native D3D9 and DXVK.
Each shadow frame clears and redraws the map. Compare multiple moving poses
within existing pixel/depth tolerances and require stale palette textures
to fail. Check draw/mesh counts and restored state, including the vertex
texture and sampler state. Keep the process guard enabled throughout.

The direct constant path is an isolated upload/batching control, not an
end-to-end installed beta comparison. Native FX loops, callback dispatch,
visibility, multipass materials and the scheduler are outside this screen.
Even a gain requires those integration checks and device-loss/resource
lifetime/failure proofs before runtime promotion. Do not convert fixture
milliseconds into gameplay FPS or request another game test from this alone.

The current ASI, INI, public releases, logic, camera and reference repository
remain unchanged. No game or WorldBuilder launch is authorized.

## Measured outcome

All eight final cases pass: six/twenty bones, lit/shadow, native/DXVK. Each
compares three moving phases, with zero pixels over the existing tolerance
for fresh palettes. Every stale-palette control exceeds 100 bad texels.
Twenty-bone drawing drops from 2,016 to 288 calls per frame (7,200 meshes
on both paths); six-bone drawing drops from 576 to 288. Both hardware paths
accept full-precision dynamic vertex textures and the generated shaders.

Median paired completed-frame savings, milliseconds; positive is faster:

| Backend | Six bones, lit | Six bones, shadow | Twenty bones, lit | Twenty bones, shadow |
|---|---:|---:|---:|---:|
| Native D3D9 | 0.463 | 0.087 | 0.959 | 1.004 |
| DXVK | -1.172 | -0.908 | -1.262 | -0.794 |

Each value pools 21 AB/BA samples, eight fenced frames per path/sample after
four warmups. The native results have substantial variation; twenty-bone
shadow's p10 is -0.489 ms. All DXVK medians regress. These are isolated
per-frame workload costs, not FPS or a production-plugin A/B comparison.
Lit/shadow savings cannot be summed.

**Reject this implementation for the installed DXVK path.** The draw-count
reduction is real but does not outweigh staging/upload/sampling costs in
this screen. Native-only gains are not enough to justify changing the whole
game backend or installing this renderer. A different upload mechanism
needs new evidence; do not promote this texture version just for its counts.

The first run stopped at DXVK's vertex-sampler restoration assertion after
completing the four native cases. It is excluded from these timings. Explicit
restoration of the candidate-owned texture and six sampler states fixed
the failure; that additional work is charged only to the texture path.
The unchanged checks then pass for every frame. This observes the particular
fixture/backend state-block behavior, not a general diagnosis of DXVK.

Checks cover mesh/draw counts, method references, shader/declaration,
stream-0 bindings, both stream frequencies, vertex texture identity and the
six touched vertex sampler values. Full native engine state-cache, arbitrary
materials, failures and resource/reset ownership remain unqualified.

The [report](../benchmarks/2026-10-19-palette-texture.json) records hashes,
controls, distributions and the excluded failure. All
[168 final samples](../benchmarks/2026-10-19-palette-texture.csv) are retained.
The parser rejects both a truncated final matrix and the real failed log.
Initial compile diagnostics required a DWORD pointer cast; no tolerances
or correctness checks were removed. No production or installed file changed.
