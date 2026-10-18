# Material-chain bulk screen implementation plan

> Execute inline under the existing user authorization, with sequential GPU
> checks and benchmarks. Production and the installed build remain unchanged
> until a substantive candidate has complete qualification.

**Goal:** Measure whether the single-method dispatch restriction leaves
significant native-loop work in otherwise supported multi-method groups.

**Architecture:** Generate an offline copy of our bulk dispatcher with one
controllable method-count gate. Run the installed gate and relaxed gate in
the same executable with identical native group/mesh/FX bodies and GPU state.
Retain the native comparison of every method pointer and all other guards.

**Tech stack:** Existing i686 GCC native/GPU fixture, D3D9/DXVK, Python
read-only executable qualification and metadata manifests.

## Constraints

- Do not launch the game or WorldBuilder, modify the reference checkout,
  change camera/logic/network/input, or lower shadow cadence or quality.
- No EA bytes or assets persist in the repository. The generated dispatcher
  is our source only; native bodies remain private in the test process.
- This screen controls child-method behavior and source computation. It
  cannot qualify arbitrary native chained subclasses or prove live coverage.
- Existing capture's shadow-state summary has 6,119 original splits and
  zero remaining splits. Do not attribute its remaining fragmentation to
  opacity. Method arrays are a separate native grouping condition.

## Task 1: Native chain correctness and cost

Files: create `tools/prepare_chain_screen.py` and
`perf/tests/bench_chain_screen.c`; add optional targets in `perf/Makefile`.

- [x] Generate an ignored test copy of `perf/src/bulk.c`, replacing only
  `*(const unsigned *)(first + 4) != 1` with
  `!bench_chain_gate(*(const unsigned *)(first + 4))`. Assert the original
  guard occurs exactly once. The production dispatcher is unchanged.
- [x] Implement the benchmark gate as
  `count == 1 || (chain_allow && count >= 2 && count <= 6)`.
  Both frame paths retain the actual installed group hooks, leader deferral,
  ordinary instancing, material lifecycle and all other dispatch guards.
- [x] Extend the existing fixture through a new translation unit. Use the
  native chain-link/clear walkers, controlled owning SetNext implementations,
  lengths 1..6 and a distinct final method in the next adjacent group.
  Check exact references, empty links after return, unchanged record bytes,
  normal/extra image equality, native boundaries, scope counts and ABI.
- [x] Add broken controls for erasing a secondary-method boundary, leaking
  a child link and denying the candidate bulk path. Each must fail an oracle.
- [x] Build optional six- and twenty-bone executables. Run both images and
  D3D9/DXVK with lit/fresh-shadow output, serially. Run GPU error/fallback
  qualification only if subsequent integration is justified; this screen
  alone does not qualify a production hook.
- [x] Measure installed and candidate gates with alternating AB/BA samples
  inside one executable. Preserve regular, extra and fenced completion
  samples and hashes. Use two- and six-method cases; avoid cross-run delta
  claims. Report fixture limits and whether benefit justifies further work.

Reproduce with the documented toolchain, `make build/bench_chain_screen.exe`
or `make build/bench_chain_screen_20.exe`, then
`tools/bench_native_fx_gpu.py IMAGE EXE SHADERS CRT shadow narrow 10 leader-bulk-first 6`.
Use the corresponding lit argument and installed executable family for
each case; place DXVK beside a copied benchmark in an ignored directory.

## Task 2: Evidence and decision

- [x] Correct the previous opacity lead in STATUS/BACKLOG with exact log
  counters and their coverage limit (summary, not the complete 60-second
  interval). Record all-method native grouping and the one-method bulk gate.
- [x] Save results in a spec and benchmark CSV/JSON. Keep the goal active
  unless the actual saved game satisfies the original FPS/pacing target.
- [x] Commit/push the experiment and findings with explicit paths. Do not
  install or ask for another gameplay test without substantive evidence.

Result: 16 normal-path matrix runs pass and all six deliberate faults are
rejected. Eight paired runs preserve 168 samples, with 2.29-4.75 ms median
completion savings per synthetic regular/extra pair. The generated core
passes the GPR audit. Runtime qualification is warranted and remains open;
production source, beta.11 installation and INI values are unchanged.
