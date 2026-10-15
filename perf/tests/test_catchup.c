/* Catch-up: the decision in a simulated game loop (the limiter as the game
 * runs it), the render site through the real stub on a fake frame function
 * (no-render path, what a skipped frame still runs, the limiter's wait), and
 * the notice's layout and its draw on a hardware D3D9 device.
 * Usage: test_catchup.exe */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <d3d9.h>

#include "catchup.h"
#include "d3d9vt.h"
#include "fps.h"
#include "lights.h"
#include "notice.h"
#include "patch.h"
#include "stubs.h"
#include "timing.h"

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

/* ------------------------------------------------------- the simulated loop */

typedef struct {
    unsigned pre_us;    /* logic, network, input and the client update before the render */
    unsigned render_us; /* the render: what a skip saves */
    unsigned after_us;  /* the UI update, audio, ... after the draw */
    unsigned spike_every, spike_us; /* every n-th frame's logic costs this much more */
} sim_load;

typedef struct {
    unsigned frames, drawn, skipped, max_in_row, cuts, longest_gap, shortest_gap;
    unsigned seconds, not_allowed; /* skips or cuts in frames that weren't allowed to */
    unsigned gaps_after_warmup[64], n_gaps;
} sim_result;

/* The game's frame loop on a microsecond clock, the limiter on the game's
 * millisecond clock: GameEngine::update (one logic phase, the client update,
 * the draw with its render, the UI), then `elapsed = now - start`; if under
 * the 33 ms frame time the loop calls Sleep(0) (where catch-up may end it)
 * until it isn't, and the next frame starts. `ok(frame)` says whether the
 * frame may skip (in game, not paused, ...). */
static void simulate(const sim_load *l, int catchup, unsigned max_skips, unsigned seconds,
                     int (*ok)(unsigned frame), sim_result *r)
{
    pf_cu_core c;
    unsigned long long t = 1000000ull * 1000; /* us */
    unsigned start = (unsigned)(t / 1000), frame = 0, in_row = 0, last_skip = 0;
    const unsigned period = 33;

    memset(r, 0, sizeof *r);
    r->shortest_gap = ~0u;
    pf_cu_core_init(&c, max_skips);
    while (t < 1000000ull * 1000 + 1000000ull * seconds) {
        unsigned now, pre = l->pre_us, skip = 0;
        int allowed = ok ? ok(frame) : 1;
        if (l->spike_every && frame % l->spike_every == 0)
            pre += l->spike_us;
        pf_cu_core_begin(&c, start, allowed);
        t += pre;
        if (catchup)
            skip = (unsigned)pf_cu_core_decide(&c, (unsigned)(t / 1000));
        r->not_allowed += skip && !allowed;
        if (!skip) {
            t += l->render_us;
            pf_cu_core_render(&c, l->render_us);
            r->drawn++;
            in_row = 0;
        } else {
            r->skipped++;
            in_row++;
            if (in_row > r->max_in_row)
                r->max_in_row = in_row;
            if (last_skip && r->skipped > 20 && r->n_gaps < 64)
                r->gaps_after_warmup[r->n_gaps++] = frame - last_skip;
            last_skip = frame;
        }
        t += l->after_us;
        pf_cu_core_after(&c, l->after_us);
        r->frames++;
        frame++;
        /* The limiter. */
        now = (unsigned)(t / 1000);
        if (now - start < period) {
            for (;;) {
                if (catchup && pf_cu_core_wait(&c, now, start, period)) {
                    r->cuts++;
                    r->not_allowed += !allowed;
                    break;
                }
                t += 100;
                now = (unsigned)(t / 1000);
                if (now - start >= period)
                    break;
            }
        }
        {
            unsigned gap = now - start;
            if (gap > r->longest_gap)
                r->longest_gap = gap;
            if (gap < r->shortest_gap)
                r->shortest_gap = gap;
        }
        start = now;
    }
    r->seconds = seconds;
}

static double per_s(unsigned n, unsigned seconds)
{
    return (double)n / seconds;
}

static unsigned g_menu_until;
static int ok_after_menu(unsigned frame)
{
    return frame >= g_menu_until;
}
static int ok_alternate_minutes(unsigned frame)
{
    return (frame / 300) % 2 == 0; /* 10 s in game, 10 s paused or in a menu */
}

