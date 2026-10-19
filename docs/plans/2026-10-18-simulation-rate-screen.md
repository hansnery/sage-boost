# Simulation Rate Screen Implementation Plan

> **For agentic workers:** execute inline under the existing development
> authorization. No subagent dispatch or launcher publication is requested.

**Goal:** qualify the native rate setter as a possible building block for
the accepted simulation-timing beta, without changing the installed game.

**Architecture:** Python verifies a complete wildcard-matched body and emits
a metadata-only manifest. A 32-bit C fixture runs that body against owned
storage and checks all ten outputs plus ABI/write boundaries. Python keeps
positive/negative results and source/input hashes together.

**Tech Stack:** existing pefile/Capstone, Python 3.13 and WinLibs i686 GCC.

## Global constraints

Follow `../specs/2026-10-18-simulation-beta-design.md`. No game launch,
runtime mutation, FP control-word changes, shipped EA bytes or FPS claims.
Reference checkout is read only; the test's numeric research identity is
never an installed hook address. Launcher work stays deferred.

## Task 1: executable native rate check

Files: create `tools/test_native_rates.py` and
`perf/tests/test_native_rates.c`; add an optional `test-native-rates` target
to `perf/Makefile` and document it in `tools/README.md`.

Interfaces: the script accepts image, harness and output-report paths. Its
manifest has a function RVA/size/hash, followed by absolute-operand offsets
and owned-storage indices. The fixture reads the local body, verifies the
hash and relocates only the validated operands before marking code RX.

- [x] Validate the full 191-byte reference body and its unique counterpart
  in each target. Reject calls, external branches, FP control instructions,
  unclassified data operands and inconsistent output storage.
- [x] Independently check expected rates and their reciprocal/metric time
  scales for 5/30, 10/60, 5/60 and clamp/ratio cases. Guard the output block,
  call stack and callee-saved registers; verify computed-baseline repetition
  and separately compare it with the original startup words.
- [x] Refuse changed instructions, a wrong conversion constant and aliased
  output addresses. Reject deliberate stale duration, client-rate and
  client-step outputs in separate fixture processes.
- [x] Build with warnings as errors and run both local images through the
  optional make target. Preserve all process results and input/source hashes.

## Task 2: record the decision and its limits

Files: update `AGENTS.md`, `docs/STATUS.md`, `docs/BACKLOG.md` and
`docs/PERFORMANCE-INVESTIGATION.md`; add the native-rate JSON report under
`docs/benchmarks/`. Keep the accepted scope in the linked specification.

- [x] Record the actual setter result and the independent duration-rounding
  example; distinguish native execution from source-only lifecycle findings.
- [x] Leave runtime integration, full timing compensation, matching-peer
  sync and gameplay performance explicitly pending. Defer launcher work.
- [x] Verify source/report hashes, local links, installed binary/INI hashes
  and `git diff --check`; stage explicit paths, commit and push development.

Result: both target bodies support 10/60, but calling the setter at the old
5/30 rates changes two startup scalar words by one ULP. Repeated computed
values match; exact rollback requires saved original values. The arithmetic
model predicts ceiling-boundary changes, to be checked with the native
integer parser next. See `../benchmarks/2026-10-18-native-rates.json`.
