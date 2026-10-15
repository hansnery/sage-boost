# Performance DLL, plan 8: more than 30 FPS (Milestone 5)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** extra frames drawn only on the local client, in the frame
limiter's idle time, with logic, its schedule, commands and network traffic
exactly as stock (spec, Milestone 5). `[Render] MaxFPS=30` (default) leaves
the game untouched.

**Architecture:** the limiter's `Sleep` call becomes a call to our stub.
The stub either sleeps as stock or draws one render-only frame when it fits
before the stock deadline. Each stage adds what the extra frame shows: F1
the last frame again, F2 an extrapolated camera, F3 extrapolated units.

**Tech Stack:** C (freestanding, 32-bit MinGW-w64, kernel32 only), GNU
assembler, GNU make from Git Bash.

## Global Constraints

- Nothing under `C:\repositories\Open-BFME-2\` is written; everything is found again in `game.dat`/`game820.dat` by signature.
- No code from SAGE-Unlocked or bfme2-accelerator.
- Never write the game's frame counters, `m_maxFPS`, `FramesPerSecondLimit` or `BfmeClientFramesPerSecond`; never run `GameEngine::update`, the client update, the message stream, audio or the network from an extra frame.
- Never delay a stock frame: an extra frame starts only if its measured cost fits before the deadline with a margin.
- Every failed check falls back to stock for the session and logs why.
- Freestanding build with `-Werror` and `-mgeneral-regs-only`; client-only.
- Conventional commits on `feat/perf-dll`, pushed after each milestone.

Status (2026-10-13): F0–F3 are built as 0.8.0-fps on `feat/perf-fps`
(spec, "F0/F1 as built" and "F2/F3 as built"); not yet run in the game.

## F0: measure (0.7.0)

- [x] **Sites.** Signatures, checked unique in both executables, for:
  - the limiter's wait loop (`push ebx; call [Sleep]; call [timeGetTime]; mov edi, eax; sub eax, [last]; cmp eax, esi; jb`), site at the `call [Sleep]`;
  - the display draw called by the client update (`TheDisplay->draw`), for F1;
  - the run-ahead frame-rate update (the minimum-FPS clamp), to confirm the negotiation.
- [x] **A 6-byte indirect-call hook kind** in `patch.c`: `FF 15 <iat>` becomes `E8 <stub> 90`; the stub continues at the IAT target. Tests: `test_hooks.c` fake loop with an indirect call; `test_sites.c` the new sites.
- [x] **Probe:** with `MaxFPS` > 30, time spent in the wait per game frame. CSV: `idle_ms` (and the F1 columns below).
- [ ] The user runs `Test Replay` and a skirmish (not installed by this branch).

## F1: hold frames (0.8.0-fps, `MaxFPS=60`)

- [x] Extra frame = display draw only, shadow and reflection reused, with the fallbacks: logic seed, engine words and client frame unchanged, `-verifyClientCRC` CRC, late game frames, minimized, screenshot, draw function check.
- [ ] Verify in the game: `-verifyClientCRC` replay at 30 and 60; CSV `extra_frames`, `extra_ms`, `extra_late`.

## F2: camera (0.8.0)

- [ ] Save the camera transform at each stock frame; in an extra frame set it extrapolated by half a frame, restore after.

## F3: units (later)

- [ ] Find the drawable list and transform interpolation; prove the path draws no logic random and writes no logic state; extrapolate transforms and the WW3D sync time for the extra frame, restore after.

## Tests for the whole milestone

- Replays at `MaxFPS=30` and `60` with `-verifyClientCRC`: same end, no report.
- Two-PC Network game, one PC at 60 with the plugin, one without: no `GameCRCMismatch`, no teleporting, no slowdown; both logs show the same logic frame counts.
