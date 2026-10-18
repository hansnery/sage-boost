# Full-rate 60 FPS investigation: next beta implementation plan

> **For agentic workers:** Use subagent-driven execution for the independent
> pure profiler and read-only audit, with parent integration/review. The
> superpowers execution skills are unavailable; use the available tools.

**Goal:** Build 0.9.11-beta.2 with separate extra-pass measurements and
presentation-call pacing data, preserving every rendering decision.

**Architecture:** A pure, bounded profiler stages four recognized pass
markers and commits them only after a complete successful extra draw.
The FPS module owns it; timing hooks forward extra markers without changing
regular scope attribution. End_Scene returns supply presentation-call counts
and a bounded gap histogram, independent of limiter-wait estimates.

**Tech Stack:** Freestanding i686 C, native hook tests, D3D9/DXVK harnesses.

## Constraints

Follow the approved full-shadow design. No cadence/quality, simulation,
network, camera, FP-control, signature or installed-file changes. No EA
assets saved. The 60 FPS goal remains unproven until user gameplay testing.

## Tasks

- [x] Profiler module `extraprofile.c/.h`: init/begin/marker begin/end/end/take;
  four pass totals and sample counts, valid/invalid draws, stack bound 8.
  Unknown marker endpoints ignored; malformed tracked scopes invalidate
  the whole frame. Aggregate repeated same-pass nesting only once.
- [x] Mandatory CPU tests: unknown/nested/malformed/overflow/abandoned/failed
  scopes, clock wrap, row extraction and reset. Report native overhead.
- [x] Wire `fps.c/.h` and `timing.c`: filter by main thread before reading
  marker data or clocks; no clock read for unknown mesh markers. Commit
  extra samples only after all draw safety checks pass.
- [x] Wrap extra End_Scene calls using the existing return stub; keep their
  time and count out of regular presentation metrics. Count completed
  flip-enabled End_Scene calls, not GPU/display scanout. Capture bounded
  gap bins and conservative percentile upper bounds.
- [x] Append CSV columns in `csvrow.c/.h` and map in `timing.c`. Missing
  samples produce blank durations. Expose raw histogram bins, per-pass
  totals/counts and invalid frames; retain all old column positions.
- [x] Extend native FPS/hook/row tests for integration, safety failure,
  wrong-thread calls, row reset, histogram boundaries and missing samples.
- [x] Audit full-pass setup using local read-only sources; record candidate
  optimizations and proof gaps without enabling speculative rendering cuts.
- [x] Bump runtime/resource/load-test version to 0.9.11-beta.2 (0,9,11,2).
  Run full plugin tests, offline shader checks, diff review/check.
- [x] Record results, package ASI + unchanged INI with hashes, commit locally.

Build/test from Git Bash with the documented WinLibs i686 PATH. Do not
launch the game. No deployment or publication in this implementation turn.
