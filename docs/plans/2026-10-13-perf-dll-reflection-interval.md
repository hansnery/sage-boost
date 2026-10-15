# Performance DLL, plan 5: the water reflection interval (Milestone 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** plugin 0.4.0 can redraw the water reflection only every Nth
frame (`[Render] ReflectionInterval`, 1 = unchanged game), and fixes the two
0.3.0 measurement issues (per-mesh labels, probe overhead switch).

**Architecture:** a *call hook* on the one `call` that runs the reflection
(AotR 0x00049E5D): the `E8` goes to the generic entry stub, and `pf_enter`
continues either at the old callee (run) or at a `ret 8` thunk (skip). A
pure scheduler (`reflect.c`) decides; a pure classifier (in `csvrow.c`)
maps marker labels to columns and scope names.

**Tech Stack:** C (freestanding, 32-bit MinGW-w64, kernel32 only, GNU
assembler), GNU make from Git Bash.

Spec: `docs/specs/2026-10-13-perf-dll-design.md`, "Milestone 2: water
reflection interval".

## Global Constraints

- Nothing under `C:\repositories\Open-BFME-2\` is written.
- No code from SAGE-Unlocked (GPL-3) or bfme2-accelerator (MIT).
- No hardcoded absolute addresses: every hook is found by signature in the running executable.
- Nothing in `dll/` changes.
- Freestanding build with `-Werror` and `-mgeneral-regs-only`.
- `ReflectionInterval=1` (the default) installs no reflection hook: the game's bytes there stay as they are.
- Client-only: only drawing is skipped; nothing that feeds the simulation changes.
- In the game folder only `mss\bfme2perf.asi` is replaced, and `mss\bfme2perf.ini` gets the new keys, keeping its values.
- Conventional commits; branch `feat/perf-dll`; don't push.

## The new site

| Id | What | RVA | Offset | Steal | Callee pops | Kind |
|---|---|---|---|---|---|---|
| `reflect` | `call 0x0007F25A` in `W3DDisplay::draw`, inside `UpdateWaterReflection` | 0x00049E5D | 22 | 5 (`E8 rel32`) | 8 | call hook, probe |

Signature: `FF B7 04 01 00 00 8B 0D ?? ?? ?? ?? FF 35 ?? ?? ?? ?? C6 45 FC 02 E8 ?? ?? ?? ?? 8D 8D A8 FE FF FF 88 5D FC E8`
(unique in `game.dat` and `game820.dat`; the callee has no other caller).

## File structure

| File | Change |
|---|---|
| `perf/src/config.h`, `config.c` | `detail`, `reflect_interval`, `reflect_requested` |
| `perf/src/reflect.h`, `reflect.c` | New: the skip scheduler |
| `perf/src/csvrow.h`, `csvrow.c` | `pf_scope_column`, `pf_scope_name`, `pf_is_mesh_label`; `fx_batch_ms`, `reflect_skipped` appended |
| `perf/src/sites.h`, `sites.c` | `call_hook` field, site 12 `reflect` |
| `perf/src/patch.h`, `patch.c` | call hooks; `PF_ST_BAD_TARGET` |
| `perf/src/stubs.h`, `stubs.c` | 13 stub pairs; `ret` thunks |
| `perf/src/timing.h`, `timing.c` | the gate, `Detail`, label fixes, `NULL` CSV paths |
| `perf/src/main.c`, `version.rc` | 0.4.0, settings, which sites to enable |
| `perf/bfme2perf.ini` | `[Timing] Detail`, `[Render] ReflectionInterval` |
| `perf/tests/test_config.c`, `test_reflect.c` (new), `test_row.c`, `test_sites.c`, `test_hooks.c`, `test_load.c` | as below |

---

### Task 1: settings

**Interfaces:** `pf_config` gains `int detail` (`[Timing] Detail`, default 1, any non-zero is 1), `int reflect_requested` (`[Render] ReflectionInterval` as read, default 1), `int reflect_interval` (clamped to 1..4).

- [x] **Step 1:** In `test_config.c` add: defaults `detail == 1 && reflect_interval == 1 && reflect_requested == 1`; `[Render] ReflectionInterval=2` → 2; `=9` → 4 (requested 9); `=0` → 1 (requested 0); `=-3` → 1; `=two` → 1 (requested 1); `[Timing] Detail=0` → 0.
- [x] **Step 2:** Run: fails (`'pf_config' has no member named 'detail'`).
- [x] **Step 3:** In `config.h` add the three fields; in `pf_config_load`:

```c
    cfg->detail = read_int(L"Timing", L"Detail", 1, ini_path) != 0;
    cfg->reflect_requested = read_int(L"Render", L"ReflectionInterval", 1, ini_path);
    cfg->reflect_interval = cfg->reflect_requested < 1   ? 1
                            : cfg->reflect_requested > 4 ? 4
                                                         : cfg->reflect_requested;
