# External Render Thread Screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Test whether the released Accelerator render thread can be qualified offline before a substantive Realms trial.

**Architecture:** Compile the matching upstream standalone harness from the existing external repository's Git objects into a temporary folder and run the verified release DLL through its documented off/on comparison. Keep every foreign implementation outside sage-boost; persist hashes, commands and measurements only.

**Tech Stack:** x86 MSVC, Python standard library, PE metadata, installed DXVK/D3DX27.

## Global Constraints

- No game/WorldBuilder launch or live injection. No production/install/settings changes.
- Never alter the external checkout or import its source into sage-boost.
- Use the official 2.0 build 73 DLL, not the fork's locally built DLL.
- Require active threading and equal frame hashes; no saved-game FPS claim.
- Execute inline under the user's continuing development authorization.

### Task 1: Isolated build and provenance

- [x] Check the external worktree status and record HEAD/source hashes.
- [x] Create a new TEMP directory; compile the matching upstream
  `src/rt_harness.cpp` using `/O2 /MT /W3`, with `/Fe` and `/Fo` directed
  there. The older fork harness was built but never run; the release-matched
  source is extracted from existing Git objects without checkout changes.
- [x] Extract only the official DLL from the verified archive as the
  harness's documented `bfme2_accel.new.dll`. Copy local DXVK/D3DX27 and hash
  all inputs and outputs. A compiler failure stops execution of that build.

### Task 2: Controlled comparison and decision

- [x] Run `rt_harness off 600 off.txt nofpu` and
  `rt_harness on 600 on.txt nofpu`
  sequentially, in the temporary folder, with a bounded subprocess timeout.
  Check game/launcher absence before each run and inspect terminal state.
- [x] Inspect the actual log/CSV format; compare every frame hash and
  require reported active thread use. Keep fault/error lines in the report.
- [x] Classify the passed harness as a correctness screen: it toggles the
  worker every 50 frames. Its separate enqueue microbench also does not
  provide full-frame timing. Repeat the independent effect workload below
  instead of treating whole-harness elapsed time as threaded throughput.
- [ ] Persist a compact report under `docs/benchmarks/`, update STATUS and
  BACKLOG, and commit/push the evidence on `codex/perf-beta`.

### Task 3: Local-effect lifecycle comparison

- [x] Add optional `perf/tests/bench_external_rt.c` and a Makefile target.
  Reuse our archive parsing, effect input generation and fence helpers;
  load only an explicit external DLL and resolve its documented harness ABI.
- [x] Capture unit/shadow effect device-state oracles before hooking. Check
  actual state through saved original vtable entries after a queue/GPU drain;
  require changed-input observations and a deliberate constant-poison failure.
- [x] Alternate threaded/wrapped-off mode over 21 pairs per technique,
  with 1,222 groups and four repetitions. Record both caller and drained
  completion time. Require an active thread, empty queue and equal submitted/
  executed sequence counts after completion.
- [x] Preserve samples/hashes and distinguish this no-draw lifecycle screen
  from GPU image qualification, native integration and saved-game FPS.
