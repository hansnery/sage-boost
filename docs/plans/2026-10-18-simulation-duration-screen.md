# Native Duration Screen Implementation Plan

> Execute inline under Hans's pre-approved simulation-beta plan. This is
> offline correctness work; no launcher change or installation is included.

**Goal:** execute the native duration conversion bodies and qualify a
legacy-rounding policy before any simulation-rate runtime integration.

**Architecture:** locate and validate complete native float/uint/ushort/byte
parser bodies and the already-qualified rate setter in each local image.
Run private copies with owned data and explicit fixture dependencies. Numeric
token scanning, mathematical ceiling, integral conversion and exception
reporting are fixture boundaries; no game imports, entry point or assets run.
Do not execute a CRT conversion helper that changes the FP control word.

**Files:** create `tools/test_native_durations.py` and
`perf/tests/test_native_durations.c`; add `test-native-durations` to
`perf/Makefile`, then record results in the simulation spec, status, backlog
and `docs/benchmarks/2026-10-18-native-durations.json`.

- [x] Match complete bodies, validate operand/branch/dependency identities,
  verify shared duration storage against the native rate setter, and refuse
  altered instructions/data dependencies before executable memory is used.
- [x] Run startup, recomputed 5 Hz, recomputed 10 Hz and exactly doubled
  startup scales. Keep a separately checked mathematical ceiling shim and
  do not touch x87/MXCSR controls. Record all fixture substitutions.
- [x] Compare sampled native outputs with Python's independent binary64
  product/ceiling oracle, including unsigned inputs above INT_MAX, exact
  duration boundaries and byte/word storage limits. Check float outputs,
  write bounds, normal-return ABI and overflow-reporting paths.
- [x] Qualify doubling the original rounded tick count for representable
  integer durations; reject overflow without partial writes. Check that
  doubling the scale before rounding fails the legacy-duration oracle.
- [x] Build with warnings as errors, run both local images and deliberate
  wrong-output controls, and preserve hashes and process output. These are
  timing-correctness results, never FPS or multiplayer evidence.
- [x] Update linked documentation, verify installed binary/INI hashes and
  whitespace, stage explicit paths, commit and push development.

The byte parser's actual native overflow branch reports an error, while
the ushort parser narrows the result. The candidate must not silently wrap
a doubled duration; unsupported widths remain an integration blocker to
resolve through actual field coverage. No blanket conversion is enabled.
