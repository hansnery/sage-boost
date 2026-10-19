/* Actual fps/catch-up adapters with owned engine state and a virtual clock.
 * No native game function, rendering, GPU, Present or network peer executes. */
#define main legacy_fps_test_main
#include "test_fps.c"
#undef main
#include "catchup.h"

static unsigned long long run_us;
static unsigned extra_us, sim_sequence, sim_draws, cut_waits;
static unsigned char logic_data[128], ui_data[32], limit_on = 1;
static void *ui_ptr = ui_data;
static int in_game = 1;

static unsigned runtime_fine(void)
{
    run_us += 10; /* bounded observer/admission cost, not a free clock */
    return (unsigned)run_us;
}

static DWORD __stdcall runtime_time(void)
{
    return (unsigned)(run_us / 1000u);
}

static unsigned char __attribute__((thiscall)) runtime_in_game(void *logic)
{
    (void)logic;
    return (unsigned char)in_game;
}

static void runtime_extra(int on)
{
    if (!on) {
        run_us += extra_us;
        sim_draws++;
    }
}

static pf_cu_sites runtime_sites(void)
{
    pf_cu_sites s;
    memset(&s, 0, sizeof s);
    s.frame_start = &g_frame_start;
    s.limit_on = &limit_on;
    s.time_import = &g_time_slot;
    s.client = (unsigned char *const *)&g_client;
    s.client_running = 4;
    s.logic = (unsigned char *const *)&g_logic;
    s.logic_paused = 16;
    s.logic_frame = 64;
    s.is_in_game = (void *)runtime_in_game;
    s.screenshots = &g_screenshots;
    s.ui.ui = &ui_ptr;
    s.ui.pending_off = 4;
    s.ui.scroll_off = 8;
    return s;
}

static void setup_runtime(int paired)
{
    pf_fps_sites s;
    pf_cu_sites cu;
    pf_cu_settings settings = {1, 1, 0, "", 1};
    g_client_vt[0x7C / 4] = g_getter;
    g_client_obj.vt = g_client_vt;
    g_client = &g_client_obj;
    g_client_obj.frame = 0;
    g_client_obj.pad[0] = 1;
    g_particles_vt[0x28 / 4] = (void *)fake_particles_update;
    g_particles_obj.vt = g_particles_vt;
    g_particles = &g_particles_obj;
    g_display_vt[12] = (void *)fake_draw;
    g_display_obj.vt = g_display_vt;
    g_display = &g_display_obj;
    g_logic = logic_data;
    g_camera_vt[0x54 / 4] = (void *)fake_set_transform;
    g_camera_obj.vt = g_camera_vt;
    camera_at(123.0f);
    memset(g_view_obj, 0, sizeof g_view_obj);
    *(void **)(g_view_obj + 0x104) = &g_camera_obj;
    g_view = g_view_obj;
    memset(logic_data, 0, sizeof logic_data);
    memset(ui_data, 0, sizeof ui_data);
    g_site[0] = 0xff;
    g_time_slot = (void *)runtime_time;
    *(void ***)(g_site + 8) = &g_time_slot;
    *(unsigned **)(g_site + 16) = &g_frame_start;
    s = sites();
    s.is_in_game = (void *)runtime_in_game;
    pf_fps_init(&s, NULL, 60, 3, 0);
    installed();
    pf_fps_set_units(1, 1);
    pf_fps_set_passes(1, 1);
    pf_fps_set_clock(runtime_fine);
    pf_fps_set_extra_listener(runtime_extra);
    cu = runtime_sites();
    pf_cu_init(&settings, &cu);
    pf_cu_set_extras(pf_fps_extras_in);
    pf_cu_set_pacing_owner(pf_fps_pair_owner);
    pf_fps_set_hold(pf_cu_behind);
    if (paired) CHECK(pf_fps_set_pair_pacing(&cu, 1), "paired adapter refused fixture");
    g_misbehave = g_iconic = g_screenshots = g_draw_spin_us = 0;
    g_verify = g_profile_markers = 0;
    g_shadow_ran = g_reflect_ran = g_draws = 0;
    g_views_ran = g_particles_ran = 0;
    g_seed = 17;
    in_game = limit_on = 1;
    extra_us = 12849;
    run_us = 1000000u;
    sim_sequence = sim_draws = cut_waits = 0;
}

