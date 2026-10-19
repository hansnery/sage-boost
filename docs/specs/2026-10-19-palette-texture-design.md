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
path uploads one 1024-by-288 A32B32G32R32F image per frame and selects each
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
