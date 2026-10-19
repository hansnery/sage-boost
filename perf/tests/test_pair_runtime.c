/* Actual fps/catch-up adapters with owned engine state and a virtual clock.
 * No native game function, rendering, GPU, Present or network peer executes. */
#define main legacy_fps_test_main
#include "test_fps.c"
#undef main
#include "catchup.h"
#include "pairpace.h"

static unsigned long long run_us;
static unsigned extra_us, sim_sequence, sim_draws, cut_waits;
static unsigned char logic_data[128], ui_data[32], limit_on = 1;
static void *ui_ptr = ui_data;
static int in_game = 1;
static unsigned frame_extra_no, slow_on, fail_on, gate_after;

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
    if (on) {
        frame_extra_no++;
        if (frame_extra_no == fail_on) g_misbehave = 1;
    }
    if (!on) {
        run_us += frame_extra_no == slow_on ? 30000u : extra_us;
        sim_draws++;
        if (frame_extra_no == gate_after) ui_data[8] = 1;
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

static void setup_runtime_limit(int paired, unsigned max_fps, unsigned limit)
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
    pf_fps_init(&s, NULL, max_fps, 3, 0);
    pf_fps_set_pair_limit(limit);
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
    frame_extra_no = slow_on = fail_on = gate_after = 0;
}

static void setup_runtime(int paired) { setup_runtime_limit(paired, 60, 1); }

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
    frame_extra_no = 0;
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

static void adaptive_loop(unsigned max_fps, unsigned limit, unsigned regular,
                          unsigned extra, unsigned count)
{
    unsigned i, before;
    unsigned long long start, elapsed;
    pf_fps_row row;
    setup_runtime_limit(1, max_fps, limit);
    extra_us = extra;
    for (i = 0; i < 30; i++) runtime_frame(regular, 33);
    before = sim_draws;
    start = run_us;
    pf_fps_take_row(&row);
    for (i = 0; i < 1800; i++) {
        runtime_frame(regular, 33);
        CHECK(frame_extra_no == count, "adaptive count %u != %u", frame_extra_no, count);
    }
    elapsed = run_us - start;
    pf_fps_take_row(&row);
    CHECK(elapsed >= 59390000ull && elapsed <= 59410000ull,
          "adaptive ordinary cadence drifted: %llu", elapsed);
    CHECK(row.extra == count * 1800 && row.extra == sim_draws - before,
          "adaptive extras missing or miscounted");
    CHECK(row.shadows == row.extra && row.reflections == row.extra &&
          (unsigned)g_shadow_ran == sim_draws, "adaptive shadow/reflection count mismatch");
    CHECK(g_client_obj.frame == sim_sequence && sim_sequence == 1830 &&
          *(unsigned *)(logic_data + 64) == 305 && g_seed == 17 &&
          !g_views_ran && !g_particles_ran && g_camera_obj.m[3] == 123.0f,
          "adaptive changed ordinary counters, random state or camera");
    CHECK(!pf_fps_stopped() && !row.late, "adaptive stable workload triggered backoff");
    printf("adaptive max=%u limit=%u regular=%u cost=%u: 1800 ordinary, %u extra, "
           "%llu us, max gap %u us, %u late\n", max_fps, limit, regular, extra,
           row.extra, elapsed, row.gap_max_us, row.late);
}

static void adaptive_faults(void)
{
    unsigned i, before;
    pf_fps_row row;
    setup_runtime_limit(1, 120, 3);
    extra_us = 4000;
    for (i = 0; i < 30; i++) runtime_frame(6000, 33);
    slow_on = 2;
    pf_fps_take_row(&row);
    runtime_frame(6000, 33);
    pf_fps_take_row(&row);
    CHECK(frame_extra_no == 2 && row.late == 1,
          "second-draw spike did not cancel third draw");
    slow_on = 0;
    before = sim_draws;
    for (i = 0; i < 500; i++) runtime_frame(6000, 33);
    CHECK(sim_draws - before > 600 && frame_extra_no == 3 && !pf_fps_stopped(),
          "adaptive failed to recover after spike");
    extra_us = 10000;
    for (i = 0; i < 30; i++) runtime_frame(6000, 33);
    CHECK(frame_extra_no <= 2, "increased cost retained three draws");
    /* Return to cheap work with a fresh controller for precise fault order. */
    setup_runtime_limit(1, 120, 3);
    extra_us = 4000;
    for (i = 0; i < 30; i++) runtime_frame(6000, 33);
    fail_on = 2;
    runtime_frame(6000, 33);
    CHECK(frame_extra_no == 2 && pf_fps_stopped() && !pf_fps_pair_owner(),
          "second-draw state fault allowed a third draw or retained ownership");
    setup_runtime_limit(1, 120, 3);
    extra_us = 4000;
    for (i = 0; i < 30; i++) runtime_frame(6000, 33);
    gate_after = 1;
    runtime_frame(6000, 33);
    CHECK(frame_extra_no == 1 && !pf_fps_pair_owner(),
          "UI eligibility change after first draw allowed another draw");
    gate_after = ui_data[8] = 0;
    runtime_frame(6000, 33);
    CHECK(frame_extra_no == 3, "adaptive did not resume after UI gate");
    runtime_frame(35000, 33);
    runtime_frame(35000, 33);
    CHECK(!pf_fps_pair_owner() && !frame_extra_no, "adaptive retained no-wait ownership");
    printf("adaptive second-draw spike/recovery/cost/state guards: checked\n");
}

