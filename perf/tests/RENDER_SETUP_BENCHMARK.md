# Offline render setup screens

These executables are developer tools, never linked into the ASI. They do
not launch the game. Local executable/effect bytes stay in memory; only
measurements, identities and hashes are saved. Results are synthetic cost
screens, not saved-game replays or FPS forecasts.

## Effect runtime and state saving

Build `mingw32-make build/bench_effect.exe` in `perf/`, then run:

```sh
./build/bench_effect.exe C:/Jogos/AgeoftheRing/rotwk
mkdir -p build/effect_dxvk
cp build/bench_effect.exe C:/Jogos/AgeoftheRing/rotwk/d3d9.dll build/effect_dxvk/
(cd build/effect_dxvk && DXVK_LOG_LEVEL=none ./bench_effect.exe C:/Jogos/AgeoftheRing/rotwk)
```

The argument is the local folder containing `Shaders.big` and
`d3dx9_27.dll`; the later `d3dx9_43.dll` must also be installed. A hidden
64-pixel D3D9 hardware device is used; there is no game or Present.

For every compiled effect, compare technique and parameter metadata, then
record commands through a custom effect state manager with two synthetic
parameter sets. Normalize shader identities to bytecode hashes, querying
the exact GetFunction length first. Compare all commands exactly. The
recorder itself disables automatic state restoration, so this check does
not prove equivalence of default-manager restoration or actual pixels.
It also does not cover texture contents, parameter blocks or arbitrary
in-game parameters. Non-object parameters are all changed, including
matrix/bone/particle inputs; these are not necessarily valid scene values.

Exit 0 means both recordings match for every checked technique; 2 means
differences were found, with counts/hashes and a classification; 1 means
the screen could not complete. Do not treat exit 2 as a compatibility pass
or silently bypass it in a release check.

For `defaultw3d.fxo` Default and _CreateShadowMap, remove the recorder and
time actual D3DX/device calls: SetValue for each used non-object parameter,
Begin, BeginPass, CommitChanges, EndPass and End. There is one empty commit
per pass, no mesh-specific mid-pass update, geometry or draw. Compare:

1. D3DX27 with flags 6, as used by the game.
2. D3DX43 with flags 6.
3. D3DX27 with flags 7: an intentionally unrestored cost-isolation control.

The third mode is never an installable optimization. Two warmups precede
15 paired samples, each averaging 12 workloads of 668 groups. Mode order
rotates between samples. Event-query fences bracket each sample outside
the timed region. Report median paired differences. Lit and shadow are
alternative workloads at the full captured group count; do not add them.
These timings do not include the game's parameter-binding callbacks.

## Nested effect-member usage

The optional `--members-only` second argument to `bench_effect.exe` instead
audits nested `IsParameterUsed` queries and runs an independently authored
selected/spare-member command control. It records counts and hashed names
for all local techniques on both runtimes, without timings or pixel draws.
Run both the system backend and the DXVK copy above with that argument.
See `docs/specs/2026-10-18-effect-member-audit.md`: a false leaf query does
not prove that the leaf is unused. The executable now refuses a running
game or WorldBuilder in either mode.

## Ambient-light sampling

Build and run `mingw32-make bench-ambient`, or:

```sh
mingw32-make build/bench_ambient.exe
python ../tools/bench_ambient.py C:/Jogos/AgeoftheRing/rotwk/game.dat
python ../tools/bench_ambient.py C:/Jogos/AgeoftheRing/rotwk/game820.dat
```

The Python launcher reads the 1.06 reference and target, finds exactly one
wildcarded prefix, then compares the complete 507-byte body after masking
only absolute data/import operands. It checks constants, internal branch
targets, the border virtual call and the two MSVCR71 floor imports. Unknown
bodies or imports fail. It sends only offsets and a checksum to the native
fixture. Use the Python launcher rather than invoking the native helper
with hand-written addresses. No production signature table is changed.

The native fixture maps the local image privately, verifies the checksum,
relocates only those inspected operands, supplies a zero border callback
and the user's CRT floor import, then makes only that function executable.
No game initialization, render loop, or on-disk image patch is performed.

Checks cover a uniform white map, clamped coordinates and failed samples
leaving output unchanged for absent maps and empty extents. There is no
replacement sampler or cache to validate. Each workload makes 1,290 calls
with positions repeated across modeled passes, approximating the capture's
object-call count. Two warmups precede 15 samples of 3,000 workloads each.
Measure 2048-square and 256-square synthetic maps and the no-map path.

This is a warm working-set microbenchmark under the fixture's FP context.
It does not establish whether the user's saved map has ambient lighting,
how often a correct cache would hit, or its invalidation overhead. Even the
whole measured sampler cost is tiny; a real cache could save less.
