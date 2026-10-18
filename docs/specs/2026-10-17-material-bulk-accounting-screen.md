# Material reuse with aggregate mesh accounting

The combined offline prototype saves **3.42-3.53 ms per modeled pair for
lit rendering, and 3.03-3.06 ms for shadow rendering on DXVK**, across three
guarded repeats. These are separate whole workloads, not additive gains.
This merits native integration research; it is not an installed optimization
or evidence of 60 FPS. The last qualifying gameplay result remains ~30.18/s.

## What changed

The earlier [per-member protocol](2026-10-17-material-queue-clean-results.md)
keeps every synthetic label, native-style buffer copy and timing hook on
cache hits. It saves only about 0.7-1.0 ms. The new test keeps real D3DX27
Begin/End, input preparation, exact cache keys, regular miss/capture costs,
fresh bone palettes and fresh shadow draws. On a cache hit it accounts for
the entire staging/submission loop with one interval and the number of
logical meshes attempted. Native-style event copies are absent on that hit.

The number of logical mesh scopes, per-pass mesh totals, per-name calls and
FX-group marker counts are preserved. Time is measured once for actual
aggregate work; it is **not multiplied by the mesh count**. Average mesh time
therefore represents aggregate work divided by logical count. The boundaries
differ from separate per-member intervals and include the tight loop's
iteration overhead. This must remain documented if implemented at runtime.
The test does not silently change Detail or count unmeasured time as zero.

`timing_bulk_bench.c` includes the production timing implementation and adds
test-only accounting helpers. Production sources, ASI and installed INI
are unchanged. Individual native debug events are not interchangeable with
this accounting: the fixture has no active PIX/debug callback. A runtime
version must verify that both native callback slots are null and fall back
when either is active. Source inspection supports that condition but is
not yet a complete target-code/caller proof.

## Measurements

Each case draws 288 groups of 25 synthetic meshes, with the local game's
actual effect, shaders and D3DX27. A pair contains a regular and extra draw.
Four warmups precede 21 alternating-order samples, each averaging eight
fenced pairs. Timings include cache validation, lifecycle, capture on misses,
aggregate accounting and final GPU completion wait. No scene or Present
runs. The process guard checks before setup and before/after every round.

Completion savings in ms:

| Regular-group churn | Run | Native lit | Native shadow | DXVK lit | DXVK shadow |
|---|---:|---:|---:|---:|---:|
| 10% | 1 | 5.4126 | 5.0084 | 3.4151 | 3.0271 |
| 10% | 2 | 5.4932 | 5.0324 | 3.5300 | 3.0559 |
| 10% | 3 | 5.5098 | 5.1160 | 3.4374 | 3.0321 |
| 0% | control | 5.9192 | 5.4890 | 3.7442 | 3.4956 |
| 100% | control | 2.1951 | 2.3353 | 0.6581 | 1.1048 |

The 10% mode alternates 30 of 288 materials at the start of each pair.
100% invalidates every **regular** group; the following extra draw can hit
again. It is not an all-miss workload for both draws. Extra-only invalidation
is tested independently for correctness. At 10%, DXVK completion p10 is
2.92-3.32 ms lit and 2.48-2.69 ms shadow; variation remains material.

[All 420 samples](../benchmarks/2026-10-17-material-queue-bulk.csv),
[84 per-member regression-control samples](../benchmarks/2026-10-17-material-queue-bulk-control.csv),
and [source/executable/log metadata](../benchmarks/2026-10-17-material-queue-bulk-runs.json)
are preserved. Raw run filenames 2/3/4 correspond to repeat rows 1/2/3.
The first attempt stopped during accounting setup, before timing: it had
not established the main render thread. Initialization now passes through
the normal display hook. That attempt contributes no performance samples.

## Verification and remaining gates

All native/DXVK cases match regular and extra images exactly: 16,668 lit
and 16,644 shadow covered texels. Stale-pose/material controls differ;
zero/partial/full material invalidation and changed extra inputs fall back
and recover correctly. Each case exercises 20,736 real effect End boundaries
with an opposite entry cull state. Existing device-state/reference checks
and the full plugin test suite pass. The original per-member protocol also
passes again with the new test-only object linked.

The accounting preflight covers count 1 and 25 in all three passes and no
pass, regular/extra modes, Detail on/off, duplicate begin/end, zero count,
and full scope stack. Every drawn regular frame still checks exactly 7,200
logical scopes with no stale scope; extras keep their existing semantics.
This does not prove native exceptions or partial GPU-draw failure handling.

Before any beta, prove and implement:

1. Unique target signatures and full contracts for event callbacks and the
   native group/loop boundary, including method ownership and SEH cleanup.
2. Every native source callback and effect lifecycle still runs where
   required; complete value keys include material/technique changes.
3. Cache entries own valid references, have bounded memory, and invalidate
   on resource/device/geometry transitions. Fixture pointers are borrowed.
4. Engine-side binding caches agree with device state after every hit,
   miss, partial failure and fallback. Restoring fixture GPU state alone
   does not establish this.
5. Native debug callbacks and unsupported groups use original events and
   rendering. Preserve diagnostics with explicitly documented boundaries.

Do not add this saving to earlier dispatch/cache screens: they overlap.
The current workload does not model scene/shroud/visibility work, animation,
UI or actual cache hit rates in the user's save. No gameplay test is requested
until the runtime candidate and its significant gain are established.

Reproduce in the documented Git Bash toolchain, in `perf/`:

```sh
mingw32-make bench-material-queue MATERIAL_CHURN=10 MATERIAL_PROTOCOL=lifecycle-bulk-markers
```