static void test_simulated_loop(void)
{
    sim_load battle = { 12000, 22000, 2000, 0, 0 }; /* 36 ms drawn, 14 ms skipped */
    sim_load light = { 8000, 15000, 2000, 0, 0 };   /* 25 ms: on time */
    sim_load heavy = { 15000, 33000, 2000, 0, 0 };  /* 50 ms drawn */
    sim_load spiky = { 9000, 18000, 2000, 6, 25000 }; /* 29 ms, one 54 ms frame in six */
    sim_load user = { 13000, 23000, 1500, 6, 15000 }; /* the user's battle, roughly */
    sim_result off, on, r;
    unsigned i, lo = ~0u, hi = 0;

    printf("simulated loop: frames of 36 ms (12 before the render, 22 render, 2 after)\n");
    simulate(&battle, 0, 1, 60, NULL, &off);
    simulate(&battle, 1, 1, 60, NULL, &on);
    printf("  off: %.2f logic frames/s, %.2f drawn/s\n", per_s(off.frames, 60),
           per_s(off.drawn, 60));
    printf("  on:  %.2f logic frames/s, %.2f drawn/s, %u skipped, %u waits cut, at most %u in a "
           "row\n",
           per_s(on.frames, 60), per_s(on.drawn, 60), on.skipped, on.cuts, on.max_in_row);
    CHECK(per_s(off.frames, 60) < 28.0, "without catch-up the game runs slow: %.2f",
          per_s(off.frames, 60));
    CHECK(per_s(on.frames, 60) >= 30.0, "with catch-up the game keeps 30 logic frames/s: %.2f",
          per_s(on.frames, 60));
    CHECK(on.max_in_row == 1, "never two skips in a row: %u", on.max_in_row);
    CHECK(per_s(on.drawn, 60) >= 25.0, "only the skips needed: %.2f drawn/s",
          per_s(on.drawn, 60));
    /* Not oscillating: once settled, skips come at a steady spacing. */
    for (i = 0; i < on.n_gaps; i++) {
        if (on.gaps_after_warmup[i] < lo)
            lo = on.gaps_after_warmup[i];
        if (on.gaps_after_warmup[i] > hi)
            hi = on.gaps_after_warmup[i];
    }
    printf("  frames between skips once settled: %u..%u\n", lo, hi);
    CHECK(on.n_gaps > 10 && hi - lo <= 2 && lo >= 4, "skip spacing %u..%u over %u gaps", lo, hi,
          on.n_gaps);

    printf("simulated loop: frames that fit (25 ms)\n");
    simulate(&light, 1, 1, 30, NULL, &r);
    CHECK(r.skipped == 0 && r.cuts == 0 && r.longest_gap == 33 && r.shortest_gap == 33,
          "on time: %u skipped, %u cuts, frames %u..%u ms", r.skipped, r.cuts, r.shortest_gap,
          r.longest_gap);

    printf("simulated loop: frames of 50 ms\n");
    simulate(&heavy, 0, 1, 60, NULL, &off);
    simulate(&heavy, 1, 1, 60, NULL, &on);
    printf("  off %.2f, on %.2f logic frames/s (%.2f drawn/s, at most %u in a row)\n",
           per_s(off.frames, 60), per_s(on.frames, 60), per_s(on.drawn, 60), on.max_in_row);
    CHECK(on.max_in_row == 1 && on.skipped <= on.drawn, "every other frame at most: %u of %u",
          on.skipped, on.frames);
    CHECK(per_s(on.frames, 60) > per_s(off.frames, 60) + 6.0, "faster than without: %.2f vs %.2f",
          per_s(on.frames, 60), per_s(off.frames, 60));
    simulate(&heavy, 1, 2, 60, NULL, &r);
    printf("  MaxSkips=2: %.2f logic frames/s, %.2f drawn/s, at most %u in a row\n",
           per_s(r.frames, 60), per_s(r.drawn, 60), r.max_in_row);
    CHECK(r.max_in_row == 2 && per_s(r.frames, 60) >= 30.0, "MaxSkips=2: %u in a row, %.2f",
          r.max_in_row, per_s(r.frames, 60));

    printf("simulated loop: one heavy logic frame in six (29 ms, 54 ms)\n");
    simulate(&spiky, 0, 1, 60, NULL, &off);
    simulate(&spiky, 1, 1, 60, NULL, &on);
    printf("  off %.2f, on %.2f logic frames/s, %.2f drawn/s\n", per_s(off.frames, 60),
           per_s(on.frames, 60), per_s(on.drawn, 60));
    CHECK(per_s(on.frames, 60) >= 30.0 && on.max_in_row == 1, "spikes caught up: %.2f",
          per_s(on.frames, 60));

    printf("simulated loop: the user's battle (13 + 23 + 1.5 ms, +15 ms one frame in six)\n");
    simulate(&user, 0, 1, 60, NULL, &off);
    simulate(&user, 1, 1, 60, NULL, &on);
    printf("  off: %.2f logic frames/s (%.0f%% speed), %.2f drawn/s\n", per_s(off.frames, 60),
           per_s(off.frames, 60) * 100 / 30, per_s(off.drawn, 60));
    printf("  on:  %.2f logic frames/s (%.0f%% speed), %.2f drawn/s\n", per_s(on.frames, 60),
           per_s(on.frames, 60) * 100 / 30, per_s(on.drawn, 60));
    CHECK(per_s(on.frames, 60) >= 30.0, "user's battle at full speed: %.2f", per_s(on.frames, 60));

    printf("simulated loop: menus, pause and extra frames never skip\n");
    g_menu_until = 1u << 30;
    simulate(&battle, 1, 1, 30, ok_after_menu, &r);
    CHECK(r.skipped == 0 && r.cuts == 0, "never allowed: %u skipped, %u cuts", r.skipped, r.cuts);
    CHECK(per_s(r.frames, 30) < 28.0, "the game's own pace there: %.2f", per_s(r.frames, 30));
    simulate(&battle, 1, 1, 60, ok_alternate_minutes, &r);
    CHECK(r.skipped > 0 && r.not_allowed == 0,
          "10 s allowed, 10 s not: %u skipped, %u skips or cuts where not allowed", r.skipped,
          r.not_allowed);

    printf("simulated loop: a stall and a long frame\n");
    {
        pf_cu_core c;
        pf_cu_core_init(&c, 1);
        pf_cu_core_begin(&c, 1000, 1);
        pf_cu_core_begin(&c, 1033, 1);
        CHECK(c.debt == 0, "on time: debt %u", c.debt);
        pf_cu_core_begin(&c, 1033 + 150, 1);
        CHECK(c.debt == 99, "a 150 ms frame: behind %u ms (capped at 3 frames)", c.debt);
        pf_cu_core_begin(&c, 1183 + 400, 1);
        CHECK(c.debt == 0, "after a 400 ms stall the schedule restarts: %u", c.debt);
        pf_cu_core_begin(&c, 1583 + 40, 0);
        CHECK(c.debt == 0 && !pf_cu_core_behind(&c), "not allowed: no debt");
        pf_cu_core_begin(&c, 1623 + 40, 1);
        CHECK(c.debt == 7, "allowed again: the last frame's 7 ms count: %u", c.debt);
        CHECK(!pf_cu_core_wait(&c, 1663 + 25, 1663, 33) && pf_cu_core_wait(&c, 1663 + 26, 1663, 33),
              "the wait ends at the schedule (26 ms into a frame 7 ms late)");
        while (c.samples < PF_CU_MIN_SAMPLES)
            pf_cu_core_render(&c, 22000);
        pf_cu_core_after(&c, 2000);
        CHECK(!pf_cu_core_decide(&c, 1663 + 12), "7 ms behind: draw (less than a render's cost)");
        pf_cu_core_begin(&c, 1663 + 50, 1);
        CHECK(c.debt == 24 && pf_cu_core_decide(&c, 1713 + 12) && !pf_cu_core_decide(&c, 1713 + 13),
              "24 ms behind: skip, once per frame (debt %u)", c.debt);
        pf_cu_core_begin(&c, 1713 + 14, 1);
        CHECK(!pf_cu_core_decide(&c, 1727 + 30), "never two in a row");
    }
}

