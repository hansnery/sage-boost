/* The extra-frame scheduler and its checks, against a fake limiter site,
 * engine, display and game state. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "fps.h"
#include "fpsmath.h"
#include "sites.h"

static int g_failures;

/* stubs.c's common entry points; no stubs are installed here. */
void *pf_enter(unsigned k, void **slot) { (void)k; (void)slot; return 0; }
void *pf_exit(unsigned k, unsigned char *esp) { (void)k; (void)esp; return 0; }

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            g_failures++;                                 \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

/* The game state the module reads or restores. */
static unsigned g_now_ms, g_frame_start, g_seed, g_sync_acc, g_sync, g_prev_sync, g_last_draw,
    g_skip, g_init, g_last_client;
static int g_screenshots, g_iconic, g_draws, g_misbehave;
static unsigned char g_verify;
static unsigned g_engine[32];
static void *g_hwnd = (void *)1, *g_logic;

/* With g_real_clock the game's clock runs in real time from the wait call;
 * otherwise it stands still unless the test or the fake draw moves it. */
static int g_real_clock;
static unsigned g_clock_base_ms, g_draw_spin_us, g_draw_at[8];
static float g_draw_cam_tx[8];
static LARGE_INTEGER g_clock_base;
/* Otherwise the fine clock is simulated: every read of it moves time on by
 * 100 us (so the plugin's pacing spins finish at once), on top of g_now_ms. */
static unsigned g_sim_us;

static unsigned fake_fine(void)
{
    g_sim_us += 100;
    return g_now_ms * 1000 + g_sim_us;
}

static DWORD __stdcall fake_time(void)
{
    LARGE_INTEGER t, f;
    if (!g_real_clock)
        return g_now_ms + g_sim_us / 1000;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    return g_clock_base_ms +
           (unsigned)((t.QuadPart - g_clock_base.QuadPart) * 1000 / f.QuadPart);
}

static void spin_us(unsigned us)
{
    LARGE_INTEGER t0, t, f;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    do
        QueryPerformanceCounter(&t);
    while ((t.QuadPart - t0.QuadPart) * 1000000 / f.QuadPart < us);
}
static int __stdcall fake_iconic(void *h) { (void)h; return g_iconic; }

/* The client: vtable slot 0x7C is `mov eax, [ecx+0x20]; ret`. */
static unsigned char g_getter[] = { 0x8B, 0x41, 0x20, 0xC3 };
static void *g_client_vt[32];
static struct { void **vt; unsigned pad[7]; unsigned frame; } g_client_obj;
static void *g_client;

static void __attribute__((thiscall)) fake_draw(void *self);
static void *g_display_vt[13];
static struct { void **vt; } g_display_obj;
static void *g_display;

/* The view camera: a RenderObjClass with the transform at +0x18 and
 * Set_Transform at vtable +0x54. */
static int g_set_transforms;
static float g_seen_tx;
static void __attribute__((thiscall)) fake_set_transform(void *self, const float *m)
{
    memcpy((unsigned char *)self + 0x18, m, 48);
    g_set_transforms++;
}
static void *g_camera_vt[32];
static struct { void **vt; unsigned char pad[0x14]; float m[12]; } g_camera_obj;
static unsigned char g_view_obj[0x200];
static void *g_view;

