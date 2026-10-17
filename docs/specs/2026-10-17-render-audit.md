# Next render experiment: normalmapped shadow shaders

Read-only audit on 2026-10-17, alongside the 0.9.11-beta.1 implementation.
Uses the user's local Shaders.big through the existing inst_shaders tooling;
no game shader bytecode is saved or redistributed.

## Identified missing shader coverage

The log's two unrecognized hashes both match normalmapped.fxo:

- `CF3571C7`, 1,976 bytes, shader index 0: position input, a three-register
  World matrix, and four preshader output registers 0..3.
- `94092159`, 5,328 bytes, shader index 1: position and blend-index inputs,
  WorldBones occupying 200 registers, and four preshader outputs 200..203.

Both preshaders consume View and Projection. Neither uses the tangent or
binormal inputs that would require a full normal-mapped lighting rewrite.
The observed fallback counts were in the shadow-map pass. The current
generator classifies both as "not a unit family": its normalmapped branch
requires DiffuseColor, DirectionalLight and Fog, which these do not read.
They are candidates for a distinct depth/shadow instancing family, not for
relaxing the existing lit-shader classifier.

The next experiment should map the position/depth computation and matrix
conventions, author a separate rigid/skinned replacement, then compare its
depth output against the originals on native D3D9 and DXVK across multiple
transforms and bone maps. Reuse only verified free constant registers.
Only recognized hash/size pairs may select it, and unsupported layouts
must keep falling back. There is no shader change in beta.1.

## Group batching audit

The current `shadow_split` diagnostic counts contiguous prior groups with
matching model and method. It is an opportunity counter, not a proof that
all device state matches. At a new leader, `capture` reads shader,
declaration, streams and state after the game has set them. `member` flushes
staged draws before allowing a new model to rebind buffers. A cache entry
is safe to replace there; delaying a group across that boundary requires
additional state capture and a proof that draw order is irrelevant.

Prioritize the missing shadow shaders over cross-group merging: they offer
a narrow independently verifiable extension. Do not remove the model/state
checks or reorder scene draws just because the counters are high.

## Skeleton work

The selected current trace averages 0.425 ms of skeleton work inside
9.300 ms of render time. The historical worker design also changes x87 and
MXCSR, which current rules forbid. Continue to measure skeleton share after
render cuts; this evidence does not justify implementing that worker design.
