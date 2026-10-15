# Performance DLL, plan 6: the shadow map interval (Milestone 3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** plugin 0.5.0 can redraw the shadow map only every Nth frame
(`[Render] ShadowInterval`, 1 = unchanged game) while the shaders keep the
matrix that matches the stored map, and counts the meshes each pass draws.
No caster cull is built: the game already culls casters to a light frustum
it refits to the view (spec, Milestone 3).

**Architecture:** two call hooks in one group: `UpdateShadowMap`'s call
in `W3DDisplay::draw` (skip continues at a `ret 4` thunk) and the
`SetShadowMapInfo` call in the per-frame matrix setter (a probe that saves
the matrix after a run and restores it after a skip). The interval
scheduler is the reflection's (`reflect.c`). Mesh markers are counted by
the open frame-level marker (shadow, reflection, views).

**Tech Stack:** C (freestanding, 32-bit MinGW-w64, kernel32 only), GNU
assembler, GNU make from Git Bash.

## Global Constraints

- Nothing under `C:\repositories\Open-BFME-2\` is written.
- No code from SAGE-Unlocked (GPL-3) or bfme2-accelerator (MIT).
- No hardcoded absolute addresses: every hook is found by signature.
- Nothing in `dll/` changes.
- Freestanding build with `-Werror` and `-mgeneral-regs-only`; the matrix is copied as 16 dwords, never read as floats.
- `ShadowInterval=1` (the default) installs neither shadow hook.
- Client-only.
- In the game folder only `mss\bfme2perf.asi` is replaced, and `mss\bfme2perf.ini` gets the new keys, keeping its values.
- Conventional commits; branch `feat/perf-dll`; don't push.

## The new sites

| Id | What | RVA | Offset | Steal | Callee pops | Kind |
|---|---|---|---|---|---|---|
| `shadow` | `call UpdateShadowMap` in `W3DDisplay::draw` | 0x00049DF5 | 16 | 5 | 4 | call hook, probe |
| `shadow_info` | `call SetShadowMapInfo` in `0x0007BA74` | 0x0007BB37 | 12 | 5 | 12 | call hook, probe |

Signatures: `FF 35 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? C6 45 FC 01 E8 ?? ?? ?? ?? 8D 8D A8 FE FF FF 88 5D FC E8`
and `8B 0D ?? ?? ?? ?? 8D 45 C4 50 6A 00 E8 ?? ?? ?? ?? 5E 83 C5 78 C9 C3`
(both unique in `game.dat` and `game820.dat`). At the second call
`[esp+4]` = 0 (shadow map index), `[esp+8]` = the matrix, `[esp+12]` =
the texture handle.

---

### Task 1: the setting

- [x] `test_config.c`: defaults `shadow_interval == 1 && shadow_requested == 1`; `[Render] ShadowInterval=2` → 2; `=7` → 4 (requested 7); `=0` → 1. Run: fails (`no member named 'shadow_interval'`).
- [x] `config.h`: `int shadow_requested; int shadow_interval;`; `pf_config_load` reads `[Render] ShadowInterval` (default 1) and clamps to 1..4 like `ReflectionInterval`.
- [x] Run: `OK: config tests passed`. Commit `feat(perf): read ShadowInterval`.

### Task 2: the new columns

- [x] `test_row.c`: the header ends `reflect_skipped,shadow_skipped,shadow_meshes,reflect_meshes,view_meshes\r\n`; the full row (`have` adds `PF_HAVE_SHADOW` 256, `shadow_skipped` 7, `shadow_meshes` 3000, `view_meshes` 6000 over 30 frames) ends `,2.000,15,7,100,0,200\r\n`; the frame-only and skeleton rows end `,,,,\r\n` after their `reflect_skipped` field. Run: fails.
- [x] `csvrow.h`: `PF_HAVE_SHADOW 256u`; `pf_row` gains `unsigned shadow_skipped, shadow_meshes, reflect_meshes, view_meshes` (row totals). `csvrow.c` appends to the header and, after `reflect_skipped`:

```c
    sep(&o);
    if (r->have & PF_HAVE_SHADOW)
        put_u(&o, r->shadow_skipped);
    sep(&o);
    if (r->have & PF_HAVE_SCOPES)
        put_u(&o, (r->shadow_meshes + f / 2) / f);
    sep(&o);
    if (r->have & PF_HAVE_SCOPES)
        put_u(&o, (r->reflect_meshes + f / 2) / f);
    sep(&o);
    if (r->have & PF_HAVE_SCOPES)
        put_u(&o, (r->view_meshes + f / 2) / f);