/* ------------------------------------------------- the site through the stub */

int g_cu_rendered, g_cu_norendered, g_cu_logic_global = 0x1234;
unsigned char g_cu_byte;
/* push ebx; xor ebx, ebx; a marker; mov eax, [TheGameLogic] (the site);
 * the render; ...; the no-render path: cmp [byte], bl. Returns EBX. */
__asm__(".text\n"
        ".p2align 4\n"
        "_fake_cu_frame:\n"
        "    pushl %ebx\n"
        "    xorl %ebx, %ebx\n"
        "    movl $0x5EED00C1, %eax\n"
        "    movl _g_cu_logic_global, %eax\n"
        "    cmpl $0x1234, %eax\n"
        "    jne 2f\n"
        "    incl _g_cu_rendered\n"
        "    jmp 1f\n"
        "_fake_cu_no_render:\n"
        "    cmpb %bl, _g_cu_byte\n"
        "    incl _g_cu_norendered\n"
        "1:  movl %ebx, %eax\n"
        "    popl %ebx\n"
        "    ret\n"
        "2:  movl $-1, %eax\n"
        "    popl %ebx\n"
        "    ret\n");
extern char fake_cu_frame[], fake_cu_no_render[];
typedef int (*int_fn)(void);

static unsigned g_now_ms, g_start_ms;
static unsigned char g_limit_on = 1, g_proj, g_shadow;
static unsigned char g_logic_obj[0x200], g_client_obj[0x100], g_ui_obj[0x800];
static unsigned char *g_logic_ptr = g_logic_obj, *g_client_ptr = g_client_obj;
static void *g_ui_ptr = g_ui_obj;
static int g_screens, g_in_game = 1, g_ui_anims, g_updates[4], g_terrain_updates;
static DWORD __stdcall fake_time(void) { return g_now_ms; }
static void *g_time_slot = (void *)fake_time;
static unsigned char __attribute__((thiscall)) fake_in_game(void *self)
{
    return self == g_logic_obj && g_in_game;
}
static void __attribute__((thiscall)) fake_ui_anim(void *self)
{
    g_ui_anims += self == g_ui_obj;
}

/* Render objects in the scene's update list: three units and the terrain. */
typedef struct {
    void **vt;
    int id;
    unsigned char pad[0x40];
} fake_robj;
static fake_robj g_objs[4];
static int __attribute__((thiscall)) fake_class_id(fake_robj *o)
{
    return o->id == 3 ? 4 : 0x19;
}
static void __attribute__((thiscall)) fake_on_frame_update(fake_robj *o)
{
    if (o->id == 3)
        g_terrain_updates++;
    else
        g_updates[o->id]++;
}
static void *g_robj_vt[16];
typedef struct node {
    struct node *prev, *next;
    void *pad;
    unsigned char *iface;
} node;
static node g_nodes[4];
static unsigned char g_scene[0x100];
static unsigned char *g_scene_ptr = g_scene;
static unsigned g_extras_last;
static unsigned extras_in(unsigned frame_start_ms)
{
    (void)frame_start_ms;
    return g_extras_last;
}

static pf_site g_sites[PF_SITE_COUNT];

static void game_frame(unsigned elapsed_ms, int *rendered)
{
    int before = g_cu_rendered, ebx;
    g_start_ms += 36;
    pf_cu_frame_edge(1);
    g_now_ms = g_start_ms + elapsed_ms;
    pf_cu_draw_edge(1);
    ebx = ((int_fn)(void *)fake_cu_frame)();
    CHECK(ebx == 0, "EBX after the frame function: %d", ebx);
    pf_cu_draw_edge(0);
    pf_cu_frame_edge(0);
    *rendered = g_cu_rendered > before;
}

