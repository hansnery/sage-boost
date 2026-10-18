# Including the first mesh in an instanced batch

The combined offline prototype saves about **2.94-3.12 ms per modeled
regular/extra pair on DXVK** in two repeats. Only eliminating the first
mesh's separate draw saves 0.31-0.46 ms. No runtime source, installed
binary, INI, stable branch or reference repository changed.

This is near the approximately 3 ms research threshold and warrants a
native rendering-contract audit. It is **not qualified for installation**
and does not establish the roughly 8 ms combined budget reduction sought
for full-rate 60 FPS. Last qualifying gameplay remains about 30.18
presentation calls/s.

## Change and measurement

The production instancer lets the first mesh draw normally and observes
its DIP count before accepting the group at the next member. The new
test-only adapter instead stages the leader at the captured DIP and returns
success without issuing its ordinary draw. Its modeled callee is known to
issue exactly one DIP and perform no subsequent device-state mutation.
A second draw, failed capture, verification mode or failed stage aborts the
fixture. That abort is **not** a safe production fallback after deferral.

Each of 288 groups has 25 distinct synthetic meshes with six bones. Adding
the leader still fits in two instanced draws, reducing DIPs per frame from
864 to 576. The final combined mode also stages the next 24 members with
aggregate scope accounting immediately after the modeled leader returns.
The earlier exploratory mode waits for the second member.

Both paths retain the real local D3DX27 effect lifecycle, source setters,
material changes and advancing Time. Palettes are fresh; every shadow map
is cleared and redrawn. No retained material cache or state filter is used.
The adapter reuses production instancing and hook code, compiles with the
production general-register flags, and is never linked into the ASI.

Four warmups precede 21 AB/BA samples per case, each averaging eight fenced
regular/extra pairs. Performance runs were serial with the game/launcher/
WorldBuilder guard passing. Completion includes the final GPU query wait;
it is not a GPU timestamp or presentation measurement.

| Mode | Run | Backend | Lit completion saving | Shadow completion saving |
|---|---:|---|---:|---:|
| Leader only | 1 | Native D3D9 | 0.191 ms | -0.354 ms |
| Leader only | 1 | DXVK | 0.314 ms | 0.461 ms |
| Leader + bulk after first | 1 | Native D3D9 | 2.830 ms | 2.792 ms |
| Leader + bulk after first | 2 | Native D3D9 | 2.894 ms | 2.654 ms |
| Leader + bulk after first | 1 | DXVK | 2.949 ms | 3.116 ms |
| Leader + bulk after first | 2 | DXVK | 2.942 ms | 2.943 ms |

These are medians of paired savings. DXVK combined p10-p90 intervals are
2.453-3.176 / 2.872-3.267 ms for lit/shadow in run 1 and
2.500-3.373 / 2.824-3.186 ms in run 2. Native leader-only results are noisy
and include regressions. Lit and shadow are alternative full workloads;
do not add their savings or add these combined results to prior bulk gains.

[252 final samples](../benchmarks/2026-10-18-leader-batch.csv),
[168 exploratory samples](../benchmarks/2026-10-18-leader-batch-exploratory.csv)
and [source/executable/log metadata](../benchmarks/2026-10-18-leader-batch-runs.json)
are preserved separately. Earlier adapter builds are not pooled with the
final build. Later legacy-fixture regression timings overlapped a native
correctness test, are not performance-qualified, and are excluded.

## Correctness and native boundary

Both regular and extra images match: lit pixels are exact, and shadow
depths stay within the existing tolerance. Leaders have an offset position
so repeated source poses cannot hide an omitted mesh. Deliberately omitting
them changes 2,768 lit or 2,516 shadow pixels across two frames. The ordinary
fresh-pose control also passes.

Checks cover suppressed-leader counts in both frames, regular instanced
draw/mesh counts, logical marker counts, method reference balance, shader/
declaration/stream state and VS/PS constant restoration at frame boundaries.
Material Begin/End runs normally. The old bulk-only fixture passes all four
native/DXVK cases, and the full plugin suite passes.

The [native group fixture](2026-10-17-native-group-contract.md) now tests
batching after one or two original members: **10,752 cases per local
executable**, plus eleven verifier refusal and four broken-bridge controls.
The earlier boundary preserves logical draw order, normal cleanup, method
ownership, callback fallback and ABI. No new native body or installation
signature is introduced.

That native test still replaces mesh rendering with a controlled logical
draw. It does **not** prove deferring the native leader DIP. The GPU test
uses a modeled material callee. These complementary tests do not yet prove
the complete engine/GPU integration.

## Next decision

Audit the complete target MeshClass::Render_With_FX_Material and its direct
draw path. The read-only reference shows dynamic method update, geometry
Begin, Draw_Triangles, geometry End and skeleton-context cleanup. One
explicit Draw_Triangles call does not exclude draws in earlier callbacks
or later state changes.

Before suppressing a native draw, identify the intended final draw, retain
the original path for unsupported/verification cases, and prove post-draw
state/cleanup, group splits, zero draws, early exits and failures. Then
measure native loop/GPU integration including guards. Do not begin broad
cache integration based on this result. No gameplay test is requested yet.

Reproduce from `perf/` with the documented i686 toolchain:

```sh
mingw32-make bench-leader
mingw32-make bench-leader LEADER_PROTOCOL=leader-bulk-first
mingw32-make test-native-group
```

The optional `leader-bulk` protocol keeps the second member's ordinary
dispatch to reproduce the earlier exploratory boundary.