static void adaptive_periods(void)
{
    unsigned i;
    unsigned long long start, expected = 0;
    pf_fps_row row;
    setup_runtime_limit(1, 120, 3);
    extra_us = 4000;
    for (i = 0; i < 30; i++) runtime_frame(6000, 33);
    start = run_us;
    pf_fps_take_row(&row);
    for (i = 0; i < 6000; i++) {
        unsigned period = 31 + i % 7;
        expected += period * 1000u;
        runtime_frame(5000 + (i % 7) * 1000, period);
        CHECK(frame_extra_no >= 2 && frame_extra_no <= 3 && pf_fps_pair_owner(),
              "changing native period lost or exceeded adaptive allowance");
    }
    pf_fps_take_row(&row);
    CHECK(run_us - start + 2000 >= expected && run_us - start <= expected + 2000 && !row.late,
          "adaptive native period changes drifted cadence");
    printf("adaptive changing-period updates: 6000 ordinary, %u extra, %llu us, %u late\n",
           row.extra, run_us - start, row.late);
}

static void adaptive_boundaries(void)
{
    pf_pairpace p;
    unsigned delay = 0, start, i;
    for (i = 0; i < 2; i++) {
        /* Same schedule near unsigned-clock wrap. */
        start = i ? 0xfffff000u : 1000000u;
        pf_pair_reset(&p);
        pf_pair_begin(&p, 1, start + 6000, 6000, 33000, 1);
        pf_pair_limit(&p, start + 6000, 4000, 3);
        CHECK(p.quota == 3, "cheap work did not qualify three extras");
        CHECK(pf_pair_plan(&p, start + 6000, 4000, 4000, &delay) && delay == 4250,
              "first subdivision not paced");
        CHECK(pf_pair_claim(&p, start + 10250, 4000), "first claim failed");
        pf_pair_begin(&p, 1, start + 14000, 14000, 33000, 1);
        pf_pair_limit(&p, start + 14000, 100, 3);
        CHECK(p.used == 1 && p.quota == 3, "repeat observation reopened allowance");
        CHECK(pf_pair_plan(&p, start + 14250, 4000, 4000, &delay) && delay == 4250,
              "second subdivision drifted from original anchor");
        CHECK(pf_pair_claim(&p, start + 18500, 4000), "second claim failed");
        CHECK(!pf_pair_claim(&p, start + 30000, 4000),
              "third draw borrowed another deadline allowance");
        CHECK(pf_pair_claim(&p, start + 26750, 4000), "third claim failed");
        CHECK(!pf_pair_claim(&p, start + 26750, 1), "fourth draw allowed");
        CHECK(p.deadline == start + 33000, "draws changed absolute deadline");
    }
    pf_pair_reset(&p);
    pf_pair_begin(&p, 1, 18000, 18000, 33000, 1);
    pf_pair_limit(&p, 18000, 8900, 3);
    CHECK(p.quota == 1, "heavy aggregate was allowed three draws");
    pf_pair_limit(&p, 18000, 100, 3);
    CHECK(p.quota == 1, "same-frame reconfiguration enlarged quota");
    pf_pair_reset(&p);
    pf_pair_begin(&p, 1, 6000, 6000, 33000, 1);
    pf_pair_limit(&p, 6000, ~0u, 3);
    CHECK(p.quota == 1, "cost overflow enlarged quota");
    printf("adaptive policy: deadline, anchor, wrap and quota boundaries checked\n");
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

static void intermittent_recovery(void)
{
    pf_fps_row row;
    unsigned before;
    unsigned long long start;
    setup_runtime(1);
    for (unsigned i = 0; i < 120; i++) runtime_frame(8000, 33);
    for (unsigned i = 0; i < 8; i++) game_draw(20000, 0);
    /* An expensive extra fits a light ordinary frame without being late.
     * Later occasional light frames permit cheap extras, but fewer than
     * 60 consecutive blocked frames occur before each such success. */
    extra_us = 18500;
    runtime_frame(8000, 33);
    extra_us = 12849;
    before = sim_draws;
    start = run_us;
    pf_fps_take_row(&row);
    for (unsigned i = 0; i < 1800; i++)
        runtime_frame(i % 50 == 0 ? 8000 : 19854, 33);
    pf_fps_take_row(&row);
    CHECK(sim_draws - before > 1700 && row.probes[0] == 1 && row.probes[1] == 1,
          "intermittent successes starved bounded recovery");
    CHECK(!row.late && !pf_fps_stopped() && run_us - start >= 59390000ull &&
          run_us - start <= 59410000ull, "intermittent recovery changed cadence");
    CHECK(row.shadows == row.extra && row.reflections == row.extra &&
          g_client_obj.frame == sim_sequence && g_seed == 17 &&
          !g_views_ran && !g_particles_ran && g_camera_obj.m[3] == 123.0f,
          "intermittent recovery changed guarded state or full-rate passes");
    printf("intermittent recovery: %u extras, %u probe, %u late, %llu us\n",
           sim_draws - before, row.probes[0], row.late, run_us - start);
}

int main(void)
{
    intermittent_recovery();
    unsigned i;
    pf_cu_sites cu;
    adaptive_boundaries();
    adaptive_loop(120, 3, 6000, 4000, 3);
    adaptive_loop(120, 1, 6000, 4000, 1);
    adaptive_loop(90, 3, 6000, 4000, 2);
    adaptive_loop(60, 3, 6000, 4000, 1);
    adaptive_loop(120, 3, 12000, 7000, 2);
    adaptive_loop(120, 3, 18000, 8900, 1);
    adaptive_faults();
    adaptive_periods();
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
