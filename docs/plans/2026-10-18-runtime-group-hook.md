# Runtime group hook implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

Execute inline under the existing authorization; those execution subskills
are unavailable. No additional delegation or approval is needed.

**Goal:** Connect the qualified batching implementation to the native group
loop, with ownership bounded by the group call, and measure that active path.

**Architecture:** `bulk.c` owns the entry wrapper, SEH unwind cleanup and
loop bridge. `timing.c` owns aggregate mesh accounting. `inst.c` owns pending
units and COM references. Startup discovers the full contract before other
hooks, installs the two group patches, then enables the path only if all
prerequisites were installed. The offline combined fixture executes these
same production operations and assembly wrappers.

**Tech Stack:** i686 C/assembly, Win32 SEH, existing native contract verifier,
native D3D9/DXVK fixtures, GCC general-register flags.

## Constraints

- Preserve simulation, input, network, camera, fresh shadow output and FP
  controls. Never launch the game/WorldBuilder or modify the reference repo.
- Generate new signature metadata through `tools/verify_sites.py --perf-bulk
  --write`; no fixed runtime addresses or copied executable bodies.
- Coherent and complete are separate outcomes. An incomplete GPU operation
  must disable optimization and report loss; it is never successful replay.
- No captured stream reference survives group return/unwind. Exception
  handlers continue the native exception search/unwind and never swallow it.
- Do not install until active-path correctness and significant performance
  evidence pass. Actual 60 FPS still requires the user's saved-game capture.

## Implementation and validation

- [x] Move the aggregate functions from `tests/timing_bulk_bench.c` into
  `src/timing.c`, exposing `pf_timing_bulk_begin/end/cancel/abandon` in
  `timing.h`. Keep one measured interval and count accepted records:
  `g_fx.markers += count - 1; g_meshes[pass] += count - 1;`.
  The existing fixture becomes delegates plus its accounting assertions.
- [x] Add `pf_inst_bulk_context(int)` and
  `pf_inst_bulk_finish(int interrupted)` to `instbulk.h`/`inst.c`. Eligibility
  requires a live owned group; leaving clears pending state and releases
  the held stream without replaying after native material cleanup.
- [x] Add `bulk.h`/`bulk.c`: `pf_bulk_bind`, `pf_bulk_enable`,
  `pf_bulk_dispatch`, `pf_bulk_entry`, `pf_bulk_bridge`. The entry preserves
  the thiscall arguments and registers, puts a cleanup record on FS:0,
  calls the native trampoline, then removes its record. Its handler calls
  cleanup only for `ExceptionFlags & (2 | 4)` and returns continue-search.
  The bridge advances `frame[-7]` only by the accepted record count and
  restores flags/registers before either verified continuation.
- [x] Generate the loop signature from the verified group body at +0x14f.
  Add `pf_bulk_install` to install entry and loop sites as one required
  group; activation remains off after any failed patch/prerequisite.
- [x] Add `BulkRender` configuration and startup discovery/installation.
  Bump the internal plugin version and load expectation together.
- [x] Add a combined fixture variant selecting the production wrapper and
  bridge. Check native ABI/FS chain, counts, output, callbacks, unsupported
  groups and ordering with `make test-runtime-bulk-gpu` on both images.
  Test incomplete restoration and native unwind directly; do not fabricate
  successful output after an unrecoverable device failure.
- [x] Run `make test`, inspect hook-object instructions, and measure serial
  regular/extra pairs through the final active production path. Preserve
  hashes and raw samples, document practical limits, commit and push.
- [x] Install a significant validated candidate with backup/hash checks;
  preserve all existing INI values and request the user's saved-game test.
  Code commit `ebe5681` is pushed and installed; STATUS.md records hashes
  and backup. The actual gameplay capture remains pending.
