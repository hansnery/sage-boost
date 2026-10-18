# Typed material writes: insufficient standalone gain

The generic SetValue result does not survive unchanged when the fixture
uses the setter families found in the native source code. The final screen
saves **0.60-0.68 ms** in the modeled lit workload; shadow median deltas are
**-0.07 to +0.11 ms**, with sample spreads crossing zero. Reject standalone
runtime promotion. The installed beta.11 and settings are unchanged, and
the last real saved-game result remains 30.0667 presentation calls/s.

## What executes

`bench_typed_effect.exe` is a developer-only extension of the effect fixture.
It reads the local default unit effect and runs D3DX27 on native D3D9 and
DXVK. Exact descriptors select SetInt, SetFloat, SetVector (float2/3/4) or
SetMatrixTranspose (float4x4). Unsupported numeric fields retain SetValue;
twenty-bone palettes retain the raw upload and change every group.

The filter calls the matching getter, compares the represented bytes, and
skips only an identical supported write outside parameter-block recording.
For shorter vectors only the represented components are compared. Failed
getters and unsupported fields forward. No persistent value cache exists.
Descriptor qualification is outside timing and has no production lifetime
or invalidation implementation. Recording state is supplied by the fixture.

| Technique | Generic forwards | Int | Float | Vector | Matrix transpose |
|---|---:|---:|---:|---:|---:|
| Default | 2 | 3 | 6 | 13 | 3 |
| Shadow | 1 | 1 | 2 | 0 | 2 |

Each also has one changing raw palette. These are metadata-derived fields,
not an execution of the native source callbacks or their matrix/light/fog
computations. This screen answers whether continued integration effort is
justified; it does not complete the earlier native-source coverage gap.

## Controls

- Thirty typed fields per backend round-trip nonzero scalar/vector values
  and asymmetric matrices. Independently changed values must be recovered
  through the filter. This avoids a zero/identity-only transpose check.
- Three changed poses per technique compare all vertex/pixel float constants
  and shader hashes after poisoning device constants and independently
  changing the joint-count parameter. Full and filtered writes agree; an
  unconditional static skip differs in all three cases.
- Full writes and the recording bypass produce usable parameter blocks.
  The unguarded equality filter omits the identical value from recording;
  applying its empty block returns `D3DERR_INVALIDCALL` (`8876086C`). An early
  version of the test incorrectly expected every block to apply successfully;
  the final negative control requires this divergent result.
- Four invalid-handle setter/getter pairs preserve the original failed
  HRESULT per technique. This is not a proof of every possible API failure.
- An authored float parameter initialized to 1.5 reads as 1 through GetInt.
  SetInt(1) changes it to 1.0. Getter equality alone therefore cannot justify
  skipping a setter that performs a type conversion. The exact-type gate
  correctly classifies this parameter as float, not int.

No meshes, Draw/Present, rendered pixels, shared effect pools, device loss or
effect destruction/recreation are exercised. Production promotion would
still require those boundaries, actual callback integration, recording-state
proof and a complete native/GPU performance check.

## Measurement

Each sample contains eight workloads of 850 groups, with changing poses and
full Begin/BeginPass/CommitChanges/EndPass/End cycles. Four warmups precede
21 samples, alternating full/filter order. GPU fences bracket each sample;
CPU submission and completed duration are recorded separately. Runs are
serial and refuse to run beside the game. Static fields repeat favorably.

| Backend | Lit CPU saved | Lit completed saved | Shadow CPU saved | Shadow completed saved |
|---|---:|---:|---:|---:|
| Native D3D9 | 0.677062 ms | 0.678187 ms | -0.069787 ms | -0.069562 ms |
| DXVK | 0.603687 ms | 0.601500 ms | 0.109250 ms | 0.088575 ms |

Lit and shadow each model 850 groups; do not add their deltas or extrapolate
FPS. Native lit CPU p10/p90 savings span 0.195-1.393 ms; DXVK spans
0.304-0.889 ms. The shadow spread crosses zero on both backends. Even this
favorable repetition pattern falls below the standalone promotion gate.

Preserve [84 timing samples](../benchmarks/2026-10-18-typed-source-writes.csv)
and [controls, source/input hashes and limits](../benchmarks/2026-10-18-typed-source-writes.json).
The existing source/member audit modes pass again on both backends after
adding the alternate main-entry macro. No full plugin-suite rerun or
installation is needed for this separate test executable.

Reproduce from `perf/` with the documented i686 toolchain:

```sh
mingw32-make build/bench_typed_effect.exe
./build/bench_typed_effect.exe C:/Jogos/AgeoftheRing/rotwk
```

For DXVK, copy only this test executable into a separate build directory
beside the user's existing DXVK `d3d9.dll`; run the same arguments serially.
No game files or installed plugin files are replaced.