static unsigned runtime_wait(unsigned period_ms)
{
    void *stack[16] = {0};
    unsigned char frame[64] = {0};
    void **slot = stack + 12;
    unsigned after_fps;
    *(unsigned **)(frame) = g_engine;
    slot[0] = g_site + 5;
    slot[-8] = frame + 0x20;
    slot[-9] = (void *)(UINT_PTR)period_ms;
    pf_fps_enter(PF_FPS_WAIT, slot, NULL, 4);
    after_fps = (unsigned)(UINT_PTR)slot[-9];
    if (!after_fps) cut_waits++;
    pf_cu_wait(slot);
    if (pf_fps_pair_owner())
        CHECK((unsigned)(UINT_PTR)slot[-9] == after_fps, "two owners changed wait threshold");
    return (unsigned)(UINT_PTR)slot[-9];
}

static void runtime_frame(unsigned regular_us, unsigned period_ms)
{
    unsigned attempts = 0;
    g_frame_start = runtime_time();
    pf_fps_frame_edge(1);
    pf_cu_frame_edge(1);
    if (pf_fps_pair_owner()) CHECK(!pf_cu_core_state()->ok, "catch-up owned a paired frame");
    g_client_obj.frame++;
    g_engine[0x34 / 4] = ++sim_sequence % 6;
    if (!(sim_sequence % 6)) (*(unsigned *)(logic_data + 64))++;
    run_us += regular_us;
    pf_fps_frame_edge(0);
    pf_cu_frame_edge(0);
    while (runtime_time() - g_frame_start < period_ms) {
        unsigned threshold = runtime_wait(period_ms);
        if (!threshold) break;
        run_us += 100; /* the real Sleep(0)/clock loop makes progress */
        CHECK(++attempts < 3000, "native wait failed to terminate");
        if (attempts >= 3000) break;
    }
}

static void closed_loop(int paired, unsigned regular, int jitter)
{
    unsigned i, before;
    unsigned long long start, elapsed;
    pf_fps_row row;
    setup_runtime(paired);
    /* Warm with enough room to learn the actual extra draw's cost. Reference
     * draw timing is deliberately not faked as an independently measured GPU. */
    for (i = 0; i < 20; i++) runtime_frame(8000, 33);
    start = run_us;
    before = sim_draws;
    pf_fps_take_row(&row);
    for (i = 0; i < 1800; i++)
        runtime_frame(regular + (jitter ? (i % 2 ? 2000 : -2000) : 0), 33);
    elapsed = run_us - start;
    pf_fps_take_row(&row);
    printf("adapter paired=%d regular=%u jitter=%d: 1800 ordinary, %u extra, %llu us, "
           "%u shortened waits, %u late, stop=%s\n", paired, regular, jitter,
           sim_draws - before, elapsed, cut_waits, row.late,
           pf_fps_stopped() ? pf_fps_stopped() : "none");
    CHECK(elapsed >= 59390000ull && elapsed <= 59410000ull,
          "ordinary cadence drifted: %llu us", elapsed);
    CHECK(row.extra == sim_draws - before, "extra accounting differs from dispatched draws");
    CHECK((unsigned)g_shadow_ran == sim_draws && (unsigned)g_reflect_ran == sim_draws,
          "extra frame did not invoke every fresh pass");
    CHECK(!g_views_ran && !g_particles_ran && g_camera_obj.m[3] == 123.0f,
          "extra frame advanced view/particle updates or moved camera");
    CHECK(!pf_fps_stopped() && !row.late, "stable work caused a stop/backoff");
    if (paired) CHECK(row.extra >= 1790, "paired adapter lost extras: %u", row.extra);
    else CHECK(row.extra == 0, "legacy unexpectedly admitted this workload: %u", row.extra);
}