static void test_site(void)
{
    pf_image img;
    pf_result res[PF_SITE_COUNT];
    int enabled[PF_SITE_COUNT];
    pf_cu_sites s;
    pf_cu_settings set = { 1, 1, 0, "Catching up", 6 };
    unsigned i, skips = 0, draws = 0, in_row = 0, max_row = 0;
    void *stack[16], **slot = stack + 12;

    printf("render site through the stub\n");
    for (i = 0; i < PF_SITE_COUNT; i++) {
        g_sites[i].id = "unused";
        g_sites[i].function = "unused";
        g_sites[i].signature = "CC";
        g_sites[i].steal = 5;
        g_sites[i].group = PF_GROUP_COUNT;
        enabled[i] = 0;
    }
    g_sites[PF_CU_RENDER].id = "catchup_render";
    g_sites[PF_CU_RENDER].function = "fake_cu_frame";
    g_sites[PF_CU_RENDER].signature = "31 DB B8 C1 00 ED 5E A1 ?? ?? ?? ??";
    g_sites[PF_CU_RENDER].site_offset = 7;
    g_sites[PF_CU_RENDER].group = PF_GROUP_CATCHUP;
    g_sites[PF_CU_RENDER].probe = 1;
    enabled[PF_CU_RENDER] = 1;
    CHECK(((int_fn)(void *)fake_cu_frame)() == 0 && g_cu_rendered == 1, "unhooked");
    CHECK(pf_image_open(&img, GetModuleHandleW(NULL)), "no .text");
    CHECK(pf_timing_open(g_sites, NULL, NULL), "timing");
    pf_install(&img, g_sites, PF_SITE_COUNT, enabled, 0, pf_enter_stubs, pf_timing_set_next, res);
    CHECK(res[PF_CU_RENDER].status == PF_ST_APPLIED, "site: %s",
          pf_status_name(res[PF_CU_RENDER].status));

    /* The scene's update list: sentinel at +0x78, first at +0x7C. */
    for (i = 0; i < 16; i++)
        g_robj_vt[i] = 0;
    g_robj_vt[PF_ROBJ_CLASS_ID / 4] = (void *)fake_class_id;
    g_robj_vt[PF_ROBJ_ON_FRAME_UPDATE / 4] = (void *)fake_on_frame_update;
    for (i = 0; i < 4; i++) {
        g_objs[i].vt = g_robj_vt;
        g_objs[i].id = (int)i;
        g_nodes[i].iface = (unsigned char *)&g_objs[i] + PF_NODE_OBJ_ADJUST;
        g_nodes[i].next = i < 3 ? &g_nodes[i + 1] : (node *)(g_scene + PF_SCENE_UPDATE_LIST);
    }
    *(node **)(g_scene + PF_SCENE_UPDATE_FIRST) = &g_nodes[0];
    _Static_assert(offsetof(node, next) == PF_NODE_NEXT && offsetof(node, iface) == PF_NODE_OBJ,
                   "fake node layout");

    memset(&s, 0, sizeof s);
    s.no_render = (const unsigned char *)fake_cu_no_render;
    s.scene = &g_scene_ptr;
    s.proj_override = &g_proj;
    s.shadow_flag = &g_shadow;
    s.ui_anim = (void *)fake_ui_anim;
    s.frame_start = &g_start_ms;
    s.limit_on = &g_limit_on;
    s.time_import = &g_time_slot;
    s.client = &g_client_ptr;
    s.client_running = 0xC8;
    s.logic = &g_logic_ptr;
    s.logic_paused = 0x125;
    s.logic_frame = 0x40;
    s.is_in_game = (void *)fake_in_game;
    s.screenshots = &g_screens;
    s.ui.ui = &g_ui_ptr;
    s.ui.pending_off = 0x53C;
    s.ui.scroll_off = 0x7F8;
    g_client_obj[0xC8] = 1;
    pf_cu_init(&set, &s);
    pf_cu_set_extras(extras_in);
    g_start_ms = 500000;

    /* 36 ms frames (render 22 ms, measured from the site). */
    for (i = 0; i < 200; i++) {
        int rendered;
        game_frame(12, &rendered);
        if (rendered) {
            draws++;
            in_row = 0;
        } else {
            skips++;
            in_row++;
            max_row = in_row > max_row ? in_row : max_row;
        }
    }
    printf("  36 ms frames, no limiter: %u drawn, %u skipped, at most %u in a row\n", draws, skips,
           max_row);
    CHECK(skips > 50 && max_row == 1 && g_cu_rendered > 0, "frames through the stub: %u skipped",
          skips);
    /* One frame at a time, with the decision's state set. */
    {
        unsigned before_skip = (unsigned)g_cu_norendered, u0 = (unsigned)g_updates[0],
                 t0 = (unsigned)g_terrain_updates, a0 = (unsigned)g_ui_anims;
        int rendered;
        pf_cu_row row;
        pf_cu_take_row(&row);
        /* Behind by 40 ms with a 22 ms render expected: skip. */
        g_start_ms += 40;
        pf_cu_frame_edge(1);
        g_now_ms = g_start_ms + 12;
        pf_cu_draw_edge(1);
        {
            pf_cu_core *c = pf_cu_core_state();
            c->render_us = 22000;
            c->after_us = 2000;
            c->samples = PF_CU_MIN_SAMPLES;
            c->in_row = 0;
            c->sched = g_start_ms - 40;
            c->debt = 40;
        }
        CHECK(((int_fn)(void *)fake_cu_frame)() == 0, "EBX kept on the no-render path");
        pf_cu_draw_edge(0);
        CHECK((unsigned)g_cu_norendered == before_skip + 1, "skipped: the no-render path ran");
        CHECK((unsigned)g_updates[0] == u0 + 1 && g_updates[1] == g_updates[0] &&
                  g_updates[2] == g_updates[0] && (unsigned)g_terrain_updates == t0,
              "a skipped frame updates the scene's objects once, not the terrain (%d %d %d, "
              "terrain %d)",
              g_updates[0], g_updates[1], g_updates[2], g_terrain_updates);
        CHECK((unsigned)g_ui_anims == a0 + 1, "and the UI's animations once");
        /* Its wait ends at the schedule: 40 ms behind, so at once. */
        slot[-9] = (void *)33;
        g_now_ms = g_start_ms + 20;
        pf_cu_wait(slot);
        CHECK(slot[-9] == 0, "the limiter's wait is ended (ESI = 0)");
        pf_cu_frame_edge(0);
        /* The next frame draws (never two in a row), its objects aren't
         * touched by the plugin. */
        u0 = (unsigned)g_updates[0];
        g_start_ms += 15;
        pf_cu_frame_edge(1);
        g_now_ms = g_start_ms + 12;
        pf_cu_draw_edge(1);
        rendered = g_cu_rendered;
        ((int_fn)(void *)fake_cu_frame)();
        CHECK(g_cu_rendered == rendered + 1 && (unsigned)g_updates[0] == u0,
              "the next frame renders, the game updates its objects itself");
        pf_cu_draw_edge(0);
        pf_cu_frame_edge(0);
        pf_cu_take_row(&row);
        CHECK(row.skipped == 1 && row.frames == 2 && row.waits_cut == 1,
              "row: %u skipped, %u frames, %u waits cut", row.skipped, row.frames, row.waits_cut);
    }

    printf("the logic frame counter: its steps per row, read only, from 0 at a new game\n");
    {
        pf_cu_row row;
        unsigned *counter = (unsigned *)(g_logic_obj + 0x40), k;
        pf_cu_take_row(&row);
        *counter = 1000;
        pf_cu_frame_edge(1);
        pf_cu_frame_edge(0);
        pf_cu_take_row(&row);
        for (k = 0; k < 12; k++) {
            *counter += k % 6 == 5; /* one logic frame per 6 game frames */
            g_start_ms += 33;
            pf_cu_frame_edge(1);
            pf_cu_frame_edge(0);
        }
        *counter = 3; /* a new game */
        pf_cu_frame_edge(1);
        pf_cu_frame_edge(0);
        pf_cu_take_row(&row);
        CHECK(row.ticks_on && row.logic_ticks == 5 && *counter == 3,
              "logic ticks %u (want 2 + 3 after the reset), counter %u", row.logic_ticks,
              *counter);
        *counter = 0;
    }

    printf("not allowed: paused, frozen time, not in game, screenshot, extra frames, no limiter\n");
    {
        unsigned char *flags[6] = { &g_logic_obj[0x125], &g_client_obj[0xC8], NULL, NULL, NULL,
                                    &g_limit_on };
        unsigned k;
        for (k = 0; k < 6; k++) {
            int before = g_cu_norendered;
            pf_cu_core *c;
            if (k == 0)
                *flags[0] = 1;
            if (k == 1)
                *flags[1] = 0;
            if (k == 2)
                g_in_game = 0;
            if (k == 3)
                g_screens = 1;
            if (k == 4)
                g_extras_last = 2;
            if (k == 5)
                g_limit_on = 0;
            g_start_ms += 60;
            pf_cu_frame_edge(1);
            g_now_ms = g_start_ms + 12;
            pf_cu_draw_edge(1);
            c = pf_cu_core_state();
            c->samples = PF_CU_MIN_SAMPLES;
            c->render_us = 22000;
            c->in_row = 0;
            c->sched = g_start_ms - 60;
            c->debt = 60;
            ((int_fn)(void *)fake_cu_frame)();
            pf_cu_draw_edge(0);
            slot[-9] = (void *)33;
            pf_cu_wait(slot);
            CHECK(g_cu_norendered == before && slot[-9] == (void *)33,
                  "case %u: skipped or the wait was cut", k);
            pf_cu_frame_edge(0);
            g_logic_obj[0x125] = 0;
            g_client_obj[0xC8] = 1;
            g_in_game = 1;
            g_screens = 0;
            g_extras_last = 0;
            g_limit_on = 1;
        }
        /* Placing a building and scrolling. */
        for (k = 0; k < 2; k++) {
            int before = g_cu_norendered;
            pf_cu_core *c;
            if (k == 0)
                *(void **)(g_ui_obj + 0x53C) = g_ui_obj;
            else
                g_ui_obj[0x7F8] = 1;
            g_start_ms += 60;
            pf_cu_frame_edge(1);
            g_now_ms = g_start_ms + 12;
            pf_cu_draw_edge(1);
            c = pf_cu_core_state();
            c->samples = PF_CU_MIN_SAMPLES;
            c->render_us = 22000;
            c->in_row = 0;
            c->sched = g_start_ms - 60;
            ((int_fn)(void *)fake_cu_frame)();
            pf_cu_draw_edge(0);
            pf_cu_frame_edge(0);
            CHECK(g_cu_norendered == before, "%s: skipped", k ? "scrolling" : "placing");
            *(void **)(g_ui_obj + 0x53C) = 0;
            g_ui_obj[0x7F8] = 0;
        }
    }
}

