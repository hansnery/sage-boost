# Performance DLL, plan 4: the render breakdown (Milestone 1b)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** plugin 0.3.0 splits the frame further: visibility check, object
loop, flush, `Present`, draw calls, and every render section the game names
itself (shadow map, water reflection, views, UI, terrain, water, particles,
trees, shrubs, shadows, sort lists, FX and DX8 meshes), still without
changing what the game does.

**Architecture:** the 0.2.0 wrapped hooks gain four more (`Visibility_Check`,
`Customized_Render`, `RTS3DScene::Flush`, `DX8Wrapper::End_Scene`), plus a
new hook kind, the *probe*: the entry stub calls `pf_enter`, which records
something and continues at the trampoline without touching the return
address. Probes watch `BFME2ScopedRenderEvent`'s constructor and destructor
(the game's named render markers, matched by their `this`) and
`BfmeDrawOps::Draw` (counted). The marker destructor is a lone `jmp rel32`,
so its trampoline jumps to the decoded target, and its signature starts a
few bytes before the site (`site_offset`). Rows close on whole seconds
since the first frame (`pf_clock`).

**Tech Stack:** C (freestanding, 32-bit MinGW-w64, kernel32 only, GNU
assembler for the stubs and test functions), GNU make from Git Bash.

Spec: `docs/specs/2026-10-13-perf-dll-design.md`, "Milestone 1b".

## Global Constraints

- Nothing under `C:\repositories\Open-BFME-2\` is written.
- No code from SAGE-Unlocked (GPL-3) or bfme2-accelerator (MIT).
- No hardcoded absolute addresses: every hook is found by signature in the running executable.
- Nothing in `dll/` changes; code needed from it is copied into `perf/`.
- The build stays freestanding with `-Werror` and `-mgeneral-regs-only`.
- No behaviour change in the game: the hooks only read the clock and count.
- The only file written in the game folder is `mss\bfme2perf.asi`; `mss\bfme2perf.ini` only if it doesn't exist.
- Conventional commits; branch `feat/perf-dll`; don't push.

## Sites added (AotR = 2.01 RVAs)

| Id | Function | RVA | Offset | Steal | `ret` pops | Group | Kind |
|---|---|---|---|---|---|---|---|
| `vis` | `RTS3DScene::Visibility_Check` | 0x00070B83 | 0 | 5 (`B8 imm32`) | 4 | scene | wrapped |
| `cust` | `RTS3DScene::Customized_Render` | 0x0006FE84 | 0 | 6 (`55 8B EC 83 EC 0C`) | 4 | scene | wrapped |
| `flush` | `RTS3DScene::Flush` | 0x00070F44 | 0 | 5 (`B8 imm32`) | 4 | scene | wrapped |
| `present` | `DX8Wrapper::End_Scene(bool)` | 0x001225E0 | 0 | 6 (`64 A1 00 00 00 00`) | 0 | present | wrapped |
| `event_begin` | `BFME2ScopedRenderEvent` constructor | 0x00117690 | 0 | 6 (`8B 44 24 04 85 C0`) | 12 | markers | probe |
| `event_end` | `BFME2ScopedRenderEvent` destructor | 0x00117740 | 20 | 5 (`jmp rel32`) | 0 | markers | probe |
| `draw` | `BfmeDrawOps::Draw` | 0x0011FD40 | 0 | 5 (`A1 abs32`) | 0 | draws | probe |

Every signature (below, in `sites.c`) is unique in `game.dat` and
`game820.dat`; no branch lands inside a stolen range.

## File structure

| File | Change |
|---|---|
| `perf/src/clock.h`, `clock.c` | New: whole-second row clock |
| `perf/src/csvrow.h`, `csvrow.c` | 20 new columns, scope-row formatter |
| `perf/src/sites.h`, `sites.c` | 12 sites; `site_offset`, `probe` |
| `perf/src/patch.c` | `site_offset`; a stolen `jmp rel32` |
| `perf/src/stubs.c` | 12 entry and 12 exit stubs |
| `perf/src/timing.h`, `timing.c` | Probes, markers, covered time, scopes CSV, clock |
| `perf/src/main.c`, `version.rc` | 0.3.0, scopes CSV path |
| `perf/tests/test_row.c` | New columns, scope row, clock |
| `perf/tests/test_sites.c` | 12 sites, offsets |
| `perf/tests/test_hooks.c` | Probes, markers, draws, present, `jmp` trampoline |
| `perf/tests/test_load.c` | 0.3.0, no scopes CSV left |
| `perf/Makefile` | `clock.c` in the DLL and the tests |

---

### Task 1: the clock and the wider row

**Files:** create `perf/src/clock.h`, `perf/src/clock.c`; rewrite `perf/src/csvrow.h`, `perf/src/csvrow.c`, `perf/tests/test_row.c`; modify `perf/Makefile` (`test_row.exe` also builds `src/clock.c`; `SRCS` gains `src/clock.c`).

**Interfaces:**
- Produces: `pf_clock { first, freq, row }`, `void pf_clock_start(pf_clock *, long long t, long long freq)`, `int pf_clock_tick(pf_clock *, long long t, unsigned *closed)`; `PF_HAVE_SCENE 8`, `PF_HAVE_PRESENT 16`, `PF_HAVE_SCOPES 32`, `PF_HAVE_DRAWS 64`; `enum PF_SCOPE_SHADOW … PF_SCOPE_DX8_MESH, PF_SCOPE_COLUMNS`; `PF_SCOPE_IS_FRAME_LEVEL(c)`; `extern const char *const pf_scope_labels[]`; `pf_row` gains `vis_us, cust_us, flush_us, present_us, scope_us[PF_SCOPE_COLUMNS], draws, rest_us`; `extern const char pf_scope_header[]`; `unsigned pf_format_scope_row(unsigned time_s, const char *label, unsigned calls, unsigned frames, unsigned long long us, char *out, unsigned cap)`.

- [x] **Step 1: Write the test** `perf/tests/test_row.c`

```c
/* The CSV formatters and the whole-second row clock. */
#include <stdio.h>
#include <string.h>

#include "clock.h"
#include "csvrow.h"

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

static void commas(char *s, unsigned n)
{
    while (n--)
        strcat(s, ",");
}

int main(void)
{
    char buf[512], expect[512];
    unsigned n, closed = 99;
    pf_clock c;
    pf_row r;

    printf("header\n");
    CHECK(!strcmp(pf_csv_header,
                  "time_s,frames,frame_avg_ms,frame_max_ms,logic_ms,render_ms,skel_render_ms,"
                  "skel_other_ms,anim_on_screen,to_workers,main_thread,skel_us_per_obj,"
                  "vis_ms,cust_ms,flush_ms,present_ms,shadow_ms,reflect_ms,views_ms,ui_ms,"
                  "terrain_ms,water_ms,particles_ms,trees_ms,shrubs_ms,decal_shadows_ms,"
                  "volume_shadows_ms,static_sort_ms,fx_mesh_ms,dx8_mesh_ms,draws,rest_ms\r\n"),
          "header");
    CHECK(!strcmp(pf_scope_labels[PF_SCOPE_VIEWS], "RenderViews") &&
              !strcmp(pf_scope_labels[PF_SCOPE_DX8_MESH], "MeshDX8Render"),
          "scope labels");

    printf("full row\n");
    memset(&r, 0, sizeof r);
    r.time_s = 3;
    r.frames = 30;
    r.frame_us = 500010;
    r.frame_max_us = 25000;
    r.logic_us = 60000;
    r.render_us = 300000;
    r.skel_render_us = 90000;
    r.skel_other_us = 3000;
    r.skel_render_calls = 12000;
    r.vis_us = 15000;
    r.cust_us = 120000;
    r.flush_us = 150000;
    r.present_us = 90000;
    r.scope_us[PF_SCOPE_SHADOW] = 30000;
    r.scope_us[PF_SCOPE_VIEWS] = 240000;
    r.draws = 45000;
    r.rest_us = 30000;
    r.have = 127;
    n = pf_format_row(&r, buf, sizeof buf);
    strcpy(expect, "3,30,16.667,25.000,2.000,10.000,3.000,0.100,400,0,0,7.50,"
                   "0.500,4.000,5.000,3.000,1.000,0.000,8.000");
    for (n = 0; n < 11; n++)
        strcat(expect, ",0.000");
    strcat(expect, ",1500,1.000\r\n");
    n = pf_format_row(&r, buf, sizeof buf);
    CHECK(n == strlen(buf) && !strcmp(buf, expect), "full row:\n%s%s", buf, expect);

    printf("frame only\n");
    memset(&r, 0, sizeof r);
    r.time_s = 7;
    r.frames = 30;
    r.frame_us = 300000;
    r.frame_max_us = 12345;
    r.rest_us = 300000;
    n = pf_format_row(&r, buf, sizeof buf);
    strcpy(expect, "7,30,10.000,12.345,,,,,,0,0,");
    commas(expect, 20);
    strcat(expect, "10.000\r\n");
    CHECK(n && !strcmp(buf, expect), "frame only:\n%s%s", buf, expect);

    printf("skeleton without calls\n");
    r.have = PF_HAVE_SKEL;
    r.rest_us = 0;
    n = pf_format_row(&r, buf, sizeof buf);
    strcpy(expect, "7,30,10.000,12.345,,,0.000,0.000,0,0,0,");
    commas(expect, 19);
    strcat(expect, ",0.000\r\n");
    CHECK(n && !strcmp(buf, expect), "no skeleton calls:\n%s%s", buf, expect);
    CHECK(pf_format_row(&r, buf, 10) == 0, "too small a buffer is refused");

    printf("scope row\n");
    n = pf_format_scope_row(60, "RenderViews", 45, 30, 240000, buf, sizeof buf);
    CHECK(n && !strcmp(buf, "60,RenderViews,1.50,8.000\r\n"), "scope row: %s", buf);
    CHECK(!strcmp(pf_scope_header, "time_s,scope,calls_per_frame,ms_per_frame\r\n"),
          "scope header");

    printf("clock\n");
    pf_clock_start(&c, 500, 1000);
    CHECK(!pf_clock_tick(&c, 1400, &closed), "1400 inside second 0");
    CHECK(pf_clock_tick(&c, 1500, &closed) && closed == 0 && c.row == 1, "1500 closes 0");
    CHECK(!pf_clock_tick(&c, 2499, &closed), "2499 inside second 1");
    CHECK(pf_clock_tick(&c, 2500, &closed) && closed == 1 && c.row == 2, "2500 closes 1");
    CHECK(pf_clock_tick(&c, 5200, &closed) && closed == 2 && c.row == 4,
          "a long frame closes 2, opens 4");
    CHECK(!pf_clock_tick(&c, 5499, &closed), "5499 inside second 4");
    CHECK(pf_clock_tick(&c, 5500, &closed) && closed == 4 && c.row == 5, "5500 closes 4");

    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: row tests passed\n");
    return 0;
}
```

- [x] **Step 2: Run it to see it fail** — `clock.h: No such file or directory`.

- [x] **Step 3: Write `perf/src/clock.h` and `perf/src/clock.c`**

```c
/* Rows close on whole seconds since the first frame. */
#ifndef PF_CLOCK_H
#define PF_CLOCK_H

typedef struct {
    long long first, freq;
    unsigned row; /* time_s of the open row */
} pf_clock;

void pf_clock_start(pf_clock *c, long long t, long long freq);

/* At a frame's end: 1 if t has reached the second after the open row's;
 * *closed gets that row's time_s and the next row opens at t's second. */
int pf_clock_tick(pf_clock *c, long long t, unsigned *closed);

#endif
```

```c
#include "clock.h"

void pf_clock_start(pf_clock *c, long long t, long long freq)
{
    c->first = t;
    c->freq = freq;
    c->row = 0;
}

int pf_clock_tick(pf_clock *c, long long t, unsigned *closed)
{
    if (t - c->first < (long long)(c->row + 1) * c->freq)
        return 0;
    *closed = c->row;
    c->row = (unsigned)((t - c->first) / c->freq);
    return 1;
}
```

- [x] **Step 4: Rewrite `perf/src/csvrow.h`**

```c
/* One second of timing as a CSV row, and the per-minute scope rows. Integer only. */
#ifndef PF_CSVROW_H
#define PF_CSVROW_H

#define PF_HAVE_LOGIC 1u
#define PF_HAVE_RENDER 2u
#define PF_HAVE_SKEL 4u
#define PF_HAVE_SCENE 8u
#define PF_HAVE_PRESENT 16u
#define PF_HAVE_SCOPES 32u
#define PF_HAVE_DRAWS 64u

/* Render-event labels with a column of their own, in column order. */
enum {
    PF_SCOPE_SHADOW, PF_SCOPE_REFLECT, PF_SCOPE_VIEWS, PF_SCOPE_UI, PF_SCOPE_TERRAIN,
    PF_SCOPE_WATER, PF_SCOPE_PARTICLES, PF_SCOPE_TREES, PF_SCOPE_SHRUBS,
    PF_SCOPE_DECAL_SHADOWS, PF_SCOPE_VOLUME_SHADOWS, PF_SCOPE_STATIC_SORT, PF_SCOPE_FX_MESH,
    PF_SCOPE_DX8_MESH, PF_SCOPE_COLUMNS
};
/* The frame-level markers, which count toward the frame's covered time. */
#define PF_SCOPE_IS_FRAME_LEVEL(c) ((unsigned)(c) <= PF_SCOPE_UI)

extern const char *const pf_scope_labels[PF_SCOPE_COLUMNS];

typedef struct {
    unsigned time_s;
    unsigned frames;
    unsigned long long frame_us, frame_max_us;
    unsigned long long logic_us, render_us;
    unsigned long long skel_render_us, skel_other_us;
    unsigned skel_render_calls; /* outermost skeleton updates during the render */
    unsigned to_workers, main_thread;
    unsigned long long vis_us, cust_us, flush_us, present_us;
    unsigned long long scope_us[PF_SCOPE_COLUMNS];
    unsigned draws;             /* in the whole row */
    unsigned long long rest_us;
    unsigned have;              /* PF_HAVE_*: columns that were measured */
} pf_row;

extern const char pf_csv_header[];
extern const char pf_scope_header[];

/* NUL- and "\r\n"-terminated row; columns not measured are empty. Returns
 * its length, or 0 if it doesn't fit in cap. */
unsigned pf_format_row(const pf_row *r, char *out, unsigned cap);

/* "time_s,label,calls per frame,ms per frame\r\n"; same return. */
unsigned pf_format_scope_row(unsigned time_s, const char *label, unsigned calls, unsigned frames,
                             unsigned long long us, char *out, unsigned cap);

#endif
```

- [x] **Step 5: Rewrite `perf/src/csvrow.c`** — `put_c`, `put_u`, `put_fixed` and `per_frame_ms` as in 0.2.0, plus:

```c
const char *const pf_scope_labels[PF_SCOPE_COLUMNS] = {
    "UpdateShadowMap", "UpdateWaterReflection", "RenderViews", "RenderUI", "RenderTerrain",
    "RenderWater", "RenderParticles", "RenderTrees", "RenderShrubs", "RenderDecalShadows",
    "RenderVolumeShadows", "RenderStaticSortLists", "MeshFXShader", "MeshDX8Render",
};

const char pf_csv_header[] =
    "time_s,frames,frame_avg_ms,frame_max_ms,logic_ms,render_ms,skel_render_ms,"
    "skel_other_ms,anim_on_screen,to_workers,main_thread,skel_us_per_obj,"
    "vis_ms,cust_ms,flush_ms,present_ms,shadow_ms,reflect_ms,views_ms,ui_ms,terrain_ms,"
    "water_ms,particles_ms,trees_ms,shrubs_ms,decal_shadows_ms,volume_shadows_ms,"
    "static_sort_ms,fx_mesh_ms,dx8_mesh_ms,draws,rest_ms\r\n";

const char pf_scope_header[] = "time_s,scope,calls_per_frame,ms_per_frame\r\n";

static void sep(out_buf *o)
{
    put_c(o, ',');
}

static void ms_field(out_buf *o, int have, unsigned long long us, unsigned frames)
{
    sep(o);
    if (have)
        per_frame_ms(o, us, frames);
}

static unsigned finish(out_buf *o, unsigned cap)
{
    put_c(o, '\r');
    put_c(o, '\n');
    if (o->full)
        return 0;
    *o->p = 0;
    return cap - o->left;
}

unsigned pf_format_row(const pf_row *r, char *out, unsigned cap)
{
    out_buf o = { out, cap, 0 };
    unsigned f = r->frames ? r->frames : 1, i;
    int skel = (r->have & PF_HAVE_SKEL) != 0, scene = (r->have & PF_HAVE_SCENE) != 0;

    if (cap == 0)
        return 0;
    put_u(&o, r->time_s);
    sep(&o);
    put_u(&o, r->frames);
    sep(&o);
    per_frame_ms(&o, r->frame_us, f);
    sep(&o);
    put_fixed(&o, r->frame_max_us, 3);
    ms_field(&o, (r->have & PF_HAVE_LOGIC) != 0, r->logic_us, f);
    ms_field(&o, (r->have & PF_HAVE_RENDER) != 0, r->render_us, f);
    ms_field(&o, skel, r->skel_render_us, f);
    ms_field(&o, skel, r->skel_other_us, f);
    sep(&o);
    if (skel)
        put_u(&o, (r->skel_render_calls + f / 2) / f);
    sep(&o);
    put_u(&o, r->to_workers);
    sep(&o);
    put_u(&o, r->main_thread);
    sep(&o);
    if (skel && r->skel_render_calls)
        put_fixed(&o, (r->skel_render_us * 100 + r->skel_render_calls / 2) / r->skel_render_calls,
                  2);
    ms_field(&o, scene, r->vis_us, f);
    ms_field(&o, scene, r->cust_us, f);
    ms_field(&o, scene, r->flush_us, f);
    ms_field(&o, (r->have & PF_HAVE_PRESENT) != 0, r->present_us, f);
    for (i = 0; i < PF_SCOPE_COLUMNS; i++)
        ms_field(&o, (r->have & PF_HAVE_SCOPES) != 0, r->scope_us[i], f);
    sep(&o);
    if (r->have & PF_HAVE_DRAWS)
        put_u(&o, (r->draws + f / 2) / f);
    ms_field(&o, 1, r->rest_us, f);
    return finish(&o, cap);
}

unsigned pf_format_scope_row(unsigned time_s, const char *label, unsigned calls, unsigned frames,
                             unsigned long long us, char *out, unsigned cap)
{
    out_buf o = { out, cap, 0 };
    unsigned f = frames ? frames : 1;

    if (cap == 0)
        return 0;
    put_u(&o, time_s);
    sep(&o);
    while (*label)
        put_c(&o, *label++);
    sep(&o);
    put_fixed(&o, ((unsigned long long)calls * 100 + f / 2) / f, 2);
    ms_field(&o, 1, us, f);
    return finish(&o, cap);
}
```

- [x] **Step 6: Run the tests to see them pass** — `OK: row tests passed` (the other suites still pass).
- [x] **Step 7: Commit** `feat(perf): row clock on whole seconds and the render breakdown columns`

### Task 2: the new sites, offsets and a stolen jmp

**Files:** rewrite `perf/src/sites.h`, `perf/src/sites.c`; modify `perf/src/patch.c`, `perf/tests/test_sites.c`, `perf/src/stubs.c` (12 stubs each), `perf/tests/test_hooks.c` (site table shape only).

**Interfaces:**
- Produces: `PF_VIS, PF_CUST, PF_FLUSH, PF_END_SCENE, PF_EVENT_BEGIN, PF_EVENT_END, PF_DRAW` (sites 5–11, `PF_SITE_COUNT` 12); `PF_GROUP_SCENE, PF_GROUP_PRESENT, PF_GROUP_EVENTS, PF_GROUP_DRAWS`; `pf_site { id, function, signature, site_offset, steal, arg_bytes, group, probe, rva_201 }`.

- [x] **Step 1: Update `perf/tests/test_sites.c`**: the prologue table becomes

```c
static const char *const g_prologue[PF_SITE_COUNT] = {
    "55 8B EC 51 53", "B8 DA 41 B8 00", "55 8B EC 83 EC 28", "83 EC 4C 53 55",
    "83 EC 30 56 8B F1", "B8 ?? ?? ?? ??", "55 8B EC 83 EC 0C", "B8 ?? ?? ?? ??",
    "64 A1 00 00 00 00", "8B 44 24 04 85 C0", "E9 ?? ?? ?? ??", "A1 ?? ?? ?? ??",
};
```

and the scan result is moved by the offset before the checks:

```c
        n = pf_scan(&img, &sig, &rva);
        rva += s->site_offset;
```

Also `printf("  %-11s …")` for the longer ids.

- [x] **Step 2: Run it to see it fail** — the table has 12 initialisers for 5 slots (`excess elements in array initializer`).

- [x] **Step 3: Rewrite `perf/src/sites.h`**

```c
/* The functions the timing hooks wrap or probe, found by signature. */
#ifndef PF_SITES_H
#define PF_SITES_H

enum {
    PF_FRAME, PF_LOGIC, PF_RENDER, PF_SKEL_HLOD, PF_SKEL_ANIM,
    PF_VIS, PF_CUST, PF_FLUSH, PF_END_SCENE, PF_EVENT_BEGIN, PF_EVENT_END, PF_DRAW,
    PF_SITE_COUNT
};

/* A group's hooks are installed together or not at all. */
enum {
    PF_GROUP_FRAME = 1, PF_GROUP_LOGIC, PF_GROUP_RENDER, PF_GROUP_SKEL, PF_GROUP_SCENE,
    PF_GROUP_PRESENT, PF_GROUP_EVENTS, PF_GROUP_DRAWS, PF_GROUP_COUNT
};

typedef struct {
    const char *id;
    const char *function;
    const char *signature;
    unsigned site_offset; /* the hooked instruction, from the signature's start */
    unsigned steal;       /* bytes run from the trampoline: whole, position-independent
                           * instructions, or a single jmp rel32 */
    unsigned arg_bytes;   /* bytes the function's ret pops */
    unsigned group;
    int probe;            /* 1: only seen at entry, never wrapped */
    unsigned rva_201;     /* ROTWK 2.01 and Age of the Ring; log and tests only */
} pf_site;

extern const pf_site pf_sites[PF_SITE_COUNT];

#endif
```

- [x] **Step 4: Rewrite `perf/src/sites.c`**

```c
#include "sites.h"

const pf_site pf_sites[PF_SITE_COUNT] = {
    { "frame", "GameEngine::update",
      "55 8B EC 51 53 56 8B F1 8B 0D ?? ?? ?? ?? 8B 01 FF 50 28 8B 0D ?? ?? ??",
      0, 5, 0, PF_GROUP_FRAME, 0, 0x002325A0 },
    { "logic", "GameLogic::update",
      "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 81 EC 74 01 00 00 53 56 8B F1 57 8D 4D D8",
      0, 5, 4, PF_GROUP_LOGIC, 0, 0x0022E4E8 },
    { "render", "RTS3DScene::Render",
      "55 8B EC 83 EC 28 53 56 8B 35 ?? ?? ?? ?? 85 F6 57 8B F9 89 7D E4 0F 84",
      0, 6, 4, PF_GROUP_RENDER, 0, 0x0007177E },
    { "skel_hlod", "HLodClass::Update_Sub_Object_Transforms",
      "83 EC 4C 53 55 56 57 8B D9 E8 ?? ?? ?? ?? 33 C9 39 8B 20 01 00 00 89 4C",
      0, 5, 0, PF_GROUP_SKEL, 0, 0x0019CD70 },
    { "skel_anim", "Animatable3DObjClass::Update_Sub_Object_Transforms",
      "83 EC 30 56 8B F1 8B 8E FC 00 00 00 85 C9 74 71 80 B9 F4 00 00 00 00 75",
      0, 6, 0, PF_GROUP_SKEL, 0, 0x001A5050 },
    { "vis", "RTS3DScene::Visibility_Check",
      "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 EC 60 53 56 8B F1 8D 9E BC 00 00 00 57",
      0, 5, 4, PF_GROUP_SCENE, 0, 0x00070B83 },
    { "cust", "RTS3DScene::Customized_Render",
      "55 8B EC 83 EC 0C 53 56 33 F6 57 8B F9 89 B7 F0 07 00 00 89 B7 F8 07 00",
      0, 6, 4, PF_GROUP_SCENE, 0, 0x0006FE84 },
    { "flush", "RTS3DScene::Flush",
      "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 81 EC A0 03 00 00 53 57 FF 75 08 8B F9 89",
      0, 5, 4, PF_GROUP_SCENE, 0, 0x00070F44 },
    { "present", "DX8Wrapper::End_Scene",
      "64 A1 00 00 00 00 6A FF 68 ?? ?? ?? ?? 50 64 89 25 00 00 00 00 53 33 DB",
      0, 6, 0, PF_GROUP_PRESENT, 0, 0x001225E0 },
    { "event_begin", "BFME2ScopedRenderEvent::BFME2ScopedRenderEvent",
      "8B 44 24 04 85 C0 56 8B F1 0F 84 ?? ?? ?? ?? 55 57 8B 3D ?? ?? ?? ?? 68",
      0, 6, 12, PF_GROUP_EVENTS, 1, 0x00117690 },
    { "event_end", "BFME2ScopedRenderEvent::~BFME2ScopedRenderEvent",
      "83 C4 08 5F 5D 8B C6 5E C2 0C 00 CC CC CC CC CC CC CC CC CC E9 ?? ?? ?? ?? CC CC",
      20, 5, 0, PF_GROUP_EVENTS, 1, 0x00117740 },
    { "draw", "BfmeDrawOps::Draw",
      "A1 ?? ?? ?? ?? 83 EC 10 85 C0 57 8B 7C 24 20 74 08 3B C7 0F 83 ?? ?? ??",
      0, 5, 0, PF_GROUP_DRAWS, 1, 0x0011FD40 },
};
```

- [x] **Step 5: `perf/src/patch.c`**: `locate` checks `s->site_offset + s->steal <= sig.len` and `sig.fixed[s->site_offset]`, and adds `site_offset` to a unique match; `trampoline` takes the site and, for a 5-byte steal starting with `E9`, jumps to the decoded target instead of copying:

```c
static unsigned read_le32(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static unsigned char *trampoline(const unsigned char *site, unsigned steal, const void *resume)
{
    ...pool as before...
    if (steal == 5 && site[0] == 0xE9) {
        /* A jmp rel32 can't run from elsewhere as is: go straight to its target. */
        resume = site + 5 + (int)read_le32(site + 1);
        steal = 0;
    }
    for (i = 0; i < steal; i++)
        t[i] = site[i];
    ...jmp to resume as before...
}

static void locate(const pf_image *img, const pf_site *s, pf_result *r)
{
    pf_pattern sig;
    if (!pf_parse_pattern(s->signature, &sig) || s->steal < 5 || s->steal > PF_MAX_STEAL ||
        s->site_offset + s->steal > sig.len || !sig.fixed[s->site_offset]) {
        r->status = PF_ST_BAD_DEF;
        return;
    }
    r->matches = pf_scan(img, &sig, &r->rva);
    if (r->matches == 1)
        r->rva += s->site_offset;
    r->status = r->matches == 0 ? PF_ST_NOT_FOUND
                : r->matches > 1 ? PF_ST_AMBIGUOUS
                                 : PF_ST_LOCATED;
}
```

- [x] **Step 6: `perf/src/stubs.c`**: `PF_STUB(enter, n)` and `PF_STUB(exit, n)` for n = 0…11, with the matching `extern char` declarations and the two 12-entry tables.

- [x] **Step 7: `perf/tests/test_hooks.c`**: give `g_sites` the new field order and 12 entries (sites 5–11 disabled for now with `enabled` 0 and signature `"CC"`), so the suite still builds and passes.

- [x] **Step 8: Run the tests to see them pass** — every site once, at its RVA, with its prologue, in both images.
- [x] **Step 9: Commit** `feat(perf): render breakdown sites, site offsets and a stolen jmp`

### Task 3: probes, markers, present and draws

**Files:** rewrite `perf/src/timing.h`, `perf/src/timing.c`, `perf/tests/test_hooks.c`; modify `perf/Makefile` (`test_hooks.exe` also builds `src/clock.c`).

**Interfaces:**
- Consumes: Tasks 1–2.
- Produces: `int pf_timing_open(const pf_site *, const WCHAR *csv, const WCHAR *scopes)`, `void pf_timing_discard(const WCHAR *csv, const WCHAR *scopes)`; `pf_timing_stats` gains `scopes, stale_scopes, draws, presents`.

Behaviour:
- `pf_enter` on the main thread: a probe records and continues (`draw`
  counts; `event_begin` pushes `{this = saved ECX (slot[-4]), label =
  slot[1], t0}` unless the label is null; `event_end` pops back to its
  `this`, dropping stale entries above it). A wrapped site wraps as in
  0.2.0, except that nesting is per site (both skeleton sites share one
  depth), `End_Scene` records whether its `flip` byte (`slot[1]`) is set,
  and `logic`, presented `End_Scene` and the four frame-level markers are
  *covering* when no other covering region is open.
- `rest = frame - covered`. A label's time also goes to its column, if it
  has one, and to the per-minute table (64 labels at most, 31 characters).
- The frame exit closes a row when `pf_clock_tick` says so and writes the
  scope table when a new minute starts (`time_s` = the minute's first second).

- [x] **Step 1: Write `perf/tests/test_hooks.c`**: the 0.2.0 assembly functions, plus:

```c
        /* frame: logic(7); marker("RenderViews") { render(9); draw; draw }; anim; present(1) */
        "_fake_frame:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    pushl %ecx\n"
        "    pushl %ebx\n"
        "    movl $0x5EED0001, %eax\n"
        "    pushl $7\n"
        "    call _fake_logic\n"
        "    pushl $0\n"
        "    pushl $0\n"
        "    pushl $_g_label_views\n"
        "    movl $_g_marker, %ecx\n"
        "    call _fake_event_begin\n"
        "    pushl $9\n"
        "    call _fake_render\n"
        "    call _fake_draw\n"
        "    call _fake_draw\n"
        "    movl $_g_marker, %ecx\n"
        "    call _fake_event_end\n"
        "    call _fake_anim\n"
        "    pushl $1\n"
        "    call _fake_present\n"
        "    addl $4, %esp\n"
        "    movl $41, %eax\n"
        "    popl %ebx\n"
        "    popl %ecx\n"
        "    popl %ebp\n"
        "    ret\n"
        "_fake_event_begin:\n"
        "    movl 4(%esp), %eax\n"
        "    testl %eax, %eax\n"
        "    movl $0x5EED0009, %eax\n"
        "    movl %ecx, %eax\n"
        "    ret $12\n"
        ".p2align 4\n"
        "_fake_end_ctx:\n"
        "    movl $0x5EED00EE, %eax\n"
        "    ret\n"
        "_fake_event_end:\n"
        "    .byte 0xE9\n"
        "    .long _fake_event_end_body - . - 4\n"
        "_fake_event_end_body:\n"
        "    movl $0x5EED000A, %eax\n"
        "    ret\n"
        "_fake_draw:\n"
        "    movl _g_marker, %eax\n"
        "    movl $0x5EED000B, %eax\n"
        "    ret\n"
        "_fake_present:\n"
        "    movl %fs:0, %eax\n"
        "    movl $0x5EED0008, %eax\n"
        "    ret\n"
```

with `const char g_label_views[] = "RenderViews"; char g_marker[16];` and
the site table

```c
static const pf_site g_sites[PF_SITE_COUNT] = {
    { "frame", "fake_frame", "55 89 E5 51 53 B8 01 00 ED 5E", 0, 5, 0, PF_GROUP_FRAME, 0, 0 },
    { "logic", "fake_logic", "B8 02 00 ED 5E 8B 44 24 04", 0, 5, 4, PF_GROUP_LOGIC, 0, 0 },
    { "render", "fake_render", "55 89 E5 83 EC 28 B8 03 00 ED 5E", 0, 6, 4, PF_GROUP_RENDER, 0, 0 },
    { "skel_hlod", "fake_hlod", "83 EC 4C 53 55 B8 04 00 ED 5E", 0, 5, 0, PF_GROUP_SKEL, 0, 0 },
    { "skel_anim", "fake_anim", "83 EC 30 56 89 CE B8 05 00 ED 5E", 0, 6, 0, PF_GROUP_SKEL, 0, 0 },
    { "vis", "unused", "CC", 0, 5, 4, PF_GROUP_SCENE, 0, 0 },
    { "cust", "unused", "CC", 0, 5, 4, PF_GROUP_SCENE, 0, 0 },
    { "flush", "unused", "CC", 0, 5, 4, PF_GROUP_SCENE, 0, 0 },
    { "present", "fake_present", "64 A1 00 00 00 00 B8 08 00 ED 5E", 0, 6, 0, PF_GROUP_PRESENT, 0, 0 },
    { "event_begin", "fake_event_begin", "8B 44 24 04 85 C0 B8 09 00 ED 5E", 0, 6, 12, PF_GROUP_EVENTS, 1, 0 },
    { "event_end", "fake_event_end", "B8 EE 00 ED 5E C3 E9 ?? ?? ?? ??", 6, 5, 0, PF_GROUP_EVENTS, 1, 0 },
    { "draw", "fake_draw", "A1 ?? ?? ?? ?? B8 0B 00 ED 5E", 0, 5, 0, PF_GROUP_DRAWS, 1, 0 },
};
```

Checks (sites 5–7 disabled): every enabled site applied; the 0.2.0 checks
(return values, registers, 3 frames, 3 logic, 3 render, skeleton 3 + 3,
5 passed through, no stale entries); `scopes == 3`, `stale_scopes == 0`,
`draws == 6`, `presents == 3`; after `pf_timing_flush`, in the row:
`frames` 3, `vis_ms` (field 12) empty, `present_ms` (15) and `views_ms`
(18) non-empty, `shadow_ms` (16) `0.000`, `draws` (30) `2`, `rest_ms` (31)
non-empty; the scopes CSV has a `RenderViews` row with `1.00` calls per
frame.

- [x] **Step 2: Run it to see it fail** — `too many arguments to function 'pf_timing_open'`.

- [x] **Step 3: Write `perf/src/timing.h` and `perf/src/timing.c`** as described above (full code in the commit).

- [x] **Step 4: Run the tests to see them pass** — `OK: hook tests passed`.
- [x] **Step 5: Commit** `feat(perf): time the game's render markers, present and draw calls`

### Task 4: 0.3.0 and install

**Files:** modify `perf/src/main.c` (version, `_scopes.csv` path, `pf_timing_open`/`pf_timing_discard` with both paths), `perf/src/version.rc` (0.3.0), `perf/tests/test_load.c` (0.3.0; the `event_end` line refused in a non-game host; no `bfme2perf_scopes.csv` left), `perf/Makefile` (`SRCS` has `src/clock.c`).

- [x] **Step 1:** Update `test_load.c` and run it to see it fail (`first line is not the version`).
- [x] **Step 2:** Update `main.c` and `version.rc`; `mingw32-make clean test`: every suite `OK`; imports only `KERNEL32.dll`; no x87 or SSE instruction in the DLL.
- [x] **Step 3:** Commit `feat(perf): render breakdown build 0.3.0`.
- [x] **Step 4:** `mingw32-make dist`; copy `dist/bfme2perf.asi` to `C:\Jogos\AgeoftheRing\rotwk\mss\`; copy `dist/bfme2perf.ini` there only if none exists.
- [ ] **Step 5:** The user plays `Test Replay` and the new big-battle replay; read `bfme2perf.log`, `bfme2perf_timing.csv` and `bfme2perf_scopes.csv`.