int main(void)
{
    unsigned i;
    pf_cu_sites cu;
    closed_loop(0, 19854, 0);
    closed_loop(1, 19854, 0);
    closed_loop(1, 19854, 1);
    setup_runtime(1);
    for (i = 0; i < 360; i++) runtime_frame(19854, 33);
    {
        unsigned before = sim_draws;
        pf_fps_row row;
        pf_fps_take_row(&row);
        extra_us = 25000;
        for (i = 0; i < 900; i++) runtime_frame(19854, 33);
        pf_fps_take_row(&row);
        printf("adapter persistent cost: %u extra, %u probes, %u failed, resting=%s\n",
               sim_draws - before, row.probes[0], row.probes[2],
               pf_fps_resting() ? pf_fps_resting() : "none");
        CHECK(sim_draws - before <= 5 && row.probes[0] == 3 && row.probes[2] == 3,
              "unaffordable work was retried without bounds");
        CHECK(pf_fps_resting(), "three failed probes did not rest");
    }
    setup_runtime(1);
    for (i = 0; i < 360; i++) runtime_frame(19854, 33);
    {
        unsigned before;
        unsigned long long start = run_us;
        pf_fps_row row;
        pf_fps_take_row(&row);
        /* A full ordinary draw can cost more than the extra path, which
         * holds view/particle work. It must not make recovery impossible. */
        for (i = 0; i < 8; i++) game_draw(20000, 0);
        extra_us = 25000; /* missed prediction; actual completion beats any model */
        runtime_frame(19854, 33);
        extra_us = 12849;
        before = sim_draws;
        for (i = 0; i < 300; i++) runtime_frame(19854, 33);
        pf_fps_take_row(&row);
        printf("adapter recovery: %u extra draws after the spike, %u late, %llu us for 301 updates\n",
               sim_draws - before, row.late, run_us - start);
        CHECK(row.late == 1 && !pf_fps_stopped(), "spike handling failed");
        CHECK(sim_draws - before > 100, "learned cost starved extras after recovery");
        CHECK(run_us - start >= 9930000ull && run_us - start <= 9940000ull,
              "spike debt not repaid");
    }
    setup_runtime(1);
    for (i = 0; i < 20; i++) runtime_frame(8000, 33);
    CHECK(pf_fps_pair_owner(), "no paired owner after warmup");
    logic_data[16] = 1;
    runtime_frame(8000, 33);
    CHECK(!pf_fps_pair_owner(), "owned paused game");
    logic_data[16] = 0;
    runtime_frame(8000, 33);
    CHECK(pf_fps_pair_owner(), "did not resume");
    in_game = 0;
    runtime_frame(8000, 33);
    CHECK(!pf_fps_pair_owner(), "owned shell");
    in_game = 1;
    g_iconic = 1;
    runtime_frame(8000, 33);
    CHECK(!pf_fps_pair_owner(), "owned minimized window");
    g_iconic = 0;
    g_screenshots = 1;
    runtime_frame(8000, 33);
    CHECK(!pf_fps_pair_owner(), "owned screenshot");
    g_screenshots = 0;
    ui_data[8] = 1;
    runtime_frame(8000, 33);
    CHECK(!pf_fps_pair_owner(), "owned scrolling UI");
    ui_data[8] = 0;
    *(unsigned *)(ui_data + 4) = 1;
    runtime_frame(8000, 33);
    CHECK(!pf_fps_pair_owner(), "owned building placement");
    *(unsigned *)(ui_data + 4) = 0;
    runtime_frame(8000, 33);
    CHECK(pf_fps_pair_owner(), "did not resume after UI gates");
    runtime_frame(35000, 33);
    runtime_frame(35000, 33);
    CHECK(!pf_fps_pair_owner(), "retained ownership after frames with no wait");
    for (i = 0; i < 12; i++) runtime_frame(8000, 41);
    CHECK(pf_fps_pair_owner(), "did not acquire changed native period");
    g_misbehave = 1;
    runtime_frame(8000, 41);
    CHECK(pf_fps_stopped() && !pf_fps_pair_owner(), "state failure retained paired ownership");
    for (i = 2; i <= 5; i++) {
        if (i == 4) continue; /* the old fixture's millisecond-clock mutation */
        setup_runtime(1);
        runtime_frame(8000, 33);
        g_misbehave = (int)i;
        runtime_frame(8000, 33);
        CHECK(pf_fps_stopped() && !pf_fps_pair_owner(),
              "guard fault %u retained paired ownership", i);
    }
    setup_runtime(0);
    cu = runtime_sites();
    CHECK(!pf_fps_set_pair_pacing(&cu, 0), "enabled without frame hook");
    pf_fps_set_passes(0, 1);
    CHECK(!pf_fps_set_pair_pacing(&cu, 1), "enabled without fresh shadows");
    pf_fps_set_passes(1, 0);
    CHECK(pf_fps_set_pair_pacing(&cu, 1), "reflection preference blocked paired pacing");
    runtime_frame(8000, 33);
    CHECK(g_shadow_ran == 1 && g_reflect_ran == 0,
          "paired mode changed the user's reflection preference");
    printf("paired runtime adapter: %s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