/* ------------------------------------- a skipped render and the light cuts */

/* One directional light (colour at +0xE0, sphere at +0x4C) in the scene's
 * light list, none in the dynamic one; Add_Light folds the colour in. */
static struct {
    unsigned char b[0x160];
} g_lt_light;
static void *g_lt_vt[0x110 / 4];
static struct {
    unsigned pad0;
    void *next;
    unsigned pad8;
    unsigned obj8;
} g_lt_node;
static unsigned char g_lt_scene[0x200], g_lt_frame[0x400], g_lt_markers[4];
static unsigned g_lt_robj[4];
static volatile int g_lt_threshold;
static int g_lt_sphere_reads;

static const unsigned *__attribute__((thiscall)) lt_bsphere(void *obj)
{
    g_lt_sphere_reads++;
    return (const unsigned *)((unsigned char *)obj + 0x4C);
}
static void __attribute__((thiscall)) lt_add(void *env, void *obj)
{
    unsigned *e = env;
    e[1] = e[1] * 31 + *(unsigned *)((unsigned char *)obj + 0xE0);
}
static unsigned char lt_spheres(const void *a, const void *b)
{
    (void)a;
    (void)b;
    return 1;
}

/* One object's light lists as the plugin decides them; its environment's
 * dword 1 after. */
static unsigned lt_object(void)
{
    void *stack[16], **slot = stack + 12;
    unsigned char *ebp = g_lt_frame + 0x300;
    memset(g_lt_frame, 0, sizeof g_lt_frame);
    *(void **)(ebp + 0xC) = g_lt_robj;
    slot[-8] = ebp;
    slot[-9] = g_lt_scene;
    CHECK(pf_lt_enter(PF_ROO_LISTS, slot, g_lt_markers) == g_lt_markers + 1,
          "the plugin decides the lists");
    return ((unsigned *)(ebp + PF_LT_F_ENV))[1];
}

/* A render skipped through the stub drops LightCull's snapshot: the light
 * the skip's update walk changed isn't decided, or reused, as it was. */