static void camera_at(float tx)
{
    static const float id[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
    memcpy(g_camera_obj.m, id, sizeof id);
    g_camera_obj.m[3] = tx;
}

/* HLods: the same layout and Set_Transform slot as the camera. */
typedef struct { void **vt; unsigned char pad[0x14]; float m[12]; } fake_robj;
static fake_robj g_unit, g_unit2;
static float g_seen_unit_tx, g_seen_unit2_tx, g_draw_unit_tx[8];

static void unit_at(fake_robj *u, float tx)
{
    static const float id[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
    void *slot_area[8];
    void **slot = slot_area + 4;
    memcpy(u->m, id, sizeof id);
    u->m[3] = tx;
    /* What the game's HLodClass::Set_Transform probe sees. */
    slot[-4] = u;
    slot[1] = u->m;
    pf_fps_enter(PF_HLOD_SET, slot, NULL, 4);
}

static void unit_dies(fake_robj *u)
{
    void *slot_area[8];
    void **slot = slot_area + 4;
    slot[-4] = u;
    pf_fps_enter(PF_HLOD_DTOR, slot, NULL, 0);
}

/* The particle manager: TheFXParticleSystemManager's vtable slot 0x28 is
 * the hooked update. */
static void __attribute__((thiscall)) fake_particles_update(void *self) { (void)self; }
static void __attribute__((thiscall)) other_update(void *self) { (void)self; }
static void *g_particles_vt[16];
static struct { void **vt; } g_particles_obj;
static void *g_particles;
/* What the draw's per-draw updates would have done: run (the hook returned
 * their target) or been held. */
static int g_views_ran, g_particles_ran, g_shadow_ran, g_reflect_ran;
static int g_profile_markers;
static int g_profile_ids[5];

static void profile_marker(unsigned kind, void *self, const char *label)
{
    void *stack[8] = {0};
    void **slot = stack + 4;
    slot[-4] = self;
    slot[1] = (void *)label;
    CHECK(pf_fps_enter(kind, slot, (void *)other_update, 0) == (void *)other_update,
          "profiling changed the marker target");
}

static DWORD WINAPI profile_other_thread(void *arg)
{
    (void)arg;
    /* A null slot must be rejected before either slot[1] or slot[-4] is
     * read. This runs while the main thread has a recognized scope open. */
    if (pf_fps_enter(PF_EVENT_BEGIN, NULL, (void *)other_update, 0) != (void *)other_update)
        return 1;
    if (pf_fps_enter(PF_EVENT_END, NULL, (void *)other_update, 0) != (void *)other_update)
        return 2;
    return 0;
}

static void profile_draw_markers(void)
{
    static const char *const labels[4] = {
        "UpdateShadowMap", "UpdateWaterReflection", "RenderViews", "RenderUI"
    };
    unsigned i;
    for (i = 0; i < 4; i++) {
        profile_marker(PF_EVENT_BEGIN, &g_profile_ids[i], labels[i]);
        profile_marker(PF_EVENT_BEGIN, &g_profile_ids[4], "RenderOneObject");
        profile_marker(PF_EVENT_END, &g_profile_ids[4], NULL);
        if (i == 0 && g_profile_markers == 2) {
            DWORD result = 99;
            HANDLE thread = CreateThread(NULL, 0, profile_other_thread, NULL, 0, NULL);
            CHECK(thread != NULL, "create marker worker failed");
            if (thread) {
                CHECK(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0,
                      "marker worker did not finish");
                CHECK(GetExitCodeThread(thread, &result) && result == 0,
                      "wrong-thread markers changed target: %lu", result);
                CloseHandle(thread);
            }
        }
        profile_marker(PF_EVENT_END, &g_profile_ids[i], NULL);
    }
}

static void draw_updates(void)
{
    static void *stack[16];
    void **slot = stack + 12;
    g_views_ran += pf_fps_enter(PF_VIEWS, slot, (void *)other_update, 0) == (void *)other_update;
    g_particles_ran += pf_fps_enter(PF_PARTICLES, slot, (void *)fake_particles_update, 0) ==
                       (void *)fake_particles_update;
    /* The frame's shadow map (ret 4) and reflection (ret 8) calls. */
    g_shadow_ran += pf_fps_enter(PF_SHADOW, slot, (void *)other_update, 4) == (void *)other_update;
    g_reflect_ran += pf_fps_enter(PF_REFLECT, slot, (void *)other_update, 8) ==
                     (void *)other_update;
}

static void __attribute__((thiscall)) fake_draw(void *self)
{
    (void)self;
    if (g_draws < 8) {
        g_draw_at[g_draws] = fake_time() - g_frame_start;
        g_draw_unit_tx[g_draws] = g_unit.m[3];
        g_draw_cam_tx[g_draws] = g_camera_obj.m[3];
    }
    g_draws++;
    draw_updates();
    if (g_profile_markers)
        profile_draw_markers();
    if (g_draw_spin_us)
        spin_us(g_draw_spin_us);
    g_seen_tx = g_camera_obj.m[3];
    g_seen_unit_tx = g_unit.m[3];
    g_seen_unit2_tx = g_unit2.m[3];
    if (g_misbehave == 5)
        g_camera_obj.m[7] += 1.0f;
    /* What the real draw does to its own state; the module restores it. */
    g_sync_acc += 33;
    g_prev_sync = g_sync;
    g_last_draw = 12345;
    g_last_client = 777;
    if (g_misbehave == 1)
        g_seed++;
    if (g_misbehave == 2)
        g_engine[0x34 / 4]++;
    if (g_misbehave == 3)
        g_client_obj.frame++;
    if (g_misbehave == 4)
        g_now_ms += 40;
    if (g_misbehave == 6)
        g_now_ms += 14; /* drawn at 5 + 16.5 ms: 2 ms late, a minor late */
    if (g_misbehave == 7)
        g_now_ms += 120;
}

/* The limiter site: push ebx; call [Sleep] | call [timeGetTime]; ... sub eax, [start]. */
static unsigned char g_site[64];
static void *g_time_slot = (void *)fake_time;

static pf_fps_sites sites(void)
{
    pf_fps_sites s;
    memset(&s, 0, sizeof s);
    s.display = &g_display;
    s.draw = (void *)fake_draw;
    s.hwnd = &g_hwnd;
    s.is_iconic = (void *)fake_iconic;
    s.client = &g_client;
    s.last_client_frame = &g_last_client;
    s.sync_acc = &g_sync_acc;
    s.last_draw_ms = &g_last_draw;
    s.skip_count = &g_skip;
    s.init_flags = &g_init;
    s.screenshots = &g_screenshots;
    s.sync_time = &g_sync;
    s.prev_sync_time = &g_prev_sync;
    s.logic_seed = &g_seed;
    s.verify_crc = &g_verify;
    s.logic = &g_logic;
    s.tactical_view = &g_view;
    s.camera_off = 0x104;
    s.set_transform = 0x54;
    s.particles = &g_particles;
    s.particles_slot = 0x28;
    return s;
}

static void installed(void)
{
    pf_fps_set_installed(1, 1, 1, (void *)fake_particles_update);
}

/* A wait call `elapsed` ms into a 33 ms game frame. */
static void wait_at(unsigned elapsed)
{
    static void *stack[16];
    static unsigned char frame[64];
    void **slot = stack + 12;

    g_now_ms = g_frame_start + elapsed;
    g_sim_us = 0;
    g_clock_base_ms = g_now_ms;
    QueryPerformanceCounter(&g_clock_base);
    *(unsigned **)(frame + 0x20 - 0x20) = g_engine; /* [EBP-0x20], EBP = frame + 0x20 */
    slot[0] = g_site + 5;
    slot[-8] = frame + 0x20;
    slot[-9] = (void *)33;
    pf_fps_enter(PF_FPS_WAIT, slot, NULL, 4);
}

/* Two game frames so an extra frame's cost has been measured (until then
 * it is taken as 10 ms, and fewer fit). */
static void warm_up(void)
{
    pf_fps_row r;
    unsigned saved = g_frame_start;
    g_frame_start = 900000;
    wait_at(5);
    g_frame_start = 900033;
    wait_at(5);
    g_frame_start = saved;
    pf_fps_take_row(&r);
}

static void start_smoothing(unsigned max_fps, int smoothing)
{
    pf_fps_sites s = sites();
    pf_fps_init(&s, NULL, max_fps, smoothing, 0);
    installed();
    pf_fps_set_clock(fake_fine);
    g_misbehave = g_iconic = g_screenshots = 0;
    warm_up();
    g_draws = g_set_transforms = 0;
}

static void start(unsigned max_fps)
{
    start_smoothing(max_fps, 0);
}

/* Fresh, with no extra frame measured yet: one game frame that overran (no
 * room), so the module knows the main thread. */
static void start_cold(unsigned max_fps)
{
    pf_fps_sites s = sites();
    pf_fps_init(&s, NULL, max_fps, 0, 0);
    installed();
    pf_fps_set_clock(fake_fine);
    g_misbehave = g_iconic = g_screenshots = 0;
    g_frame_start = 800000;
    wait_at(34);
    g_draws = 0;
}

/* The game's own draw of its frame, `us` long (timed as the reference);
 * `skipped`: catch-up skipped its render. */
static void game_draw(unsigned us, int skipped)
{
    pf_fps_draw_edge(1);
    if (skipped)
        pf_fps_render_skipped();
    spin_us(us);
    pf_fps_draw_edge(0);
}

/* Catch-up's "behind the schedule" for the hold. */
static int g_behind;
static int fake_behind(void)
{
    return g_behind;
}

/* `frames` game frames 33 ms apart from `t`, each shown `at` ms in, with
 * g_unit moving `step` per frame; returns the frame time after the last. */
static unsigned run_frames(unsigned t, unsigned frames, unsigned at, float step)
{
    unsigned i;
    for (i = 0; i < frames; i++, t += 33) {
        g_frame_start = t;
        unit_at(&g_unit, step * (float)i);
        g_draws = 0;
        wait_at(at);
    }
    return t;
}

/* One game frame at `t` with the camera at x: the game's draw (its view
 * update put the camera there, then the particle update) and its wait 5 ms
 * in. Every camera x drawn, the game's frame then its extra frames, goes to
 * drawn[*n..cap). */
static void cam_frame(unsigned t, float x, float *drawn, unsigned *n, unsigned cap)
{
    int j;
    g_frame_start = t;
    camera_at(x);
    pf_fps_draw_edge(1);
    draw_updates();
    if (*n < cap)
        drawn[(*n)++] = g_camera_obj.m[3];
    pf_fps_draw_edge(0);
    CHECK(g_camera_obj.m[3] == x, "after the game's draw the camera is at %f, want %f",
          g_camera_obj.m[3], x);
    g_draws = 0;
    wait_at(5);
    for (j = 0; j < g_draws && j < 8; j++)
        if (*n < cap)
            drawn[(*n)++] = g_draw_cam_tx[j];
    CHECK(g_camera_obj.m[3] == x, "after the extra frames the camera is at %f, want %f",
          g_camera_obj.m[3], x);
}

int main(void)
{
    pf_fps_row row;

    g_client_vt[0x7C / 4] = g_getter;
    g_client_obj.vt = g_client_vt;
    g_client = &g_client_obj;
    g_particles_vt[0x28 / 4] = (void *)fake_particles_update;
    g_particles_obj.vt = g_particles_vt;
    g_particles = &g_particles_obj;
    g_display_vt[12] = (void *)fake_draw;
    g_display_obj.vt = g_display_vt;
    g_display = &g_display_obj;
    g_site[0] = 0xFF;
    *(void ***)(g_site + 8) = &g_time_slot;
    *(unsigned **)(g_site + 16) = &g_frame_start;

    printf("schedule: evenly between the game's frames, by when each is shown\n");
    {
        pf_fps_sched s;
        unsigned st[4] = { 0, 0, 0, 0 }, n;
        s.period = 33000;
        s.budget = 29000;
        s.extras = 3;
        s.cost = 2000;
        s.avg = 2000;
        s.margin = 3250;
        n = pf_fps_schedule(&s, st);
        CHECK(n == 3 && st[0] == 6250 && st[1] == 14500 && st[2] == 22750,
              "three: %u at %u %u %u (want 6250 14500 22750)", n, st[0], st[1], st[2]);
        s.budget = 25000;
        n = pf_fps_schedule(&s, st);
        CHECK(n == 3 && st[0] == 5083 && st[1] == 12166 && st[2] == 19250,
              "less room: %u at %u %u %u (want 3, shown at 7.08 14.17 21.25 ms)", n, st[0], st[1],
              st[2]);
        s.budget = 20000;
        n = pf_fps_schedule(&s, st);
        CHECK(n == 2 && st[0] == 6125 && st[1] == 14250,
              "less still: %u at %u %u (want 2, shown at 8.125 16.25 ms)", n, st[0], st[1]);
        s.budget = 31000;
        s.cost = 12000;
        s.avg = 3000;
        s.margin = 4500;
        n = pf_fps_schedule(&s, st);
        CHECK(n == 1 && st[0] == 13500, "12 ms each: %u at %u (want 1 at 13500, not back to back)",
              n, st[0]);
        s.budget = 12000;
        s.cost = 3000;
        s.margin = 3375;
        n = pf_fps_schedule(&s, st);
        CHECK(n == 1 && st[0] == 5125, "a late game frame: %u at %u (want 1, as late as fits)", n,
              st[0]);
        s.budget = 6000;
        CHECK(pf_fps_schedule(&s, st) == 0, "no room");
        s.budget = 7000;
        s.avg = 0;
        CHECK(pf_fps_schedule(&s, st) == 0, "would be shown right after the game's frame");
        s.budget = 29000;
        s.extras = 0;
        CHECK(pf_fps_schedule(&s, st) == 0, "none wanted");
    }

    printf("one long Sleep: the extra frames are paced inside the wait, evenly\n");
    start(120);
    g_real_clock = 1;
    pf_fps_set_clock(NULL);
    g_draw_spin_us = 1000;
    {
        unsigned i;
        /* The count rises to 3 once 3 have fit for a few frames in a row. */
        for (i = 0; i < 12; i++) {
            g_frame_start = 30000 + 33 * i;
            g_draws = 0;
            wait_at(5);
        }
    }
    pf_fps_take_row(&row);
    CHECK(g_draws == 3, "%d extra frames at 120 with 28 ms left, want 3", g_draws);
    /* Shown at 5 + 33 * j / 4 ms, so started 1 ms (their cost) before. */
    CHECK(g_draw_at[0] >= 11 && g_draw_at[0] <= 13 && g_draw_at[1] >= 19 && g_draw_at[1] <= 22 &&
              g_draw_at[2] >= 27 && g_draw_at[2] <= 30,
          "drawn at %u, %u, %u ms, want about 12, 20.5, 28.75", g_draw_at[0], g_draw_at[1],
          g_draw_at[2]);
    CHECK(row.late == 0 && row.sleeps == 12 && row.waits == 12 && row.cost_us >= 900 &&
              row.cost_us < 5000,
          "row: late %u sleeps %u waits %u cost %u", row.late, row.sleeps, row.waits, row.cost_us);
    g_draws = 0;
    g_frame_start = 31000;
    wait_at(20); /* a heavy frame: one, as late as fits */
    CHECK(g_draws == 1 && g_draw_at[0] >= 27 && g_draw_at[0] <= 30,
          "heavy frame: %d extra frames, at %u ms", g_draws, g_draw_at[0]);
    g_draws = 0;
    g_frame_start = 31033;
    wait_at(30);
    CHECK(g_draws == 0, "drew with 3 ms left");
    pf_fps_take_row(&row);
    CHECK(row.late == 0 && !pf_fps_stopped(), "late %u, stopped %s", row.late,
          pf_fps_stopped() ? pf_fps_stopped() : "no");

    printf("a cost measured during a load doesn't block extra frames for good\n");
    g_real_clock = 0;
    start(120);
    g_real_clock = 1;
    pf_fps_set_clock(NULL);
    g_draw_spin_us = 40000;
    g_frame_start = 40000;
    wait_at(13);
    CHECK(g_draws == 1, "the slow extra frame wasn't drawn");
    g_draw_spin_us = 1500;
    {
        unsigned i;
        for (i = 1; i < 200 && g_draws == 1; i++) {
            g_frame_start = 40000 + 33 * i;
            wait_at(13);
        }
        CHECK(g_draws > 1 && i < 120, "no extra frame for %u game frames after a 40 ms one", i);
        CHECK(!pf_fps_stopped(), "stopped: %s", pf_fps_stopped() ? pf_fps_stopped() : "no");
    }
    g_real_clock = 0;
    g_draw_spin_us = 0;
    pf_fps_set_clock(fake_fine);

    printf("before any extra frame: expected to cost what the game's own draw does\n");
    start_cold(120);
    pf_fps_take_row(&row);
    CHECK(row.cost_us == 10000 && row.ref_us == 0, "no draw timed yet: expected %u us, ref %u us",
          row.cost_us, row.ref_us);
    {
        unsigned i, ref;
        for (i = 0; i < 10; i++)
            game_draw(3000, 0);
        pf_fps_take_row(&row);
        ref = row.ref_us;
        CHECK(ref >= 3000 && ref < 4500 && row.cost_us == ref,
              "after 10 draws of 3 ms: ref %u us, expected %u us", ref, row.cost_us);
        game_draw(20000, 1);
        pf_fps_take_row(&row);
        CHECK(row.ref_us == ref, "a draw whose render catch-up skipped was timed: %u us, was %u",
              row.ref_us, ref);
        for (i = 0; i < 10; i++)
            game_draw(0, 0);
        pf_fps_take_row(&row);
        CHECK(row.ref_us < 1000 && row.cost_us == 1000,
              "cheap draws: ref %u us, expected %u us (want the 1 ms floor)", row.ref_us,
              row.cost_us);
    }

    printf("long stretches without room for an extra frame leave the expected cost as it is\n");
    start(120);
    g_real_clock = 1;
    pf_fps_set_clock(NULL);
    g_draw_spin_us = 8000;
    {
        unsigned i, t = 50000, drawn;
        for (i = 0; i < 12; i++, t += 33) {
            g_frame_start = t;
            wait_at(5);
        }
        pf_fps_take_row(&row);
        CHECK(row.extra > 0 && row.cost_us >= 8000, "8 ms extra frames: %u drawn, expected %u us",
              row.extra, row.cost_us);
        /* 20 s: game frames that overran, frames with 7 ms left, and frames
         * with 23 ms left that catch-up holds. */
        pf_fps_set_hold(fake_behind);
        drawn = (unsigned)g_draws;
        for (i = 0; i < 600; i++, t += 33) {
            g_frame_start = t;
            g_behind = i % 3 == 2;
            wait_at(i % 3 == 0 ? 33 : i % 3 == 1 ? 26 : 10);
        }
        g_behind = 0;
        pf_fps_set_hold(NULL);
        pf_fps_take_row(&row);
        CHECK((unsigned)g_draws == drawn && row.extra == 0, "drew %u extra frames without room",
              row.extra);
        CHECK(row.cost_us >= 8000, "the expected cost fell to %u us with no extra frame drawn",
              row.cost_us);
        /* The load eases: extra frames come back expecting what they cost. */
        for (i = 0; i < 40; i++, t += 33) {
            g_frame_start = t;
            wait_at(5);
        }
        pf_fps_take_row(&row);
        CHECK(row.extra > 0 && row.late == 0 && row.cost_us >= 8000 && !pf_fps_stopped() &&
                  !pf_fps_resting(),
              "after the stretch: %u drawn, %u late, expected %u us, stopped %s, resting %s",
              row.extra, row.late, row.cost_us, pf_fps_stopped() ? pf_fps_stopped() : "no",
              pf_fps_resting() ? pf_fps_resting() : "no");
    }

    printf("a slow sample gives way only in frames with room, never below the game's own draw\n");
    start(120);
    g_real_clock = 1;
    pf_fps_set_clock(NULL);
    {
        unsigned i, t = 70000, low = ~0u, resumed = 0;
        for (i = 0; i < 10; i++)
            game_draw(6000, 0);
        g_draw_spin_us = 40000;
        g_frame_start = t;
        wait_at(13);
        CHECK(g_draws == 1, "the slow extra frame wasn't drawn");
        g_draw_spin_us = 1500;
        for (i = 1; i < 200 && !resumed; i++) {
            t += 33;
            g_frame_start = t;
            game_draw(6000, 0);
            wait_at(13);
            resumed = g_draws > 1;
            pf_fps_take_row(&row);
            if (!resumed && row.cost_us < low)
                low = row.cost_us;
        }
        CHECK(resumed && i < 120, "no extra frame for %u game frames after a 40 ms one", i);
        CHECK(low >= 6000 && row.ref_us >= 6000,
              "the expected cost went down to %u us, under the game's draw (%u us)", low,
              row.ref_us);
        CHECK(row.late == 0 && !pf_fps_stopped(), "late %u, stopped %s", row.late,
              pf_fps_stopped() ? pf_fps_stopped() : "no");
    }

    printf("a late after a guessed cost: the guess isn't made that low again\n");
    start(120);
    g_real_clock = 1;
    pf_fps_set_clock(NULL);
    {
        unsigned i, t = 90000, lates = 0, extras = 0, probes = 0;
        for (i = 0; i < 10; i++)
            game_draw(3000, 0);
        g_draw_spin_us = 40000;
        g_frame_start = t;
        wait_at(13);
        pf_fps_take_row(&row);
        /* From here the extra frames cost 22 ms, far over the 3 ms draw: the
         * first guess is late; only bounded recovery probes may follow. */
        g_draw_spin_us = 22000;
        for (i = 1; i < 1400; i++) {
            t += 33;
            g_frame_start = t;
            game_draw(3000, 0);
            wait_at(13);
            pf_fps_take_row(&row);
            lates += row.late;
            extras += row.extra;
            probes += row.probes[2];
        }
        CHECK(lates == 4 && extras == 4 && probes == 3 && row.cost_us >= 22000,
              "22 ms extra frames with 20 ms left: %u late, %u drawn, expected %u us", lates,
              extras, row.cost_us);
        CHECK(!pf_fps_stopped() && pf_fps_resting(), "stopped %s, resting %s",
              pf_fps_stopped() ? pf_fps_stopped() : "no",
              pf_fps_resting() ? pf_fps_resting() : "no");
    }
    printf("recovery from a learned floor above the entire frame budget\n");
    start_cold(120);
    g_real_clock = 1;
    pf_fps_set_clock(NULL);
    {
        unsigned i, t = 0xffffd000u, resumed = 0;
        for (i = 0; i < 10; i++)
            game_draw(3000, 0);
        /* The first draw is guessed: a transient locks the learned floor. */
        g_draw_spin_us = 31000;
        g_frame_start = t;
        wait_at(13);
        pf_fps_take_row(&row);
        CHECK(row.extra == 1 && row.late == 1 && row.cost_us >= 31000,
              "seed recovery trap: %u extras, %u late, %u us", row.extra, row.late, row.cost_us);
        g_draw_spin_us = 1000;
        pf_fps_set_hold(fake_behind);
        for (i = 0; i < 750 && !resumed; i++) {
            t += 33; /* Cross the 32-bit millisecond clock wrap. */
            g_frame_start = t;
            game_draw(3000, 0);
            g_behind = i == 180;
            g_screenshots = i == 200;
            g_iconic = i == 220;
            wait_at(i == 240 ? 33 : 13);
            if (g_behind) wait_at(14); /* Repeated waits count one frame. */
            pf_fps_take_row(&row);
            if (g_behind) CHECK(row.blocked[1] == 1, "hold counted per wait, not frame");
            if (i == 260) {
                /* A game frame with no limiter wait also breaks the run. */
                pf_fps_frame_edge(0);
                pf_fps_frame_edge(1);
            }
            resumed = row.extra;
            CHECK(!row.late, "cheap recovery draw was late");
        }
        g_behind = 0;
        pf_fps_set_hold(NULL);
        CHECK(resumed && i >= 560 && row.probes[0] == 1 && row.probes[1] == 1, "bounded recovery: resumed %u after %u frames", resumed, i);
        CHECK(row.cost_us < 10000, "stale estimate survived successful recovery: %u", row.cost_us);
    }
    g_real_clock = 0;
    g_draw_spin_us = 0;
    pf_fps_set_clock(fake_fine);

    printf("off at 30\n");
    start(30);
    g_frame_start = 1000;
    wait_at(20);
    CHECK(g_draws == 0, "drew at MaxFPS=30");

    printf("one extra frame at 60, state restored\n");
    start(60);
    g_frame_start = 2000;
    g_sync_acc = 500;
    g_sync = 400;
    g_prev_sync = 300;
    g_last_draw = 1990;
    g_last_client = 10;
    g_client_obj.frame = 10;
    wait_at(5);
    CHECK(g_draws == 1, "no extra frame: %d", g_draws);
    CHECK(g_draw_at[0] >= 21 && g_draw_at[0] <= 22, "drawn at %u ms, want 5 + 16.5",
          g_draw_at[0]);
    CHECK(g_sync_acc == 500 && g_sync == 400 && g_prev_sync == 300 && g_last_draw == 1990 &&
              g_last_client == 10,
          "draw state not restored: acc %u sync %u prev %u last %u client %u", g_sync_acc, g_sync,
          g_prev_sync, g_last_draw, g_last_client);
    wait_at(25);
    CHECK(g_draws == 1, "a second extra frame at 60");
    g_frame_start = 2033;
    wait_at(5);
    CHECK(g_draws == 2, "no extra frame in the next game frame");
    pf_fps_take_row(&row);
    CHECK(row.extra == 2 && row.waits == 2, "row: extra %u waits %u", row.extra, row.waits);
    CHECK(pf_fps_stopped() == NULL, "stopped: %s", pf_fps_stopped());

    printf("paused while minimized or a screenshot is pending\n");
    start(60);
    g_frame_start = 3000;
    g_iconic = 1;
    wait_at(5);
    g_iconic = 0;
    g_screenshots = 1;
    wait_at(6);
    CHECK(g_draws == 0 && pf_fps_stopped() == NULL, "drew while minimized or screenshotting");

    printf("checks\n");
    {
        static const char *const want[] = { "random", "frame counters", "client frame" };
        int m;
        for (m = 1; m <= 3; m++) {
            unsigned i;
            start(60);
            g_misbehave = m;
            for (i = 0; i < 6 && !pf_fps_stopped(); i++) {
                g_frame_start = 4000 + 100 * i;
                wait_at(5);
            }
            CHECK(pf_fps_stopped() && strstr(pf_fps_stopped(), want[m - 1]),
                  "misbehaviour %d: stopped = %s", m, pf_fps_stopped() ? pf_fps_stopped() : "no");
            g_frame_start = 5000;
            g_misbehave = 0;
            i = (unsigned)g_draws;
            wait_at(16);
            CHECK((unsigned)g_draws == i, "misbehaviour %d: drew after stopping", m);
        }
    }

    printf("late frames: isolated ones pause, persistent ones rest 1, 2, 4 minutes, then stop\n");
    start(60);
    {
        unsigned f, drawn_before;
        /* 70 s of game frames. One late in the grace period after the first
         * frame, then one every 12 s: 5 counted, none within 10 s of another. */
        for (f = 0; f < 70 * 30; f++) {
            g_frame_start = 100000 + f * 33;
            g_misbehave = f == 30 || (f % 360 == 359) ? 4 : 0;
            wait_at(5);
        }
        g_misbehave = 0;
        pf_fps_take_row(&row);
        CHECK(row.late == 6 && !pf_fps_stopped() && !pf_fps_resting(),
              "isolated lates: %u late, stopped: %s, resting: %s", row.late,
              pf_fps_stopped() ? pf_fps_stopped() : "no",
              pf_fps_resting() ? pf_fps_resting() : "no");
        /* Each late paused extra frames for 1-2 s; the rest drew one each. */
        CHECK(row.extra > 70 * 30 - 6 * 70 && row.extra < 70 * 30 - 6 * 25,
              "extra frames around the pauses: %u", row.extra);
        drawn_before = (unsigned)g_draws;
        g_frame_start = 100000 + f * 33;
        wait_at(5);
        CHECK((unsigned)g_draws == drawn_before + 1, "no extra frame after the isolated lates");

        /* Now every extra frame is late: a rest once the pauses reach 8 s
         * (1 + 2 + 4 + 8 s of them) and it is late again. */
        g_misbehave = 4;
        for (f = 2101; f < 2101 + 30 * 30 && !pf_fps_resting(); f++) {
            g_frame_start = 100000 + f * 33;
            wait_at(5);
        }
        CHECK(pf_fps_resting() && strstr(pf_fps_resting(), "backing off to 8 s") &&
                  !pf_fps_stopped() && f - 2101 > 14 * 30 && f - 2101 < 20 * 30,
              "persistent lates: resting after %u frames: %s, stopped: %s", f - 2101,
              pf_fps_resting() ? pf_fps_resting() : "no",
              pf_fps_stopped() ? pf_fps_stopped() : "no");
        /* Still late every time once back: rests of 1, 2 and 4 minutes, with
         * no extra frame in them, then off for the session. */
        {
            unsigned rest_frames[3] = { 0, 0, 0 }, rests = 1, from = f, end = f + 30 * 600;
            unsigned drawn = (unsigned)g_draws, in_rest_draws = 0;
            int was = 1;
            for (; f < end && !pf_fps_stopped(); f++) {
                int now;
                g_frame_start = 100000 + f * 33;
                wait_at(5);
                now = pf_fps_resting() != NULL;
                if (was && now)
                    in_rest_draws += (unsigned)g_draws != drawn;
                if (was && !now && rests <= 3)
                    rest_frames[rests - 1] = f - from;
                if (!was && now) {
                    rests++;
                    from = f;
                }
                was = now;
                drawn = (unsigned)g_draws;
            }
            g_misbehave = 0;
            CHECK(rest_frames[0] * 33 >= 59000 && rest_frames[0] * 33 <= 61000 &&
                      rest_frames[1] * 33 >= 119000 && rest_frames[1] * 33 <= 121000 &&
                      rest_frames[2] * 33 >= 239000 && rest_frames[2] * 33 <= 241000 &&
                      rests == 3,
                  "rests of %u, %u, %u game frames (want 1, 2, 4 minutes), %u rests",
                  rest_frames[0], rest_frames[1], rest_frames[2], rests);
            CHECK(in_rest_draws == 0, "%u extra frames drawn while resting", in_rest_draws);
            CHECK(pf_fps_stopped() && strstr(pf_fps_stopped(), "resting 1, 2 and 4 minutes"),
                  "after the third rest: stopped %s",
                  pf_fps_stopped() ? pf_fps_stopped() : "no");
        }
    }

    printf("late frames: minor ones pause 1 s and never stop\n");
    start(60);
    {
        unsigned f;
        for (f = 0; f < 120 * 30; f++) {
            g_frame_start = 200000 + f * 33;
            g_misbehave = 6;
            wait_at(5);
        }
        g_misbehave = 0;
        pf_fps_take_row(&row);
        CHECK(row.late > 100 && row.late_ms >= 2 * row.late && row.late_ms <= 3 * row.late &&
                  !pf_fps_stopped(),
              "minor lates: %u late, %u ms, stopped: %s", row.late, row.late_ms,
              pf_fps_stopped() ? pf_fps_stopped() : "no");
    }

    printf("late frames: big ones too far apart to escalate, over 1%% of a minute\n");
    start(60);
    {
        unsigned f;
        /* About 109 ms late every 11 s: the sixth is over 600 ms in a minute. */
        for (f = 0; f < 120 * 30 && !pf_fps_resting(); f++) {
            g_frame_start = 300000 + f * 33;
            g_misbehave = f % 330 == 329 ? 7 : 0;
            wait_at(5);
        }
        g_misbehave = 0;
        pf_fps_take_row(&row);
        CHECK(pf_fps_resting() && strstr(pf_fps_resting(), "1% of the last minute") &&
                  !pf_fps_stopped() && row.late >= 5 && row.late <= 7,
              "big lates: %u late (%u ms), resting: %s, stopped: %s", row.late, row.late_ms,
              pf_fps_resting() ? pf_fps_resting() : "no",
              pf_fps_stopped() ? pf_fps_stopped() : "no");
        /* Back after a minute, with the minute's lateness started over. */
        {
            unsigned start_f = f;
            for (; f < start_f + 62 * 30 && pf_fps_resting(); f++) {
                g_frame_start = 300000 + f * 33;
                wait_at(5);
            }
            g_draws = 0;
            g_frame_start = 300000 + f * 33;
            wait_at(5);
            CHECK(!pf_fps_resting() && g_draws == 1 && f - start_f >= 59 * 30,
                  "after %u game frames of rest: resting %s, %d extra frames", f - start_f,
                  pf_fps_resting() ? pf_fps_resting() : "no", g_draws);
        }
    }

    printf("a stall starts a grace period\n");
    start(60);
    {
        unsigned f, t = 300000;
        for (f = 0; f < 700; f++) {
            t += f == 330 ? 2000 : 33; /* a 2 s load after 11 s */
            g_frame_start = t;
            /* Late on every attempt for the 10 s after the load. */
            g_misbehave = f > 330 && f < 330 + 290 ? 4 : 0;
            wait_at(5);
        }
        g_misbehave = 0;
        pf_fps_take_row(&row);
        CHECK(row.late >= 3 && !pf_fps_stopped(), "lates after a stall: %u, stopped: %s", row.late,
              pf_fps_stopped() ? pf_fps_stopped() : "no");
    }

    printf("matrix steps\n");
    {
        float out[12];
        static const float a[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
        /* 10 degrees about Z, and 10 units along x. */
        static const float b[12] = { 0.98480775f, -0.17364818f, 0, 10, 0.17364818f, 0.98480775f, 0,
                                     0,           0,            0, 1, 0 };
        float c1, s1, n0, d01;
        CHECK(!pf_mat_offset(a, b, 65536, 5, out), "a 10-unit step passed a 5-unit limit");
        pf_mat_offset(a, b, 65536, 0, out);
        c1 = out[0];
        s1 = out[4];
        n0 = out[0] * out[0] + out[4] * out[4] + out[8] * out[8];
        d01 = out[0] * out[1] + out[4] * out[5] + out[8] * out[9];
        CHECK(out[3] > 19.99f && out[3] < 20.01f, "translation %f, want 20", out[3]);
        CHECK(n0 > 0.9999f && n0 < 1.0001f && d01 > -1e-4f && d01 < 1e-4f,
              "not orthonormal: |x|^2 %f, x.y %f", n0, d01);
        CHECK(s1 / c1 > 0.35f && s1 / c1 < 0.37f, "about 20 degrees: tan %f", s1 / c1);
    }
    g_camera_vt[0x54 / 4] = (void *)fake_set_transform;
    g_camera_obj.vt = g_camera_vt;
    *(void **)(g_view_obj + 0x104) = &g_camera_obj;
    g_view = g_view_obj;

    printf("camera: never moved, every frame shows it where the game has it\n");
    {
        static const float slowing[5] = { 196, 200, 203, 205, 206 };
        static float path[40], drawn[8];
        int m;
        unsigned i, j, n, extras, off;
        for (i = 0; i < 40; i++)
            path[i] = i < 20 ? 10.0f * (float)i : i < 25 ? slowing[i - 20] : 206.0f;
        for (m = 0; m <= 3; m++) {
            start_smoothing(120, m);
            pf_fps_set_units(m >= 2, 1);
            extras = off = 0;
            for (i = 0; i < 40; i++) {
                n = 0;
                cam_frame(100000 + 10000 * (unsigned)m + 33 * i, path[i], drawn, &n, 8);
                extras += n - 1;
                for (j = 0; j < n; j++)
                    off += drawn[j] != path[i];
            }
            CHECK(off == 0 && extras > 80 && !pf_fps_stopped(),
                  "FrameSmoothing=%d: %u of %u frames drawn off the game's camera, stopped %s",
                  m, off, extras + 40, pf_fps_stopped() ? pf_fps_stopped() : "no");
        }
    }

    printf("camera: still the game's while units go between game frames\n");
    {
        unsigned i, j, n;
        float drawn[8];
        g_unit.vt = g_camera_vt;
        start_smoothing(120, 3);
        pf_fps_set_units(1, 1);
        for (i = 0; i < 12; i++) {
            g_frame_start = 150000 + 33 * i;
            unit_at(&g_unit, 10.0f * (float)i);
            n = 0;
            cam_frame(150000 + 33 * i, 5.0f * (float)i, drawn, &n, 8);
            for (j = 0; j < n; j++)
                CHECK(drawn[j] == 5.0f * (float)i, "frame %u: camera drawn at %f, want %f", i,
                      drawn[j], 5.0f * (float)i);
        }
        CHECK(g_draws == 3 && g_draw_unit_tx[0] > 100.0f && g_draw_unit_tx[0] < 110.0f,
              "the unit wasn't moved between game frames: %d extra frames, at %f", g_draws,
              g_draw_unit_tx[0]);
    }

    printf("camera: an extra frame's draw that moves it stops extra frames\n");
    {
        float drawn[8];
        unsigned n = 0, i;
        start_smoothing(120, 3);
        pf_fps_set_units(1, 1);
        for (i = 0; i < 6; i++)
            cam_frame(160000 + 33 * i, 10.0f * (float)i, drawn, &n, 8);
        g_misbehave = 5;
        g_frame_start = 160000 + 33 * 6;
        camera_at(60.0f);
        wait_at(5);
        g_misbehave = 0;
        CHECK(pf_fps_stopped() && strstr(pf_fps_stopped(), "moved the camera") &&
                  g_camera_obj.m[3] == 60.0f && g_camera_obj.m[7] == 0.0f,
              "stopped %s, camera at %f, %f (want 60, 0)",
              pf_fps_stopped() ? pf_fps_stopped() : "no", g_camera_obj.m[3], g_camera_obj.m[7]);
    }

    printf("units moved ahead\n");
    g_unit.vt = g_camera_vt;
    g_unit2.vt = g_camera_vt;
    start_smoothing(60, 2);
    pf_fps_set_units(1, 0);
    /* Each game frame: its update sets the units, then the limiter waits. */
    g_frame_start = 20000;
    camera_at(0);
    unit_at(&g_unit, 0);
    unit_at(&g_unit2, 0);
    wait_at(5);
    g_frame_start = 20033;
    unit_at(&g_unit, 10);
    unit_at(&g_unit2, 500);
    g_draws = 0;
    wait_at(5);
    CHECK(g_draws == 1, "no extra frame");
    CHECK(g_seen_unit_tx > 14.9f && g_seen_unit_tx < 15.2f,
          "the extra frame saw the unit at %f, want 10 + 10 * 16.5/33", g_seen_unit_tx);
    CHECK(g_seen_unit2_tx == 500.0f, "a teleported unit was moved to %f", g_seen_unit2_tx);
    CHECK(g_unit.m[3] == 10.0f && g_unit2.m[3] == 500.0f, "units not put back: %f %f", g_unit.m[3],
          g_unit2.m[3]);
    {
        pf_fps_row row2;
        pf_fps_take_row(&row2);
        CHECK(row2.units == 1, "units moved %u, want 1", row2.units);
    }
    g_frame_start = 20066;
    unit_at(&g_unit, 20);
    unit_dies(&g_unit);
    g_set_transforms = 0;
    wait_at(5);
    CHECK(g_draws == 2 && g_seen_unit_tx == 20.0f && g_set_transforms == 0,
          "a destroyed unit was touched: x %f, %d sets", g_seen_unit_tx, g_set_transforms);
    CHECK(!pf_fps_stopped(), "stopped: %s", pf_fps_stopped() ? pf_fps_stopped() : "");
    start_smoothing(60, 1);
    pf_fps_set_units(1, 0);
    g_frame_start = 21000;
    unit_at(&g_unit, 0);
    wait_at(5);
    g_frame_start = 21033;
    unit_at(&g_unit, 10);
    wait_at(5);
    CHECK(g_seen_unit_tx == 10.0f, "FrameSmoothing=1 moved a unit to %f", g_seen_unit_tx);

    printf("units between the last two game frames (FrameSmoothing=3)\n");
    start_smoothing(60, 3);
    pf_fps_set_units(1, 1);
    camera_at(0);
    {
        float during;
        /* A few frames with extra frames: the delay is full by then. */
        unsigned t = run_frames(22000, 6, 5, 10.0f);
        g_frame_start = t;
        unit_at(&g_unit, 60.0f);
        /* The game's draw for this frame: its view update set x = 60, then the
         * particle update; the units go back when the draw returns. */
        pf_fps_draw_edge(1);
        draw_updates();
        during = g_unit.m[3];
        unit_at(&g_unit2, 77); /* placed by the pose update from the earlier transform */
        pf_fps_draw_edge(0);
        CHECK(during == 50.0f && g_unit.m[3] == 60.0f,
              "the game's draw showed the unit at %f (want 50), then %f (want 60)", during,
              g_unit.m[3]);
        g_draws = 0;
        wait_at(5);
        CHECK(g_draws == 1 && g_seen_unit_tx > 54.9f && g_seen_unit_tx < 55.2f,
              "the extra frame saw the unit at %f, want 50 + 10 * 16.5/33", g_seen_unit_tx);
        CHECK(g_unit.m[3] == 60.0f && !pf_fps_stopped(), "unit not put back: %f, stopped %s",
              g_unit.m[3], pf_fps_stopped() ? pf_fps_stopped() : "no");
        g_frame_start = t + 33;
    }
    {
        /* g_unit2's transform during the lag wasn't recorded as the game's. */
        unit_at(&g_unit2, 80);
        wait_at(5);
        CHECK(g_seen_unit2_tx == 80.0f, "a transform set during the lag was recorded: %f",
              g_seen_unit2_tx);
    }
    start_smoothing(60, 3);
    pf_fps_set_units(1, 0);
    run_frames(23000, 6, 5, 0.0f);
    g_frame_start = 23198;
    unit_at(&g_unit, 0);
    wait_at(5);
    g_frame_start = 23231;
    unit_at(&g_unit, 10);
    pf_fps_draw_edge(1);
    draw_updates();
    CHECK(g_unit.m[3] == 10.0f, "lagged without the draw hook: %f", g_unit.m[3]);
    pf_fps_draw_edge(0);
    wait_at(5);
    CHECK(g_seen_unit_tx > 14.9f && g_seen_unit_tx < 15.2f,
          "without the draw hook units should be moved ahead: %f", g_seen_unit_tx);

    printf("FrameSmoothing=3: the delay eases out when extra frames stop, and back in\n");
    {
        unsigned t, i;
        float shown[24];
        start_smoothing(60, 3);
        pf_fps_set_units(1, 1);
        t = run_frames(60000, 6, 5, 10.0f);
        for (i = 0; i < 24; i++, t += 33) {
            float x = 10.0f * (float)(6 + i);
            g_iconic = i >= 2 && i < 16; /* no extra frames for 14 game frames */
            g_frame_start = t;
            unit_at(&g_unit, x);
            pf_fps_draw_edge(1);
            draw_updates();
            shown[i] = g_unit.m[3] - x;
            pf_fps_draw_edge(0);
            CHECK(g_unit.m[3] == x, "frame %u: unit not put back: %f", i, g_unit.m[3]);
            wait_at(5);
        }
        g_iconic = 0;
        /* Full delay (-10) while extra frames come, kept through short gaps,
         * eased out a quarter a frame after 8 frames without, none (the game's
         * own position) after that, eased back in once they resume. */
        CHECK(shown[0] == -10.0f && shown[10] == -10.0f, "full delay: %f %f", shown[0],
              shown[10]);
        CHECK(shown[11] > -9.0f && shown[11] < -6.0f && shown[12] > shown[11] &&
                  shown[13] > shown[12],
              "easing out: %f %f %f", shown[11], shown[12], shown[13]);
        CHECK(shown[14] == 0.0f && shown[15] == 0.0f && shown[16] == 0.0f,
              "no extra frames: units as the game has them: %f %f %f", shown[14], shown[15],
              shown[16]);
        CHECK(shown[18] < 0.0f && shown[18] > -6.0f && shown[23] == -10.0f,
              "easing back in: %f, then %f", shown[18], shown[23]);
        for (i = 1; i < 24; i++)
            CHECK(shown[i] - shown[i - 1] < 2.6f && shown[i - 1] - shown[i] < 2.6f,
                  "frame %u: the delay jumped from %f to %f", i, shown[i - 1], shown[i]);
    }

    printf("FrameSmoothing=3: off for the session, units are drawn as the game has them\n");
    {
        unsigned t;
        start_smoothing(60, 3);
        pf_fps_set_units(1, 1);
        t = run_frames(65000, 6, 5, 10.0f);
        g_misbehave = 1; /* the next extra frame draws a logic random number */
        g_frame_start = t;
        unit_at(&g_unit, 60.0f);
        wait_at(5);
        g_misbehave = 0;
        CHECK(pf_fps_stopped() != NULL, "not stopped");
        g_frame_start = t + 33;
        unit_at(&g_unit, 70.0f);
        pf_fps_draw_edge(1);
        draw_updates();
        CHECK(g_unit.m[3] == 70.0f, "after stopping the game's draw showed the unit at %f",
              g_unit.m[3]);
        pf_fps_draw_edge(0);
    }

    printf("a game frame that overran (no wait) still counts as a frame\n");
    {
        start_smoothing(60, 2);
        pf_fps_set_units(1, 0);
        run_frames(70000, 3, 5, 0.0f);
        g_frame_start = 70099;
        unit_at(&g_unit, 0.0f);
        wait_at(5);
        g_frame_start = 70132; /* updated and drawn, but too late to wait */
        unit_at(&g_unit, 10.0f);
        g_frame_start = 70165;
        unit_at(&g_unit, 20.0f);
        g_draws = 0;
        wait_at(5);
        CHECK(g_draws == 1 && g_seen_unit_tx > 24.9f && g_seen_unit_tx < 25.2f,
              "after a frame without a wait the unit was moved to %f, want 20 + 10 * 0.5",
              g_seen_unit_tx);
    }

    printf("120 FPS: units placed by when each frame is shown, not by when the game frame "
           "started\n");
    {
        static const float want2[3] = { 12.5f, 15.0f, 17.5f }, want3[3] = { 2.5f, 5.0f, 7.5f };
        int m;
        for (m = 2; m <= 3; m++) {
            const float *want = m == 2 ? want2 : want3;
            unsigned t, j;
            start_smoothing(120, m);
            pf_fps_set_units(1, m == 3);
            /* The unit moves 10 a frame; each game frame is shown 5 ms after
             * it started (its update). The count rises to 3. */
            t = run_frames(40000, 11, 5, 10.0f);
            g_frame_start = t;
            unit_at(&g_unit, 110.0f);
            if (m == 3) {
                /* The game's own draw shows the unit at the last frame's 100. */
                pf_fps_draw_edge(1);
                draw_updates();
                CHECK(g_unit.m[3] == 100.0f, "FrameSmoothing=3: the game's draw showed %f",
                      g_unit.m[3]);
                pf_fps_draw_edge(0);
            }
            g_draws = 0;
            wait_at(5);
            CHECK(g_draws == 3, "FrameSmoothing=%d: %d extra frames, want 3", m, g_draws);
            for (j = 0; j < 3 && j < (unsigned)g_draws; j++) {
                float x = g_draw_unit_tx[j] - 100.0f;
                unsigned at = 5 + 33 * (j + 1) / 4;
                /* A quarter of the way to the game's next frame each time
                 * (+ up to 0.1 for the clock steps), not 5 ms further on. */
                CHECK(x > want[j] - 0.05f && x < want[j] + 0.15f,
                      "FrameSmoothing=%d: extra frame %u saw the unit at +%f, want +%f", m, j,
                      x, want[j]);
                /* Shown (started: the fake draw costs nothing) at 5 + 33 * (j + 1) / 4 ms. */
                CHECK(g_draw_at[j] + 1 >= at && g_draw_at[j] <= at + 1,
                      "FrameSmoothing=%d: extra frame %u started at %u ms, want %u", m, j,
                      g_draw_at[j], at);
            }
            CHECK(g_unit.m[3] == 110.0f && !pf_fps_stopped(), "unit not put back: %f, stopped %s",
                  g_unit.m[3], pf_fps_stopped() ? pf_fps_stopped() : "no");
            pf_fps_take_row(&row);
            CHECK(row.rate_changes >= 1 && row.rate_changes <= 3 && row.gaps > 30,
                  "FrameSmoothing=%d: count changes %u, gaps %u", m, row.rate_changes, row.gaps);
            CHECK(row.unit_steps >= 8 && row.unit_uneven == 0,
                  "FrameSmoothing=%d: steps %u, uneven %u (the unit moves evenly)", m,
                  row.unit_steps, row.unit_uneven);
        }
    }

    printf("uneven steps are counted\n");
    {
        static const float pp[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
        float p[12], c[12], c2[12];
        memcpy(p, pp, sizeof p);
        memcpy(c, pp, sizeof c);
        memcpy(c2, pp, sizeof c2);
        p[3] = 10;
        c[3] = 20;
        c2[3] = 25;
        CHECK(pf_step_uneven(pp, p, c) == 0, "even steps");
        CHECK(pf_step_uneven(pp, p, c2) == 1, "10 then 15");
        CHECK(pf_step_uneven(pp, pp, pp) == -1, "standing");
        start_smoothing(60, 2);
        pf_fps_set_units(1, 0);
        run_frames(50000, 3, 5, 10.0f);
        g_frame_start = 50099;
        unit_at(&g_unit, 40.0f); /* 0, 10, 20, then 40 */
        wait_at(5);
        pf_fps_take_row(&row);
        CHECK(row.unit_uneven == 1 && row.unit_steps == 2, "steps %u, uneven %u (want 2, 1)",
              row.unit_steps, row.unit_uneven);
    }

    printf("the view update and the particles are held in extra frames only\n");
    start(60);
    g_views_ran = g_particles_ran = 0;
    draw_updates();
    CHECK(g_views_ran == 1 && g_particles_ran == 1, "the game's own draw held them: %d %d",
          g_views_ran, g_particles_ran);
    g_frame_start = 24000;
    wait_at(5);
    pf_fps_take_row(&row);
    CHECK(g_draws == 1 && g_views_ran == 1 && g_particles_ran == 1 && row.held == 2,
          "extra frame: %d draws, views ran %d, particles ran %d, held %u", g_draws, g_views_ran,
          g_particles_ran, row.held);
    g_particles_vt[0x28 / 4] = (void *)other_update;
    g_frame_start = 24033;
    wait_at(5);
    CHECK(g_draws == 1 && pf_fps_stopped() && strstr(pf_fps_stopped(), "particle manager"),
          "a different particle update: %d draws, stopped %s", g_draws,
          pf_fps_stopped() ? pf_fps_stopped() : "no");
    g_particles_vt[0x28 / 4] = (void *)fake_particles_update;
    g_particles = 0;
    start(60);
    g_frame_start = 25000;
    wait_at(5);
    CHECK(g_draws == 1 && !pf_fps_stopped(), "no particle manager yet: %d draws, stopped %s",
          g_draws, pf_fps_stopped() ? pf_fps_stopped() : "no");
    g_particles = &g_particles_obj;

    printf("shadow map and reflection: reused by extra frames unless asked to redraw\n");
    {
        static const int want[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
        int m;
        for (m = 0; m < 4; m++) {
            start(60);
            pf_fps_set_passes(want[m][0], want[m][1]);
            g_shadow_ran = g_reflect_ran = 0;
            draw_updates();
            CHECK(g_shadow_ran == 1 && g_reflect_ran == 1,
                  "%d: a game frame's passes didn't run: %d %d", m, g_shadow_ran, g_reflect_ran);
            g_shadow_ran = g_reflect_ran = 0;
            g_frame_start = 26000 + 100 * m;
            wait_at(5);
            pf_fps_take_row(&row);
            CHECK(g_draws == 1 && g_shadow_ran == want[m][0] && g_reflect_ran == want[m][1] &&
                      row.shadows == (unsigned)want[m][0] &&
                      row.reflections == (unsigned)want[m][1],
                  "%d: extra frame drew shadow %d reflection %d (row %u %u), want %d %d", m,
                  g_shadow_ran, g_reflect_ran, row.shadows, row.reflections, want[m][0],
                  want[m][1]);
            CHECK(row.held == 2 && !pf_fps_stopped(), "%d: held %u, stopped %s", m, row.held,
                  pf_fps_stopped() ? pf_fps_stopped() : "no");
        }
    }

    printf("extra pass profiling: main thread, successful draws and drained rows\n");
    {
        unsigned mode, i;
        for (mode = 1; mode <= 2; mode++) {
            start(60);
            pf_fps_set_passes(1, 1);
            /* Markers outside an extra frame must not enter its row. */
            profile_draw_markers();
            g_profile_markers = (int)mode;
            g_frame_start = 28000 + 100 * mode;
            wait_at(5);
            g_profile_markers = 0;
            pf_fps_take_row(&row);
            CHECK(g_draws == 1 && row.profile.valid == 1 && !row.profile.invalid,
                  "mode %u: draws %d, valid %u invalid %u", mode, g_draws,
                  row.profile.valid, row.profile.invalid);
            for (i = 0; i < 4; i++) {
                CHECK(row.profile.samples[i] == 1,
                      "mode %u pass %u: samples %u", mode, i, row.profile.samples[i]);
                CHECK(row.profile.us[i] <= row.profile.total_us,
                      "mode %u pass %u exceeds total", mode, i);
            }
            pf_fps_take_row(&row);
            CHECK(!row.profile.valid && !row.profile.invalid && !row.profile.total_us,
                  "completed profile was not drained");
            for (i = 0; i < 4; i++)
                CHECK(!row.profile.samples[i] && !row.profile.us[i], "pass %u not drained", i);
        }
    }
    printf("extra pass profiling: safety failure discards completed pass samples\n");
    {
        static const int failures[] = {1, 2, 3, 5};
        unsigned m, i;
        for (m = 0; m < sizeof failures / sizeof failures[0]; m++) {
            start(60);
            g_profile_markers = 1;
            g_misbehave = failures[m];
            g_frame_start = 29000 + 100 * m;
            wait_at(5);
            g_profile_markers = g_misbehave = 0;
            pf_fps_take_row(&row);
            CHECK(g_draws == 1 && pf_fps_stopped() && !row.profile.valid &&
                      row.profile.invalid == 1 && !row.profile.total_us,
                  "failure %d: draws %d valid %u invalid %u", failures[m], g_draws,
                  row.profile.valid, row.profile.invalid);
            for (i = 0; i < 4; i++)
                CHECK(!row.profile.samples[i] && !row.profile.us[i],
                      "failure %d retained pass %u", failures[m], i);
        }
    }

    printf("not found\n");
    pf_fps_init(NULL, "TheDisplay", 60, 0, 0);
    pf_fps_set_installed(1, 1, 1, (void *)fake_particles_update);
    CHECK(pf_fps_stopped() && !strcmp(pf_fps_stopped(), "TheDisplay"), "reason %s",
          pf_fps_stopped() ? pf_fps_stopped() : "none");
    {
        pf_fps_sites s = sites();
        pf_fps_init(&s, NULL, 60, 0, 0);
        pf_fps_set_installed(1, 1, 1, NULL);
        CHECK(pf_fps_stopped() && strstr(pf_fps_stopped(), "particles"),
              "no particle hook: %s", pf_fps_stopped() ? pf_fps_stopped() : "none");
        pf_fps_init(&s, NULL, 60, 0, 0);
        pf_fps_set_installed(1, 1, 0, (void *)fake_particles_update);
        CHECK(pf_fps_stopped() && strstr(pf_fps_stopped(), "view update"),
              "no views hook: %s", pf_fps_stopped() ? pf_fps_stopped() : "none");
    }

    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: fps tests passed\n");
    return 0;
}