```

- [x] **Step 4:** Run: `OK: config tests passed`. **Commit** `feat(perf): read ReflectionInterval and Detail`.

### Task 2: the scheduler, the label classifier, the row

**Interfaces:**
- `reflect.h`: `enum { PF_REFLECT_SKIP, PF_REFLECT_RUN, PF_REFLECT_FORCED }`; `pf_reflect { interval, started, skipped, last_call, stall }`; `void pf_reflect_init(pf_reflect *, unsigned interval, long long stall)`; `int pf_reflect_next(pf_reflect *, long long now)`.
- `csvrow.h`: `PF_SCOPE_FX_BATCH` (= `PF_SCOPE_COLUMNS`), `PF_SCOPE_ALL`; `PF_HAVE_REFLECT 128`; `pf_row.scope_us[PF_SCOPE_ALL]`, `pf_row.reflect_skipped`; `int pf_is_mesh_label(const char *)`, `int pf_scope_column(const char *)`, `void pf_scope_name(const char *label, char *out, unsigned cap)`.

- [x] **Step 1: Write `perf/tests/test_reflect.c`**

```c
/* The water reflection skip scheduler. */
#include <stdio.h>

#include "reflect.h"

static int g_failures;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            g_failures++;                                 \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

static void run(unsigned interval, const long long *t, const int *want, unsigned n)
{
    pf_reflect r;
    unsigned i;
    pf_reflect_init(&r, interval, 250);
    for (i = 0; i < n; i++) {
        int got = pf_reflect_next(&r, t[i]);
        CHECK(got == want[i], "interval %u, call %u at %lld: %d, want %d", interval, i, t[i], got,
              want[i]);
    }
}

int main(void)
{
    static const long long t[] = { 0, 33, 66, 100, 133, 166, 500, 533, 566 };
    static const int every[] = { 2, 1, 1, 1, 1, 1, 2, 1, 1 };
    static const int half[] = { 2, 0, 1, 0, 1, 0, 2, 0, 1 };
    static const int third[] = { 2, 0, 0, 1, 0, 0, 2, 0, 0 };
    static const int fourth[] = { 2, 0, 0, 0, 1, 0, 2, 0, 0 };
    static const long long edge_t[] = { 0, 249, 499, 749 };
    static const int edge_want[] = { 2, 0, 2, 2 };

    printf("schedules\n");
    run(1, t, every, 9);
    run(2, t, half, 9);
    run(3, t, third, 9);
    run(4, t, fourth, 9);
    printf("stall boundary\n");
    run(4, edge_t, edge_want, 4);
    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: reflect tests passed\n");
    return 0;
}
```

(`edge_want`: 249 is inside the stall and skipped; 499 is 250 after 249,
so it's forced, and so is 749.)

- [x] **Step 2:** In `test_row.c`: `r.have = 255`, `r.scope_us[PF_SCOPE_FX_BATCH] = 60000`, `r.reflect_skipped = 15`, and the full row ends `,1500,1.000,2.000,15\r\n`; the frame-only row ends `10.000,,\r\n`; the skeleton row ends `,0.000,,\r\n`; the header ends `draws,rest_ms,fx_batch_ms,reflect_skipped\r\n`. Classifier checks:

```c
    CHECK(pf_scope_column("UpdateShadowMap") == PF_SCOPE_SHADOW, "shadow");
    CHECK(pf_scope_column("Rendering mesh\tFXShader\tIUNPIKE") == PF_SCOPE_FX_MESH, "fx mesh");
    CHECK(pf_scope_column("Rendering mesh\tDX8Render\tRJNEWS") == PF_SCOPE_DX8_MESH, "dx8 mesh");
    CHECK(pf_scope_column("RenderFXShaderBatch") == PF_SCOPE_FX_BATCH, "fx batch");
    CHECK(pf_scope_column("RenderRoads") == -1, "no column");
    CHECK(pf_is_mesh_label("Rendering mesh\tFXShader\tX") && !pf_is_mesh_label("RenderViews"),
          "mesh label");
    pf_scope_name("Rendering mesh\tFXShader\tIUNPIKE", name, sizeof name);
    CHECK(!strcmp(name, "Rendering mesh FXShader"), "mesh name %s", name);
    pf_scope_name("a,b\tc", name, sizeof name);
    CHECK(!strcmp(name, "a;b c"), "cleaned name %s", name);
    pf_scope_name("RenderVolumeShadowsAndMoreText", name, 8);
    CHECK(!strcmp(name, "RenderV"), "truncated name %s", name);