static void test_skip_lights(void)
{
    static volatile unsigned char flag;
    pf_lt_data d;
    unsigned before = (unsigned)g_cu_norendered, colour;
    int reads;

    printf("a skipped render and the light snapshot\n");
    memset(&d, 0, sizeof d);
    d.lists = g_lt_markers + 3;
    d.lists_after = g_lt_markers + 1;
    d.lists_default = g_lt_markers + 2;
    d.add_light = (void *)lt_add;
    d.spheres_intersect = (void *)lt_spheres;
    d.bsphere = (void *)lt_bsphere;
    d.threshold = &g_lt_threshold;
    g_lt_vt[PF_LT_BSPHERE_SLOT / 4] = (void *)lt_bsphere;
    *(void ***)g_lt_light.b = g_lt_vt;
    *(int *)(g_lt_light.b + PF_LT_LIGHT_TYPE) = 1;
    *(unsigned *)(g_lt_light.b + 0xE0) = 5;
    g_lt_node.obj8 = (unsigned)(UINT_PTR)g_lt_light.b + 8;
    g_lt_node.next = g_lt_scene + PF_LT_SCENE_LIGHTS;
    *(void **)(g_lt_scene + PF_LT_SCENE_LIGHTS + 4) = &g_lt_node;
    *(void **)(g_lt_scene + PF_LT_SCENE_DYNAMIC + 4) = g_lt_scene + PF_LT_SCENE_DYNAMIC;
    pf_lt_init(&d, &flag, 0, 1, 1, 0);
    pf_lt_set_installed(1, 1);

    pf_lt_pass_begin();
    CHECK(lt_object() == 5 && lt_object() == 5, "a drawn frame: the light's colour");
    reads = g_lt_sphere_reads;
    *(unsigned *)(g_lt_light.b + 0xE0) = 6; /* the skip's walk fades it */

    g_start_ms += 40;
    pf_cu_frame_edge(1);
    g_now_ms = g_start_ms + 12;
    pf_cu_draw_edge(1);
    {
        pf_cu_core *c = pf_cu_core_state();
        c->render_us = 22000;
        c->after_us = 2000;
        c->samples = PF_CU_MIN_SAMPLES;
        c->in_row = 0;
        c->sched = g_start_ms - 40;
        c->debt = 40;
    }
    ((int_fn)(void *)fake_cu_frame)();
    pf_cu_draw_edge(0);
    pf_cu_frame_edge(0);
    CHECK((unsigned)g_cu_norendered == before + 1, "the render wasn't skipped");

    /* The next object, before any pass start. */
    colour = lt_object();
    CHECK(colour == 6 && g_lt_sphere_reads == reads + 1,
          "after the skip: colour %u (want 6), %d new snapshot(s)", colour,
          g_lt_sphere_reads - reads);
    pf_lt_init(&d, &flag, 0, 0, 0, 0);
}

/* ---------------------------------------------------------------- the notice */

/* Skips every `every` ms from `t` until `end` (exclusive), the notice asked
 * at every 33 ms frame in between; how many frames showed it, and whether it
 * went off and on again (a flicker). */
static unsigned notice_run(pf_cu_notice *n, unsigned t, unsigned end, unsigned every,
                           unsigned *flickers)
{
    unsigned shown = 0, next = t, offs = 0;
    int was = pf_cu_notice_shown(n, t);
    for (; (int)(end - t) > 0; t += 33) {
        int now;
        if (every && (int)(t - next) >= 0) {
            pf_cu_notice_skip(n, t);
            next += every;
        }
        now = pf_cu_notice_shown(n, t);
        shown += (unsigned)now;
        if (was && !now)
            offs++;
        if (!was && now && offs && flickers)
            (*flickers)++;
        was = now;
    }
    return shown;
}

static void test_notice_window(void)
{
    pf_cu_notice n;
    unsigned f = 0, t0, k;

    printf("notice: only when catching up is sustained\n");
    pf_cu_notice_init(&n, 6);
    CHECK(notice_run(&n, 1000, 31000, 1000, &f) == 0, "a skip a second for 30 s showed it");
    CHECK(notice_run(&n, 40000, 60000, 2500, &f) == 0, "a skip every 2.5 s showed it");
    pf_cu_notice_skip(&n, 70000);
    pf_cu_notice_skip(&n, 70066);
    CHECK(!pf_cu_notice_shown(&n, 70100), "one stutter's two skips showed it");

    /* Every other frame (MaxSkips=1 at its limit): on at the 6th skip. */
    pf_cu_notice_init(&n, 6);
    for (k = 0; k < 6; k++) {
        CHECK(!pf_cu_notice_shown(&n, 100000 + 66 * k), "shown before the %uth skip", k + 1);
        pf_cu_notice_skip(&n, 100000 + 66 * k);
    }
    CHECK(pf_cu_notice_shown(&n, 100000 + 66 * 5), "not shown after 6 skips in 330 ms");
    /* Fewer, a skip every 500 ms (4 in 2 s, over half of 6): kept, no flicker. */
    f = 0;
    CHECK(notice_run(&n, 100400, 110400, 500, &f) == (110400 - 100400 + 32) / 33 && f == 0,
          "with a skip every 500 ms it went off (%u flickers)", f);
    /* A skip every 1.5 s (2 in 2 s at most): off 2 s after the last skip that
     * kept it (the window still holds the faster ones for a moment), and
     * stays off. */
    t0 = 110400;
    f = 0;
    k = notice_run(&n, t0, t0 + 20000, 1500, &f);
    CHECK(k > 0 && k * 33 <= 3700 && f == 0 && !pf_cu_notice_shown(&n, t0 + 20000),
          "after slowing to a skip every 1.5 s: shown %u ms more, %u flickers", k * 33, f);

    /* Skips about the threshold: no flicker. */
    pf_cu_notice_init(&n, 6);
    f = 0;
    k = notice_run(&n, 200000, 230000, 350, &f);
    CHECK(k > 25000 / 33 && f == 0, "a skip every 350 ms: shown %u frames, %u flickers", k, f);

    /* NoticeMinSkips=1 (and 0): every skip shows it for 2 s, as before. */
    for (k = 0; k <= 1; k++) {
        pf_cu_notice_init(&n, k);
        pf_cu_notice_skip(&n, 300000);
        CHECK(pf_cu_notice_shown(&n, 300000) && pf_cu_notice_shown(&n, 301999) &&
                  !pf_cu_notice_shown(&n, 302000),
              "NoticeMinSkips=%u: a single skip isn't shown for 2 s", k);
    }
    pf_cu_notice_init(&n, 99);
    CHECK(n.min_skips == PF_CU_NOTICE_MAX_SKIPS, "99 -> %u", n.min_skips);

    /* The game's clock wraps. */
    pf_cu_notice_init(&n, 6);
    for (k = 0; k < 6; k++)
        pf_cu_notice_skip(&n, 0xFFFFFF00u + 66 * k);
    CHECK(pf_cu_notice_shown(&n, 0xFFFFFF00u + 66 * 5) && pf_cu_notice_shown(&n, 1000) &&
              !pf_cu_notice_shown(&n, 2500),
          "across the clock's wrap");
}

