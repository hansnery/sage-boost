# Native animation decode reuse screen

The exact decoder cache is **not promoted**. Two serial runs save only
0.154-0.367 ms per synthetic 24,576-call workload in the most favorable
warm cases, and regress by about 0.22-0.27 ms with many different phases.
These are CPU decoder timings, not render frames or measured FPS gains.
The previous 2.68-2.93 ms group/GPU result remains the qualified offline
candidate; no additional gameplay saving is established here.

## Why test this boundary

The captured regular frame spends about 1.70 ms in skeleton updates. That
scope also contains hierarchy composition and sub-object updates; it is
not all channel decoding. Compressed stream animations already keep a
per-skeleton cursor with a frame and two decoded values per channel. A
screen that always starts decoding from frame zero would overstate the
opportunity during steady playback.

The test reads three complete encoding-1 decoder bodies from the user's
local executable into private executable memory: scalar, Vector3 and
quaternion, totaling 905 bytes. It does not compile or persist EA source
or executable bytes. Full wildcard matches, internal branch targets,
terminal `ret 20`, absence of callees/FP-control accesses and a single
shared filter-table dependency are verified against the read-only 1.06
reference. Both local target images pass. The only relocated dependency
points to the fixture's independently generated powers-of-two table.

The fixture generates its own nibble streams, channel headers and values.
It does not use game animation assets or the game's table initializer.
Nothing launches the game or executes its entry point, scene or update
loop. The harness and cache compile with general-register-only code; the
original decoders execute their own scalar SSE arithmetic without any
control-word change.

The cache holds 1,024 direct-mapped entries (60 KiB). Its key contains the
channel identity, starting frame, requested frame and every input-state
word. It copies the native result rather than recomputing floating-point
values. Input state is captured before decoding because the normal cursor
path aliases it with output zero. Channel data is immutable within each
fixture epoch; resetting the cache is charged on every repetition.

## Correctness evidence and limits

Each target image passes 8,909 direct comparisons, including cold and warm
state, backward seeks, end-of-stream bounds, changed state, repeated calls,
aliasing, output guards and an epoch reset after stream mutation. Native
callee-saved registers and stack balance pass. Each image also passes
294,912 comparisons across the complete twelve benchmark workloads, with
identical output bytes and unchanged channel/input/stream hashes.

Six altered-body/table controls are refused per image. Three deliberately
broken caches are independently rejected: omitting input state, omitting
the target frame, and corrupting a cached result. The first control also
demonstrates why channel plus frame alone is not a sufficient exact key.

This does not qualify an installed animation hook. Actual channel encoding
mix, cache-hit distribution, animation lifetimes/invalidation, the native
evaluators/interpolation, other encodings, FP status effects and the rest
of the hierarchy path remain outside the proof. No hit-rate assumption is
made for the saved game. The fixture's decoder invocation count is a
sensitivity workload, not a count extracted from gameplay.

## Final timings

Each workload uses 512 units and 48 channels, alternating scalar, vector
and quaternion. Units share one, eight or 64 distinct phase inputs. Warm
states are first produced by native decoding from the channel's initial
state. The timed call aliases its input with output zero. Requested frame
distance is zero, one or eight for warm cases; the cold case starts at zero
and requests frame 64 plus phase. Cache collisions and reset costs count.

Four warmups precede 21 alternating-order sample pairs, each containing
four repetitions. The two runs are serial; the game/WorldBuilder process
guard passes before, between and after workloads. Times below are medians
per repetition, with savings computed from paired differences.

| Distance / distinct phases | Native ms, run 1 / 2 | Cache ms, run 1 / 2 | Saving ms, run 1 / 2 |
|---|---:|---:|---:|
| Warm 0 / 1 | 0.431 / 0.433 | 0.274 / 0.275 | 0.156 / 0.154 |
| Warm 0 / 8 | 0.427 / 0.431 | 0.452 / 0.450 | -0.024 / -0.018 |
| Warm 0 / 64 | 0.433 / 0.436 | 0.701 / 0.693 | -0.267 / -0.257 |
| Warm 1 / 1 | 0.616 / 0.621 | 0.251 / 0.253 | 0.365 / 0.367 |
| Warm 1 / 8 | 0.616 / 0.616 | 0.448 / 0.450 | 0.168 / 0.166 |
| Warm 1 / 64 | 0.619 / 0.620 | 0.841 / 0.839 | -0.222 / -0.218 |
| Warm 8 / 1 | 1.691 / 1.695 | 0.369 / 0.369 | 1.322 / 1.325 |
| Warm 8 / 8 | 1.706 / 1.715 | 0.933 / 0.935 | 0.773 / 0.780 |
| Warm 8 / 64 | 1.700 / 1.701 | 1.940 / 1.947 | -0.241 / -0.246 |
| Cold 64+ / 1 | 11.568 / 11.564 | 0.275 / 0.275 | 11.292 / 11.287 |
| Cold 64+ / 8 | 12.116 / 12.165 | 4.672 / 4.702 | 7.438 / 7.461 |
| Cold 64+ / 64 | 17.021 / 17.027 | 16.558 / 16.555 | 0.467 / 0.478 |

The large cold-case gains do not establish a useful steady-game result.
Even the whole skeleton scope in the capture is much smaller than those
cold workloads. Promoting this narrow cache requires actual workload
evidence that does not currently exist. Do not double these numbers or add
them to the group/GPU savings as a gameplay prediction.

Preserve [504 timing samples](../benchmarks/2026-10-18-native-decode-screen.csv)
and [run metadata, distributions and hashes](../benchmarks/2026-10-18-native-decode-runs.json).
The earlier exploratory library-copy/nonaliasing timing run is excluded.
The production ASI, installed ASI and installed INI hashes are unchanged.

## Source-binding audit and next action

The full 207-byte `FXShaderParameterBinder::AddBinding` has one wildcarded
match at research RVA 0x153459 in both local images (reference 0x153ACA).
Its effect slot 0xF8 call and subsequent zero-result branch gate insertion
into each technique's source list. It already performs a technique-usage
test, so adding the same general test again is not an optimization.

There is an important qualification: when a parsing parent is present,
that parent is tested instead of the individual binding parameter. This
does not prove every unused structure member or array element is omitted.
Check the actual local effect metadata and D3DX behavior before rejecting
or proposing a narrower member-level filter. The other remaining large
target is shadow scene traversal/setup outside the existing group fast
path. Native source computation remains controlled in the GPU fixture.

Reproduce from `perf/` with the documented i686 toolchain:

```sh
mingw32-make test-native-decode
mingw32-make bench-native-decode
```

No installation or user gameplay test was requested. Last measured
gameplay remains about 30.18 presentations/s; full-rate 60 FPS is open.