```

- [x] **Step 3:** Makefile: `test_reflect.exe` from `tests/test_reflect.c src/reflect.c`, run after `test_row`; `SRCS` gains `src/reflect.c`. Run: fails (`reflect.h: No such file or directory`).
- [x] **Step 4: Write `perf/src/reflect.h` and `reflect.c`**

```c
/* Which water reflection updates run when ReflectionInterval > 1. */
#ifndef PF_REFLECT_H
#define PF_REFLECT_H

enum { PF_REFLECT_SKIP, PF_REFLECT_RUN, PF_REFLECT_FORCED };

typedef struct {
    unsigned interval;
    int started;
    unsigned skipped;    /* skipped in a row since the last run */
    long long last_call; /* clock of the previous call */
    long long stall;     /* a gap at least this long forces a run */
} pf_reflect;

void pf_reflect_init(pf_reflect *r, unsigned interval, long long stall);

/* One call per reflection update. RUN: due; FORCED: the first call, or one
 * after a stall (loading, alt-tab, a device reset); SKIP: reuse the last
 * reflection. */
int pf_reflect_next(pf_reflect *r, long long now);

#endif
```

```c
#include "reflect.h"

void pf_reflect_init(pf_reflect *r, unsigned interval, long long stall)
{
    r->interval = interval ? interval : 1;
    r->started = 0;
    r->skipped = 0;
    r->last_call = 0;
    r->stall = stall;
}

int pf_reflect_next(pf_reflect *r, long long now)
{
    int run;

    if (!r->started || now - r->last_call >= r->stall)
        run = PF_REFLECT_FORCED;
    else if (r->skipped + 1 >= r->interval)
        run = PF_REFLECT_RUN;
    else
        run = PF_REFLECT_SKIP;
    r->started = 1;
    r->last_call = now;
    r->skipped = run == PF_REFLECT_SKIP ? r->skipped + 1 : 0;
    return run;
}
```

- [x] **Step 5: `csvrow`:** enum gains `PF_SCOPE_FX_BATCH = PF_SCOPE_COLUMNS, PF_SCOPE_ALL`; `pf_scope_labels[PF_SCOPE_ALL]` ends with `"RenderFXShaderBatch"`; the header appends `,fx_batch_ms,reflect_skipped`; `pf_format_row` appends after `rest_ms`:

```c
    ms_field(&o, (r->have & PF_HAVE_SCOPES) != 0, r->scope_us[PF_SCOPE_FX_BATCH], f);
    sep(&o);
    if (r->have & PF_HAVE_REFLECT)
        put_u(&o, r->reflect_skipped);
```

and the classifier:

```c
static const char mesh_prefix[] = "Rendering mesh\t";