static void test_notice_layout(void)
{
    static pf_notice_rect r[PF_NOTICE_MAX_RECTS];
    unsigned n, i, c;

    printf("notice layout\n");
    for (c = 33; c < 127; c++) {
        const unsigned char *g = pf_notice_glyph((char)c);
        unsigned any = 0, row;
        for (row = 0; row < 7; row++)
            any |= g[row];
        CHECK(any && !(any & ~0x1Fu), "glyph %c empty or wider than 5", (char)c);
    }
    CHECK(pf_notice_glyph((char)0xE7) == pf_notice_glyph('?'), "non-ASCII drawn as '?'");
    n = pf_notice_layout("Catching up", 1920, 1080, r, PF_NOTICE_MAX_RECTS);
    printf("  1080p: %u rectangles, box %ld,%ld..%ld,%ld\n", n, r[0].x1, r[0].y1, r[0].x2, r[0].y2);
    CHECK(n > 20, "rectangles: %u", n);
    CHECK(r[0].x2 - r[0].x1 == 11 * 12 - 2 + 8 && r[0].y2 - r[0].y1 == 14 + 8 &&
              r[0].x2 == 1920 - 36 && r[0].y1 == 36,
          "box at 1080p: top right, 36 from the top and the right");
    for (i = 1; i < n; i++)
        CHECK(r[i].x1 >= r[0].x1 && r[i].x2 <= r[0].x2 && r[i].y1 >= r[0].y1 &&
                  r[i].y2 <= r[0].y2 && r[i].x2 > r[i].x1 && r[i].y2 > r[i].y1,
              "rect %u outside the box", i);
    n = pf_notice_layout("Catching up", 3840, 2160, r, PF_NOTICE_MAX_RECTS);
    CHECK(n && r[0].y2 - r[0].y1 == 28 + 16 && r[0].x2 == 3840 - 72 && r[0].y1 == 72,
          "4x at 2160p, top right");
    n = pf_notice_layout("Recuperando atraso...", 1280, 720, r, PF_NOTICE_MAX_RECTS);
    printf("  720p: box %ld,%ld..%ld,%ld\n", r[0].x1, r[0].y1, r[0].x2, r[0].y2);
    CHECK(n && r[0].x2 - r[0].x1 == 21 * 6 - 1 + 4 && r[0].x2 == 1280 - 24 && r[0].y1 == 24,
          "1x at 720p, top right");
    {
        static const char long_text[] = "01234567890123456789012345678901234567890123...";
        long w1 = (long)strlen(long_text) * 6 - 1 + 4;
        n = pf_notice_layout(long_text, 400, 1080, r, PF_NOTICE_MAX_RECTS);
        CHECK(n && r[0].x2 - r[0].x1 == w1 && r[0].x2 == 400 - 36 && r[0].x1 >= 0,
              "too wide at 2x: drawn at 1x, inside the target");
        n = pf_notice_layout(long_text, 300, 1080, r, PF_NOTICE_MAX_RECTS);
        CHECK(n && r[0].x1 == 300 - w1 && r[0].x2 == 300,
              "no room for the margin: against the right edge");
    }
    for (i = 0; i < 4; i++) {
        static const unsigned sizes[4][2] = { { 640, 480 }, { 1024, 768 }, { 2560, 1080 },
                                              { 1366, 768 } };
        n = pf_notice_layout("Recuperando atraso...", sizes[i][0], sizes[i][1], r,
                             PF_NOTICE_MAX_RECTS);
        CHECK(n && r[0].x1 >= 0 && r[0].y1 >= 0 && r[0].x2 <= (long)sizes[i][0] &&
                  r[0].y2 <= (long)sizes[i][1] && r[0].x2 > (long)sizes[i][0] * 9 / 10,
              "%ux%u: on screen, at the right", sizes[i][0], sizes[i][1]);
    }
    CHECK(pf_notice_layout("", 1920, 1080, r, PF_NOTICE_MAX_RECTS) == 0, "empty text");
    CHECK(pf_notice_layout("Catching up", 40, 20, r, PF_NOTICE_MAX_RECTS) == 0, "doesn't fit");
    CHECK(pf_notice_layout("Catching up", 1920, 1080, r, 5) == 0, "too few rectangles");
}

static void test_notice_text(void)
{
    static const char *const cases[][2] = {
        { "Catching up", "Catching up..." },
        { "Recuperando atraso", "Recuperando atraso..." },
        { "Recuperando atraso...", "Recuperando atraso..." },
        { "Recuperando atraso.", "Recuperando atraso..." },
        { "Done . ", "Done..." },
        { "", "..." },
        { "0123456789012345678901234567890123456789012345", /* 46 */
          "01234567890123456789012345678901234567890123..." },
    };
    char out[48];
    unsigned i;

    printf("notice text\n");
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        pf_notice_text(cases[i][0], out, sizeof out);
        CHECK(!strcmp(out, cases[i][1]), "'%s' -> '%s'", cases[i][0], out);
    }
    pf_notice_text("Catching up", out, 8);
    CHECK(!strcmp(out, "Catc..."), "cut to the buffer: '%s'", out);
    pf_notice_text("Catching up", out, 3);
    CHECK(!out[0], "no room for the dots: '%s'", out);
}

