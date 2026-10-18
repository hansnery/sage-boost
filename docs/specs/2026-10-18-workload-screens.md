# Queue cleanup and larger bone palettes

Neither screen establishes a new improvement to install. The last saved-game
measurement remains **30.0667 presentation calls/s**, far below the target.
Production source, installed beta.11, the INI and stable branches are unchanged.
The full plugin `make test` suite passes after the shared fixture changes.

## Queue cleanup: reject runtime promotion

The new offline fixture compares the verified native full-range erase with
an independently written loop. It releases a record directly only if its
mesh has more than one reference, every non-null method has more than six,
and no method aliases the mesh. Otherwise it calls the native record
destructor. Methods are released in reverse slot order before the mesh;
queue finish changes only after all records, and record bytes stay intact.

All 192 normal-return cases pass against both executable families, with an
additional AotR repeat. Sizes are 0/1/2/25/256/7,680, with 0/1/3/6 method
holders, unique/repeated/null/mesh-alias holders, and zero/two external
references. Checks compare every final reference count, full record bytes,
queue bounds and the exact deletion sequence. Callback observations include
the old queue finish, the first eight method counts and a zero count on
the deleted object. The observer does not free objects; exception and
reentrancy behavior is not qualified. Omitting a mesh release deliberately
fails the oracle.

Four warmups precede 21 alternating paired samples per method count. Each
sample sums eight clears of 7,680 records, with ownership restored outside
the measured interval. Final median savings range from **0.0836 to 0.0926
ms per clear** across three runs. This is too small to justify a new runtime
hook or integration work toward the multi-millisecond deficit. Retain the
screen as evidence, not as an installable feature.

[252 raw samples](../benchmarks/2026-10-18-queue-cleanup.csv) and
[controls, hashes and distributions](../benchmarks/2026-10-18-queue-cleanup.json)
refer to the final exact-event-array oracle. Earlier exploratory runs used
a callback fingerprint and are not mixed into these results.

## Twenty-bone workload: existing batching is more expensive

The live verification log contains twenty-bone models. The previous GPU
fixture used six bones. A separate `bench_runtime_bulk_20_gpu.exe` now uses
twenty bones and enough tree storage, with vertex indices covering all
twenty. Default six-bone geometry is unchanged. Expected batch counts come
from the selected shader's register capacity instead of a fixed two draws.

For the selected variants, six bones fit fifteen units per instanced draw;
twenty bones fit four. With 25 units per group, the ordinary leader plus
followers needs seven draws at twenty bones, and batching all 25 also needs
seven. Leader inclusion therefore removes **zero** draws, versus 288 per
frame across the six-bone fixture's 288 groups. It still avoids per-member
native dispatch. A lower draw count must not be assumed for every palette.

Both executable families pass twenty-bone native D3D9 and DXVK controls in
lit and fresh-shadow workloads. The six-bone DXVK lit/shadow regression
checks also pass: ten distinct cases, each with 136 native group comparison
pairs, whole-queue/order/state/ownership checks and three deliberately
corrupted queue controls. The larger fault/recovery matrix was not rerun
with twenty bones; this is workload coverage, not blanket qualification.

Six serial AotR/DXVK timing runs use shuffled queues, four warmups, 21 paired
rounds and eight fenced regular/extra pairs per round. The comparison is
native insertion/sort plus ordinary instancing against the **already
installed** insertion/sort/bulk optimizations, not beta.11 against a new
candidate. Median costs and median paired differences are separate
statistics; differences of medians need not equal the paired median.

| Palette / workload | Optimized pair CPU ms | Optimized pair completed ms | Existing path's paired CPU saving ms |
|---|---:|---:|---:|
| 6 bones, lit | 9.1764 | 9.9044 | 9.3916 |
| 20 bones, lit, first | 16.0937 | 17.0398 | 7.7425 |
| 20 bones, lit, repeat | 16.1810 | 16.8393 | 7.8764 |
| 6 bones, shadow | 7.1953 | 7.7758 | 9.3881 |
| 20 bones, shadow, first | 15.4466 | 15.9390 | 7.4608 |
| 20 bones, shadow, repeat | 15.2445 | 15.9418 | 7.9890 |

The palette-size sensitivity is substantial. Geometry, materials, source
computation and resource lifetimes remain controlled, so these values do
not predict saved-game FPS. CPU/GPU drift is still possible despite serial
execution and game-process guards. Lit and shadow costs must not be added
to overlapping gameplay scopes.

[126 raw samples](../benchmarks/2026-10-18-palette-workload.csv) and
[control matrix, hashes and distributions](../benchmarks/2026-10-18-palette-workload.json)
preserve the final evidence. The tests read local native bodies and assets
into private memory; no native body, shader or game asset is saved here.

## Next target

Native material-source computation remains a gap in the fixture, and FX
batch processing remains a large gameplay scope. The read-only reference's
`fxshadernamespacesas.cpp` has matrix multiplication/inversion and palette
source setters. Audit their actual bindings and native executable bodies
before treating their cost as avoidable. Existing source-pruning and
material-cache experiments were rejected; this does not revive them.

The earlier [marker screen](2026-10-17-marker-cpu-screen.md) measured about
0.49 ms between regular Detail=1 and Detail=0 for 7,200 markers. Turning off
detail alone does not explain the current multi-millisecond deficit. Keep
the user's settings and pacing/shadow constraints unchanged.

## Reproduction

From the documented Git Bash toolchain, in `perf/`:

```sh
mingw32-make bench-queue-cleanup
mingw32-make build/bench_runtime_bulk_20_gpu.exe
python ../tools/bench_native_insert_gpu.py --controls-only --self-test \
  C:/Jogos/AgeoftheRing/rotwk/game.dat build/bench_runtime_bulk_20_gpu.exe \
  C:/Jogos/AgeoftheRing/rotwk/Shaders.big \
  C:/Jogos/AgeoftheRing/rotwk/msvcr71.dll lit narrow 10 leader-bulk-first
```

For timings omit `--controls-only --self-test`; for shadow use `shadow` in
place of `lit`. Repeat with `game820.dat` for the other image. DXVK uses a
separate directory containing the benchmark executable and the user's
`d3d9.dll`, with `DXVK_LOG_LEVEL=none`. Run benchmarks serially with the game
closed. No game launch or installation is part of these commands.