static int starts_with(const char *s, const char *prefix)
{
    while (*prefix && *s == *prefix) {
        s++;
        prefix++;
    }
    return !*prefix;
}

int pf_is_mesh_label(const char *label)
{
    return starts_with(label, mesh_prefix);
}

int pf_scope_column(const char *label)
{
    unsigned i;
    if (starts_with(label, "Rendering mesh\tFXShader"))
        return PF_SCOPE_FX_MESH;
    if (starts_with(label, "Rendering mesh\tDX8Render"))
        return PF_SCOPE_DX8_MESH;
    for (i = 0; i < PF_SCOPE_ALL; i++) {
        const char *a = label, *b = pf_scope_labels[i];
        while (*a && *a == *b) {
            a++;
            b++;
        }
        if (*a == *b)
            return (int)i;
    }
    return -1;
}

void pf_scope_name(const char *label, char *out, unsigned cap)
{
    unsigned i = 0, tabs = 0;
    int mesh = pf_is_mesh_label(label);

    for (; *label && i + 1 < cap; label++) {
        char c = *label;
        if (c == '\t' && mesh && ++tabs == 2)
            break;
        out[i++] = c == '\t' ? ' ' : c == ',' ? ';' : c;
    }
    if (cap)
        out[i] = 0;
}
```

- [x] **Step 6:** Run: `OK: reflect tests passed`, `OK: row tests passed`. **Commit** `feat(perf): reflection skip scheduler and label classifier`.

### Task 3: call hooks and the reflection site

**Interfaces:** `pf_site` gains `int call_hook` after `probe`; `PF_REFLECT` (12), `PF_GROUP_REFLECT`; `PF_ST_BAD_TARGET`; `void *pf_skip_thunk(unsigned arg_bytes)` (for 0, 4, 8, 12; `NULL` otherwise).

- [x] **Step 1:** `test_sites.c`: prologue 12 is `"E8 ?? ?? ?? ??"`. Run: fails (initialiser count).
- [x] **Step 2:** `sites.h`/`sites.c`: the field, the enum entries and the site:

```c
    { "reflect", "W3DDisplay::draw, the water reflection update call",
      "FF B7 04 01 00 00 8B 0D ?? ?? ?? ?? FF 35 ?? ?? ?? ?? C6 45 FC 02 E8 ?? ?? ?? ?? 8D 8D "
      "A8 FE FF FF 88 5D FC E8",
      22, 5, 8, PF_GROUP_REFLECT, 1, 1, 0x00049E5D },
```

(every other entry gets `call_hook` 0).
- [x] **Step 3:** `patch.c`: `locate` also requires, for a call hook, `steal == 5` and the site byte fixed to `E8`. `apply` for a call hook computes the old callee `site + 5 + rel32`, refuses it with `PF_ST_BAD_TARGET` unless it lies in `.text`, calls `set_next(i, callee)`, stores it in `trampoline` and writes `E8` to the stub (no trampoline, no padding).
- [x] **Step 4:** `stubs.c`: pairs 0–12, and

```c
__asm__(".text\n"
        "_pf_ret0:\n    ret\n"
        "_pf_ret4:\n    ret $4\n"
        "_pf_ret8:\n    ret $8\n"
        "_pf_ret12:\n    ret $12\n");
extern char pf_ret0[], pf_ret4[], pf_ret8[], pf_ret12[];