```

- [x] Run: `OK: row tests passed`. Commit `feat(perf): shadow and per-pass mesh columns`.

### Task 3: the sites

- [x] `test_sites.c`: prologues 13 and 14 are `"E8 ?? ?? ?? ??"`. Run: fails (initialiser count).
- [x] `sites.h`: `PF_SHADOW`, `PF_SHADOW_INFO` before `PF_SITE_COUNT`; `PF_GROUP_SHADOW` before `PF_GROUP_COUNT`. `sites.c`:

```c
    { "shadow", "W3DDisplay::draw, the UpdateShadowMap call",
      "FF 35 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? C6 45 FC 01 E8 ?? ?? ?? ?? 8D 8D A8 FE FF FF 88 5D FC "
      "E8",
      16, 5, 4, PF_GROUP_SHADOW, 1, 1, 0x00049DF5 },
    { "shadow_info", "the SetShadowMapInfo call in the shadow matrix setter",
      "8B 0D ?? ?? ?? ?? 8D 45 C4 50 6A 00 E8 ?? ?? ?? ?? 5E 83 C5 78 C9 C3",
      12, 5, 12, PF_GROUP_SHADOW, 1, 1, 0x0007BB37 },
```

- [x] `stubs.c`: pairs 13 and 14. `test_hooks.c`: two more table entries, disabled. Run: every suite passes. Commit `feat(perf): the shadow map call sites`.

### Task 4: the gate, the matrix, the mesh counts

**Interfaces:** `void pf_timing_set_shadow(unsigned interval)`; stats gain `shadow_runs, shadow_skipped, shadow_forced, shadow_restored`.

Behaviour in `timing.c`:
- `PF_SHADOW` sets the main thread like the frame and the reflection; on
  the main thread `pf_reflect_next(&g_shadow, now())`; SKIP continues at
  `pf_skip_thunk(4)` and sets `g_shadow_stale`; RUN/FORCED clear it.
- `PF_SHADOW_INFO` on the main thread: `matrix = (unsigned *)slot[2]`; if
  `g_shadow_stale` and a matrix was saved, copy the 16 saved dwords over
  it (`shadow_restored`); otherwise save its 16 dwords. Always continue at
  the callee.
- `scope_begin`: a per-mesh label is counted to the pass of the open
  frame-level marker (shadow, reflection or views) before `Detail` is
  looked at; the pass is set when such a marker begins and cleared when
  it ends.
- The row gets `shadow_skipped` and the three mesh counts; `PF_HAVE_SHADOW`
  when both shadow sites are installed.

- [x] **`test_hooks.c`**: `fake_frame` gains, before the logic call,

```c
        "    incl _g_frame_no\n"
        "    pushl $3\n"
        "    movl $0x5EED00DD, %eax\n"
        "    call _fake_shadow\n"
```

and, before `present`,

```c
        "    subl $64, %esp\n"
        "    movl _g_frame_no, %eax\n"
        "    movl %eax, (%esp)\n"
        "    movl %esp, %edx\n"
        "    pushl $0\n"
        "    pushl %edx\n"
        "    pushl $0\n"
        "    movl $0x5EED00EF, %eax\n"
        "    call _fake_set_info\n"
        "    addl $64, %esp\n"
```

with `_fake_shadow: incl _g_shadow_runs; ret $4` and `_fake_set_info:
movl 8(%esp), %eax; movl (%eax), %eax; movl %eax, _g_seen_matrix0; ret $12`;
sites `"B8 DD 00 ED 5E E8 ?? ?? ?? ??"` (offset 5, pops 4) and
`"B8 EF 00 ED 5E E8 ?? ?? ?? ??"` (offset 5, pops 12). With
`pf_timing_set_shadow(2)`: over the three frames the shadow runs twice and
`SetShadowMapInfo` sees the matrix of frames 1, 1, 3; stats
`shadow_runs == 2`, `shadow_skipped == 1`, `shadow_restored == 1`; the row's
`shadow_skipped` (34) is `1` and `view_meshes` (37) is `1`.
- [x] Run: fails (`pf_timing_set_shadow` undeclared). Implement. Run: `OK: hook tests passed`. Commit `feat(perf): redraw the shadow map every Nth frame, keeping its matrix`.

### Task 5: 0.5.0, install

- [x] `test_load.c`: 0.5.0; the settings line has `[Render] ReflectionInterval=1 ShadowInterval=1`; `render: ShadowInterval=1`; `hook shadow (...): off`. Run: fails.
- [x] `main.c`: both shadow sites enabled only with `ShadowInterval` > 1; `pf_timing_set_shadow` before installing; the main thread can come from the shadow hook; logs. `version.rc` 0.5.0; `bfme2perf.ini` gains `ShadowInterval=1` with its comment.
- [x] `mingw32-make clean test`: every suite `OK`, `KERNEL32.dll` only, no x87/SSE instruction. Commit `feat(perf): shadow map interval build 0.5.0`.
- [x] `make dist`; install `bfme2perf.asi`; in the installed INI keep the user's values, set `ShadowInterval=2` and `Detail=0`.
