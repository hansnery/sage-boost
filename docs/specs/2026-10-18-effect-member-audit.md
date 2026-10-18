# Effect member-usage audit

**Reject member-level `IsParameterUsed` as a source-pruning condition.**
Both local D3DX runtimes report a genuinely used structure member as unused
in an independently authored control shader. Changing that member changes
the emitted shader constants; skipping its update would be incorrect.

The previous native AddBinding audit found a selected-technique usage test
that sometimes checks the enclosing parameter instead of the bound leaf.
This audit explains why replacing it with a leaf query is unsafe. It does
not show that every source evaluation is necessary, or rule out a different
optimization with stronger dependency evidence.

## Control and local effects

`perf/tests/bench_effect.c --members-only` creates a small shader using only
`values.selected` from a two-member structure. It compares three complete
emitted-command streams: baseline values, changed selected member, and
changed spare member. Shader identities are normalized to bytecode hashes.

All four combinations of D3DX27/D3DX43 and native D3D9/DXVK report:

| Observation | Result |
|---|---|
| Parent structure reported used | True |
| Selected member reported used | False |
| Spare member reported used | False |
| Changing selected member changes commands | True |
| Changing spare member changes commands | False |

The selected/spare command controls are mandatory assertions. The recorder
captures effect commands rather than rendering pixels, and this mode does
not measure performance. The shader is our own source, not a copied asset.

The audit also recursively enumerates the local compiled effects' arrays
and structures, with bounded recursion and counts. It checks all 38
techniques in both runtimes on both backends. Among annotated, used parent
parameters there are 220 nested-root observations per backend, with 3,082
leaf observations. Every such leaf query returns false. These are repeated
observations across techniques/runtimes, not unique parameter counts, and
must not be interpreted as 3,082 safely removable source bindings.

Only counts, hashed identities and file hashes are retained:
[152 technique observations](../benchmarks/2026-10-18-effect-members-usage.csv),
[440 nested-parent observations](../benchmarks/2026-10-18-effect-members-roots.csv),
and [controls and hashes](../benchmarks/2026-10-18-effect-members-runs.json).
The 32-bit process reports system DLL paths through WOW64; metadata hashes
the actual SysWOW64 runtime/backend files. Local effect bytes stay in RAM.

The default mode also runs again on both backends. It still exits 2 with
the previously documented three runtime differences across 76 command
comparisons. That unchanged diagnostic outcome is not a compatibility pass
for D3DX43; the new member mode does not waive those differences.

## Remaining target

The saved heavy-scene capture's visibility scope averages only about
0.60 ms across passes; render-object pre-work and light-environment scopes
are also small. They overlap broader scopes and must not be added to those
parents. Whole-object reuse would additionally risk the known shroud-frame
writes. The next separate target is the native render-task sort, whose
temporary ownership traffic is screened in the
[sort report](2026-10-18-native-sort-screen.md).

No production, installed binary or INI changes. No FPS improvement is
claimed by this audit. Reproduce with the documented i686 toolchain:

```sh
cd perf
mingw32-make build/bench_effect.exe
./build/bench_effect.exe C:/Jogos/AgeoftheRing/rotwk --members-only
```

The existing DXVK copy/run instructions in
`perf/tests/RENDER_SETUP_BENCHMARK.md` apply with the same extra argument.
The process guard refuses a running game or WorldBuilder.
