# Native Matrix Sources Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure actual matrix-source computation omitted from the fixed-input material fixture before proposing a runtime cache.

**Architecture:** Verify complete native callback bodies against the existing read-only reference, load only those bodies into private process memory, and redirect their globals to controlled matrices. Invoke the original callbacks through a recording effect interface; use the installed D3DX27 inverse/transpose functions. This isolates computation and dispatch, not D3DX parameter storage or drawing.

**Tech Stack:** Python/Capstone/pefile, WinLibs i686 C, Windows QPC and D3DX27.

## Global Constraints

- Never launch the game or WorldBuilder, change floating-point control, or modify the reference checkout.
- No executable bytes or EA assets in reports or source; numeric relocation manifests only.
- No production edits, version bump, deployment, INI change or repeat gameplay request for this screen.
- Preserve full-rate shadows and existing scheduling margins.
- Execute inline under the user's existing continuation authorization.

### Task 1: Verified native callback fixture

**Files:** Create `tools/bench_native_matrix_sources.py` and `perf/tests/bench_native_matrix_sources.c`; add an explicit offline build target to `perf/Makefile`.

- [x] Verify the matrix, inverse and inverse-transpose callbacks; view, projection and world providers; matrix constructor, array constructor and empty row constructor. Require exact full-body equivalence after masking only classified relocation fields, unique root discovery in both installed image variants, valid branches, consistent data roles and exact constants. Resolve folded helpers through verified callers.
- [x] Reject changed instruction bytes, wrong helper dependencies, mismatched constants and duplicate complete bodies. Test refusal before executing any native code.
- [x] Load the verified bytes from the user's executable in memory. Resolve inverse/transpose through D3DX27, patch controlled globals and switch allocated memory from writable to executable before invocation.
- [x] Capture effect setter kind and all sixteen output floats. Test asymmetric matrices, identity flags, invertible and singular matrices, changed view/projection/world inputs, and both setter variants. For each changed input require a changed output. Confirm an intentionally transposed expectation is rejected, and swapping the native view provider for the world provider fails the output oracle.

Oracle: input storage `M` is transposed by the native provider. Ordinary output is `transpose(M)` through SetMatrixTranspose; inverse output is compared with independent double-precision elimination of that provider matrix. Singular inverse falls back to the provider's transpose. Identity flags affect only the corresponding world/view provider; projection remains independent.

### Task 2: Cost screen and recorded decision

**Files:** Create `docs/benchmarks/2026-10-18-native-matrix-sources.json`; update `docs/STATUS.md`, `docs/BACKLOG.md`, `tools/README.md` and this plan.

- [x] Build with `mingw32-make build/bench_native_matrix_sources.exe` from `perf/` using the documented i686 toolchain.
- [x] Run `python tools/bench_native_matrix_sources.py <game.dat> perf/build/bench_native_matrix_sources.exe <d3dx9_27.dll>` sequentially for AotR and game820. The harness refuses a running game/WorldBuilder.
- [x] Preserve raw repeated measurements and input/build hashes. Report isolated callback cost per 1,222 modeled groups; this is a workload scale, not live callback coverage or an FPS prediction.
- [x] If total isolated computation is small relative to the multi-millisecond deficit, reject a standalone matrix-source cache. Do not substitute this result for remaining light, skeleton, binder or D3DX lifecycle costs.
- [x] Review explicit diff; include this completed screen in a conventional
  beta-branch commit with the repository trailer. Push under existing
  authorization; the command result, not this checklist, confirms success.