static void test_notice_device(void)
{
    IDirect3D9 *(WINAPI * create9)(UINT);
    IDirect3D9 *d3d;
    IDirect3DDevice9 *dev = NULL;
    IDirect3DSurface9 *bb = NULL, *sys = NULL;
    D3DPRESENT_PARAMETERS pp;
    D3DSURFACE_DESC desc;
    D3DVIEWPORT9 vp = { 3, 4, 10, 10, 0.0f, 1.0f }, got;
    RECT sc = { 0, 0, 2, 2 };
    D3DLOCKED_RECT lr;
    DWORD scissor = 0;
    pf_notice_rect r[PF_NOTICE_MAX_RECTS];
    unsigned n, fg = 0, bg = 0, other = 0, x, y;
    HWND wnd;

    printf("notice on a D3D9 device\n");
#define SLOT(name) (unsigned)(offsetof(IDirect3DDevice9Vtbl, name) / sizeof(void *))
    CHECK(SLOT(GetBackBuffer) == PF_DEV_GET_BACK_BUFFER &&
              SLOT(SetRenderState) == PF_DEV_SET_RENDER_STATE &&
              SLOT(GetRenderState) == PF_DEV_GET_RENDER_STATE && SLOT(Clear) == PF_DEV_CLEAR &&
              SLOT(SetViewport) == PF_DEV_SET_VIEWPORT && SLOT(GetViewport) == PF_DEV_GET_VIEWPORT &&
              SLOT(GetRenderTarget) == PF_DEV_GET_RENDER_TARGET,
          "device vtable slots");
#undef SLOT
    create9 = (IDirect3D9 * (WINAPI *)(UINT))(void *)GetProcAddress(LoadLibraryA("d3d9.dll"),
                                                                  "Direct3DCreate9");
    d3d = create9 ? create9(D3D_SDK_VERSION) : NULL;
    wnd = CreateWindowExA(0, "STATIC", "test_catchup", WS_POPUP, 0, 0, 64, 64, NULL, NULL, NULL,
                          NULL);
    memset(&pp, 0, sizeof pp);
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = 1280;
    pp.BackBufferHeight = 720;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.hDeviceWindow = wnd;
    if (!d3d || FAILED(IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                                               D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev))) {
        printf("  (no hardware D3D9 device: skipped)\n");
        DestroyWindow(wnd);
        return;
    }
    IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    IDirect3DSurface9_GetDesc(bb, &desc);
    IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET, 0xFF0000FFu, 1.0f, 0);
    IDirect3DDevice9_SetViewport(dev, &vp);
    IDirect3DDevice9_SetScissorRect(dev, &sc);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_SCISSORTESTENABLE, TRUE);
    IDirect3DDevice9_BeginScene(dev);
    pf_notice_draw(dev, "Catching up");
    IDirect3DDevice9_EndScene(dev);
    IDirect3DDevice9_GetViewport(dev, &got);
    IDirect3DDevice9_GetRenderState(dev, D3DRS_SCISSORTESTENABLE, &scissor);
    CHECK(!memcmp(&got, &vp, sizeof vp) && scissor == TRUE, "viewport and scissor test put back");
    IDirect3DDevice9_CreateOffscreenPlainSurface(dev, desc.Width, desc.Height, desc.Format,
                                                 D3DPOOL_SYSTEMMEM, &sys, NULL);
    if (sys && SUCCEEDED(IDirect3DDevice9_GetRenderTargetData(dev, bb, sys)) &&
        SUCCEEDED(IDirect3DSurface9_LockRect(sys, &lr, NULL, D3DLOCK_READONLY))) {
        n = pf_notice_layout("Catching up", desc.Width, desc.Height, r, PF_NOTICE_MAX_RECTS);
        for (y = 0; y < desc.Height; y++) {
            const unsigned *row = (const unsigned *)((const unsigned char *)lr.pBits + y * lr.Pitch);
            for (x = 0; x < desc.Width; x++) {
                unsigned p = row[x] & 0xFFFFFFu, inside = 0, lit = 0, i;
                inside = (long)x >= r[0].x1 && (long)x < r[0].x2 && (long)y >= r[0].y1 &&
                         (long)y < r[0].y2;
                for (i = 1; i < n && !lit; i++)
                    lit = (long)x >= r[i].x1 && (long)x < r[i].x2 && (long)y >= r[i].y1 &&
                          (long)y < r[i].y2;
                if (lit)
                    fg += p == 0xFFD040u;
                else if (inside)
                    bg += p == 0x202020u;
                else
                    other += p != 0x0000FFu;
            }
        }
        IDirect3DSurface9_UnlockRect(sys);
        printf("  %u text pixels, %u background pixels, %u pixels changed outside\n", fg, bg,
               other);
        CHECK(fg > 100 && bg > 400 && other == 0, "notice pixels: %u text, %u box, %u outside",
              fg, bg, other);
    } else {
        CHECK(0, "couldn't read the back buffer");
    }
    /* Another render target bound: nothing is drawn. */
    {
        IDirect3DSurface9 *rt = NULL;
        IDirect3DDevice9_CreateRenderTarget(dev, 256, 256, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE,
                                            0, FALSE, &rt, NULL);
        if (rt) {
            IDirect3DDevice9_SetRenderTarget(dev, 0, rt);
            IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET, 0xFF0000FFu, 1.0f, 0);
            pf_notice_draw(dev, "Catching up");
            if (SUCCEEDED(IDirect3DDevice9_GetRenderTargetData(dev, bb, sys))) {
                /* The back buffer kept its pixels from before (the notice). */
            }
            {
                IDirect3DSurface9 *s2 = NULL;
                IDirect3DDevice9_CreateOffscreenPlainSurface(dev, 256, 256, D3DFMT_X8R8G8B8,
                                                             D3DPOOL_SYSTEMMEM, &s2, NULL);
                if (s2 && SUCCEEDED(IDirect3DDevice9_GetRenderTargetData(dev, rt, s2)) &&
                    SUCCEEDED(IDirect3DSurface9_LockRect(s2, &lr, NULL, D3DLOCK_READONLY))) {
                    unsigned changed = 0;
                    for (y = 0; y < 256; y++)
                        for (x = 0; x < 256; x++)
                            changed += (((const unsigned *)((const unsigned char *)lr.pBits +
                                                            y * lr.Pitch))[x] &
                                        0xFFFFFFu) != 0x0000FFu;
                    IDirect3DSurface9_UnlockRect(s2);
                    CHECK(changed == 0, "drawn into another render target: %u pixels", changed);
                }
                if (s2)
                    IDirect3DSurface9_Release(s2);
            }
            IDirect3DDevice9_SetRenderTarget(dev, 0, bb);
            IDirect3DSurface9_Release(rt);
        }
    }
    if (sys)
        IDirect3DSurface9_Release(sys);
    IDirect3DSurface9_Release(bb);
    IDirect3DDevice9_Release(dev);
    IDirect3D9_Release(d3d);
    DestroyWindow(wnd);
}

int main(void)
{
    test_simulated_loop();
    test_site();
    test_skip_lights();
    test_notice_window();
    test_notice_layout();
    test_notice_text();
    test_notice_device();
    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: catch-up tests passed\n");
    return 0;
}
