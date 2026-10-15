# Performance DLL, plan 7: batching identical units (Milestone 4)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 0.6.0 measures how many neighbouring FX groups could share one
effect `Begin`/`End` (the cheapest batching option), so the merge is only
built if it pays. Rendering is unchanged; `[Render] Batching` is read and
logged, default 0.

**Architecture:** one more call-hook probe, on the per-group setup call in
the FX shader batch renderer. It reads the group's first mesh, record,
method and end from the registers the stub saved, and the mesh's light
environment. The marker probe counts mesh markers per group. Counts go into
three appended CSV columns.

**Tech Stack:** C (freestanding, 32-bit MinGW-w64, kernel32 only), GNU
assembler, GNU make from Git Bash.

## Global Constraints

- Nothing under `C:\repositories\Open-BFME-2\` is written.
- No code from SAGE-Unlocked or bfme2-accelerator; no EA shader files redistributed.
- No hardcoded absolute addresses: every hook is found by signature, checked in `game.dat` and `game820.dat`.
- Nothing in `dll/` changes.
- Freestanding build with `-Werror` and `-mgeneral-regs-only`.
- Client-only, render-only, read-only probes.
- In the game folder only `mss\bfme2perf.asi` and `mss\bfme2perf.ini` change; the INI keeps its values and gains `Batching=0`.
- Conventional commits on `feat/perf-dll`, pushed after each milestone, no force-push.

## Milestones

1. **B1, measure (0.6.0, this plan).** The probe and columns below.
2. **B2, merge: no-go (2026-10-13).** The 0.6.0 stress test showed
   `fx_merge` 0% and `fx_merge_anylight` 5.6–12% of `fx_groups`, far
   below the 25% gate (spec, "B1 result"). Not built. The design is kept
   below for reference. Only if B1 shows `fx_merge` ≥ a
   quarter of `fx_groups` in a heavy battle. Wrap the method's slots 2, 3,
   5, 6 (pass-through outside the batch function), defer `EndPass`/`End`
   of a group, skip `Begin`/`BeginPass` of a mergeable next group, flush
   when the next group isn't mergeable (before its setup call) and when the
   batch function returns. Its own plan.
3. **B3, instancing with our own skinning shaders.** A separate project
   (see the spec).

### Task 1: the site

- [x] `test_sites.c`: prologue 15 is `"E8 ?? ?? ?? ??"`. Run: fails (initialiser count).
- [x] `sites.h`: `PF_FX_GROUP` before `PF_SITE_COUNT`, `PF_GROUP_BATCH` before `PF_GROUP_COUNT`. `sites.c`:

```c
    { "fx_group", "the FX shader batch, the per-group setup call",
      "68 ?? ?? ?? ?? 8D 8D 8C FD FF FF E8 ?? ?? ?? ?? 8B 0B C6 45 FC 01 E8 ?? ?? ?? ?? 8B 06 68 "
      "FF FF 00 00",
      22, 5, 0, PF_GROUP_BATCH, 1, 1, 0x00173CB8 },
```

- [x] `stubs.c`: pair 15; `test_hooks.c`: a disabled entry; `main.c`: enabled with timing. Run: every suite passes. Commit `feat(perf): the FX group setup call site`.

### Task 2: the columns

- [x] `test_row.c`: the header ends `,shadow_full_rate,fx_groups,fx_merge,fx_merge_anylight\r\n`; the full row (`have` adds `PF_HAVE_BATCH` 512; 3000, 900, 1500 over 30 frames) ends `,4,100,30,50\r\n`; the others gain `,,,`. Run: fails.
- [x] `csvrow.h/.c`: the bit, the three totals, the three fields (per frame). Run: passes. Commit `feat(perf): FX group and merge columns`.

### Task 3: the counts

**Interfaces:** stats gain `fx_groups, fx_merge, fx_merge_anylight`.

- [x] `test_hooks.c`: `fake_group(end, record, mesh, method)` sets EBX = record, ECX = mesh, ESI = method and keeps its first argument at `[EBP+8]`, then `mov eax, 0x5EED00F1; call _fake_group_setup` (site `"B8 F1 00 ED 5E E8 ?? ?? ?? ??"`, offset 5, pops 0). Four groups with method M: A (2 records, env E, two mesh markers), B (env E, one marker), C (env F, one marker), D (method N). Expect groups 4, `fx_merge` 1 (A→B), `fx_merge_anylight` 2 (A→B, B→C). Run: fails.
- [x] `timing.c`: the probe reads `slot[-4]` (ECX), `slot[-6]` (EBX), `slot[-8]` (EBP), `slot[-9]` (ESI); closes the previous group (single-pass when its markers equal its records), compares, opens the new one. Any marker other than per-mesh ones and `RenderFXShaderBatch`, or a pass change, ends the run of neighbours. Run: passes. Commit `feat(perf): count the FX groups that could share an effect pass`.

### Task 4: 0.6.0, install, push

- [x] `test_config.c`: `Batching` default 0, `=1` → 1. `test_load.c`: 0.6.0, the settings line with `Batching=0`, `hook fx_group (...)` refused in a non-game host. Run: fails.
- [x] `config.c/h`, `main.c` (log line), `version.rc` 0.6.0, `bfme2perf.ini` (`Batching=0` with a comment). `mingw32-make clean test`: all `OK`, KERNEL32 only, no x87/SSE. Commit `feat(perf): FX batch measurement build 0.6.0`.
- [x] Install `bfme2perf.asi`; add `Batching=0` to the installed INI keeping its values; push.
