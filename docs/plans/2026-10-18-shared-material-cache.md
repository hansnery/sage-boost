# Shared Material Cache Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Determine whether sharing complete retained snapshots across equal fixture material/geometry keys recovers a substantial saving with advancing time and full boundary restoration.

**Architecture:** Add an opt-in offline protocol to the existing retained-material fixture. Use a bounded hash lookup with linear probing and exact byte comparison; full-table collisions recapture. Keep original effects, complete keys including Time/textures, per-hit Begin/End, fresh palettes and the full restoration boundary.

**Tech Stack:** WinLibs i686 C, real local D3DX27 effects, D3D9 and DXVK, existing retained/GPU fixtures.

## Global Constraints

- Developer-only experiment: no production edits, deployment or new gameplay request.
- No graphics-quality, frame-scheduling, logic, network, input or camera changes.
- Native/reference code and game assets remain local and read-only; persist only timings/hashes/counts.
- Fixed fixture geometry and borrowed stable lifetimes are not a production cache contract.
- Execute inline under existing authorization.

### Task 1: Bounded exact-key sharing

**Files:** Modify `perf/tests/bench_material_queue.c`; existing `perf/tests/inst_retained_engine.c` remains the replay/restoration implementation.

- [x] Add `lifecycle-bulk-shared-clock` as an explicit protocol. It enables advancing time, real effect Begin/End, aggregate accounting and full per-hit state restoration.
- [x] Include the first mesh's model and base vertex, fixture VB/IB/declaration, and ordered method-array identities in the key prefix. Append every existing material key byte, including Time and texture identity. Use FNV only for slot selection; require full-size and byte equality for a hit. A miss overwrites the selected slot using the existing capture path.
- [x] Keep old protocols unchanged. The new mode requires cached replay only when its extra lookup succeeds. Fresh bone staging and state capture/restore stay timed.
- [x] Adjust advancing-clock validation to count a bounded number of unique-key misses in a shared regular frame, then all extra groups hit. Preserve exact image comparison and changed-input invalidation; require actual cross-group hits so a no-op implementation fails.

### Task 2: Correctness and cost decision

**Files:** Preserve measurements under `docs/benchmarks/`; update STATUS, BACKLOG and this plan.

- [x] Build `mingw32-make build/bench_material_queue.exe` with the documented i686 toolchain.
- [x] Run sequential native D3D9 and DXVK lit/shadow cases using the local Shaders.big and msvcr71.dll, churn 10, protocol `lifecycle-bulk-shared-clock`. Require nonempty pixels, changed-pose/input controls, correct lifecycle restoration and the existing omitted-restore/binding corruption controls.
- [x] Run `lifecycle-bulk-state-clock` as the old-cache control using the same executable. Preserve all samples and source/input hashes; compare both against their common native baseline without adding lit/shadow workloads.
- [x] Run `lifecycle-bulk-shared-vs-bulk-clock` against the existing optimistic bulk-after-second prototype in the same paired run. This is a stricter screen, still not the full beta.13 native group path. Reject standalone promotion if the gain disappears with bulk already active.
- [x] Add a forced-collision control that maps every key to one slot; verify correct recapture and output despite lost hits. Include changed geometry identity in refusal checks.
- [x] Promote only to further native qualification if savings are substantial. A fixture gain alone neither justifies installation nor establishes the real scene's hit rate.
- [x] Commit/push the bounded experiment and decision on `codex/perf-beta` with the repository trailer.

## Decision

Reject standalone runtime promotion. The stricter already-bulk comparison
regresses on DXVK by 0.7895 ms lit and 0.9704 ms shadow per modeled pair.
All twelve correctness runs pass; 252 samples and final hashes are preserved
in docs/benchmarks/2026-10-18-shared-material-cache.json. No production or
installed changes. The user redirected investigation toward Accelerator and
SAGE Unlocked; see STATUS.md for the external evaluation checkpoint.