void *pf_skip_thunk(unsigned arg_bytes)
{
    switch (arg_bytes) {
    case 0: return pf_ret0;
    case 4: return pf_ret4;
    case 8: return pf_ret8;
    case 12: return pf_ret12;
    }
    return 0;
}
```

- [x] **Step 5:** `test_hooks.c`: the table gets `call_hook` and a 13th entry (disabled). Run: every suite passes. **Commit** `feat(perf): call hooks and the water reflection site`.

### Task 4: the gate and the measurement fixes

**Interfaces:** `void pf_timing_set_detail(int)`, `void pf_timing_set_reflect(unsigned interval)`; `pf_timing_open` accepts `NULL` paths (no files); stats gain `reflect_runs, reflect_skipped, reflect_forced`.

Behaviour in `timing.c`:
- The main thread is the first to run the frame hook **or** the reflection call.
- `PF_REFLECT`: on the main thread `pf_reflect_next(&g_reflect, now())`; SKIP continues at `pf_skip_thunk(arg_bytes)` and counts, RUN/FORCED continue at the old callee; other threads always run.
- Labels: `pf_is_mesh_label` labels are ignored when `Detail=0`, and otherwise map to one name per kind (cached by kind, never by pointer); other labels as before, named with `pf_scope_name`, columns from `pf_scope_column`. `scope_end` finds its marker before reading the clock.
- The row gets `reflect_skipped` and `scope_us[PF_SCOPE_FX_BATCH]`; `PF_HAVE_REFLECT` when site 12 is installed.

- [x] **Step 1: `test_hooks.c`**: `fake_frame` gains, inside the `RenderViews` marker, a second marker (`g_marker2`, label `"Rendering mesh\tFXShader\tTEST"`) around the two draws, and before `present`:

```c
        "    movl $0x5EED00CC, %eax\n"
        "    pushl $2\n"
        "    pushl $1\n"
        "    call _fake_reflect\n"
```

with `_fake_reflect: incl _g_reflect_runs; movl $0x5EED000D, %eax; ret $8`
and site `{ "reflect", "fake_reflect call", "B8 CC 00 ED 5E 6A 02 6A 01 E8 ?? ?? ?? ??", 9, 5, 8, PF_GROUP_REFLECT, 1, 1, 0 }`.
`pf_timing_set_reflect(2)` before installing. Checks after 3 frames: the
fake reflection ran 2 times (counted from after installing), stats
`reflect_runs == 2`, `reflect_forced == 1`, `reflect_skipped == 1`,
`scopes == 6`; the row's `fx_mesh_ms` (28) non-empty, `reflect_skipped`
(33) `1`; the scopes CSV has `,Rendering mesh FXShader,1.00,`. Then
`pf_timing_set_detail(0)` and one more frame: `scopes == 7`.
- [x] **Step 2:** Run: fails (`pf_timing_set_reflect` undeclared).
- [x] **Step 3:** Implement in `timing.h`/`timing.c` as above.
- [x] **Step 4:** Run: `OK: hook tests passed`. **Commit** `feat(perf): skip water reflection updates, fix mesh labels, Detail switch`.

### Task 5: 0.4.0, install

- [x] **Step 1:** `test_load.c`: version 0.4.0; the settings line `settings: [Perf] Enabled=1 Log=1, [Timing] Enabled=1 Detail=1, [Render] ReflectionInterval=1, [Skeleton] Threads=0 Verify=0`; `render: ReflectionInterval=1`; `hook reflect (W3DDisplay::draw, the water reflection update call): off`. Run: fails on the version.
- [x] **Step 2:** `main.c`: timing sites enabled with `[Timing] Enabled` (the draw counter also needs `Detail=1`), `reflect` only with `ReflectionInterval` > 1; nothing to install → log and return; `pf_timing_open` with `NULL` paths when timing is off; `pf_timing_set_detail`, `pf_timing_set_reflect` before `pf_install`; required group the frame only when timing is on; log a clamped `ReflectionInterval` and the reflection state. `version.rc` 0.4.0; `bfme2perf.ini` gains `Detail=1` and `[Render] ReflectionInterval=1` with comments.
- [x] **Step 3:** `mingw32-make clean test`: every suite `OK`, imports only `KERNEL32.dll`, no x87/SSE instruction. **Commit** `feat(perf): water reflection interval build 0.4.0`.
- [x] **Step 4:** `make dist`; copy `bfme2perf.asi` into `C:\Jogos\AgeoftheRing\rotwk\mss\`; in its `bfme2perf.ini` add `Detail=1` under `[Timing]` and a `[Render]` section with `ReflectionInterval=2`, keeping every existing value.
- [ ] **Step 5:** The user runs the comparison (spec, Milestone 2).
