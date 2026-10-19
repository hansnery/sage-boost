#include "catchup.h"

#include "log.h"
#include "notice.h"

/* ------------------------------------------------------------------ decision */

static unsigned ema(unsigned est, unsigned sample, unsigned samples)
{
    /* One slow frame (a pipeline compile, a verify sample) counts as at most
     * twice the estimate. */
    if (!samples)
        return sample;
    if (sample > 2 * est)
        sample = 2 * est;
    return est - est / 8 + sample / 8;
}

void pf_cu_core_init(pf_cu_core *c, unsigned max_skips)
{
    unsigned char *p = (unsigned char *)c;
    unsigned i;
    for (i = 0; i < sizeof *c; i++)
        p[i] = 0;
    c->max_skips = max_skips ? max_skips : 1;
    c->period = PF_CU_DEFAULT_PERIOD;
}

void pf_cu_core_begin(pf_cu_core *c, unsigned start_ms, int ok)
{
    unsigned sched, cap = PF_CU_MAX_DEBT_FRAMES * c->period;

    if (!c->seen || !ok || start_ms - c->prev_start > PF_CU_STALL_MS)
        sched = start_ms;
    else
        sched = c->sched + c->period;
    /* Started early (the frame time changed): the schedule moves up. */
    if ((int)(start_ms - sched) < 0)
        sched = start_ms;
    if (start_ms - sched > cap)
        sched = start_ms - cap;
    c->sched = sched;
    c->debt = start_ms - sched;
    c->prev_start = start_ms;
    c->seen = 1;
    c->ok = ok;
    c->decided = c->skip = 0;
}

int pf_cu_core_decide(pf_cu_core *c, unsigned now_ms)
{
    long long late_us, threshold;

    if (c->decided)
        return 0;
    c->decided = 1;
    c->skip = 0;
    if (c->ok && c->samples >= PF_CU_MIN_SAMPLES && c->in_row < c->max_skips) {
        /* Where the frame would end, drawn, against the next frame's start
         * on the schedule. */
        late_us = (long long)(int)(now_ms - c->sched) * 1000 + c->render_us + c->after_us -
                  (long long)c->period * 1000;
        threshold = c->render_us > PF_CU_MIN_LATE_US ? c->render_us : PF_CU_MIN_LATE_US;
        c->skip = late_us > threshold;
    }
    c->in_row = c->skip ? c->in_row + 1 : 0;
    return c->skip;
}

void pf_cu_core_render(pf_cu_core *c, unsigned us)
{
    c->render_us = ema(c->render_us, us, c->samples);
    c->samples++;
}

void pf_cu_core_after(pf_cu_core *c, unsigned us)
{
    c->after_us = ema(c->after_us, us, c->after_us ? 1 : 0);
}

int pf_cu_core_wait(pf_cu_core *c, unsigned now_ms, unsigned start_ms, unsigned period_ms)
{
    if (period_ms >= 10 && period_ms <= 200)
        c->period = period_ms;
    if (!c->ok || !c->debt)
        return 0;
    return now_ms - start_ms + c->debt >= c->period;
}

int pf_cu_core_behind(const pf_cu_core *c)
{
    return c->ok && c->debt > 0;
}

/* ---------------------------------------------------------------- the notice */

void pf_cu_notice_init(pf_cu_notice *n, unsigned min_skips)
{
    unsigned char *p = (unsigned char *)n;
    unsigned i;
    for (i = 0; i < sizeof *n; i++)
        p[i] = 0;
    if (!min_skips)
        min_skips = 1;
    n->min_skips = min_skips > PF_CU_NOTICE_MAX_SKIPS ? PF_CU_NOTICE_MAX_SKIPS : min_skips;
}

void pf_cu_notice_skip(pf_cu_notice *n, unsigned now_ms)
{
    unsigned i, recent = 0, need;
    n->at[n->head] = now_ms;
    n->head = (n->head + 1) % PF_CU_NOTICE_MAX_SKIPS;
    if (n->count < PF_CU_NOTICE_MAX_SKIPS)
        n->count++;
    for (i = 0; i < n->count; i++)
        recent += now_ms - n->at[i] < PF_CU_NOTICE_WINDOW_MS;
    if (n->shown && (int)(now_ms - n->until) >= 0)
        n->shown = 0;
    need = n->shown ? (n->min_skips + 1) / 2 : n->min_skips;
    if (recent >= need) {
        n->shown = 1;
        n->until = now_ms + PF_CU_NOTICE_MS;
    }
}

int pf_cu_notice_shown(pf_cu_notice *n, unsigned now_ms)
{
    if (n->shown && (int)(now_ms - n->until) >= 0)
        n->shown = 0;
    return n->shown;
}

/* ---------------------------------------------------------------- the game */

typedef DWORD(__stdcall *time_fn)(void);
typedef unsigned char(__attribute__((thiscall)) * bool_fn)(void *self);
typedef int(__attribute__((thiscall)) * int_fn)(void *self);
typedef void(__attribute__((thiscall)) * void_fn)(void *self);

static pf_cu_sites S;
static pf_cu_settings g_set;
static pf_cu_core g_core;
static pf_cu_notice g_notice;
static int g_on, g_in_draw, g_skipped_draw, g_cut_this_frame, g_any_skip;
static unsigned (*g_extras_in)(unsigned frame_start_ms);
static DWORD g_main;
static LONGLONG g_freq, g_site_t, g_draw_exit_t, g_minute_t;
static pf_cu_row g_row;
/* The minute's totals for the log line. */
static struct {
    unsigned frames, skipped, tracked, waits_cut, logic, behind_max_ms, objects, ticks;
    unsigned long long behind_ms, skip_us;
} g_min;
static unsigned g_minutes;
/* TheGameLogic's frame counter when last read. */
static unsigned g_last_tick;
static int g_tick_seen;

static LONGLONG qpc(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

static unsigned to_us(LONGLONG ticks)
{
    return ticks <= 0 || g_freq <= 0
               ? 0
               : (unsigned)((unsigned long long)ticks * 1000000ull / (unsigned long long)g_freq);
}

static unsigned now_ms(void)
{
    return ((time_fn)*S.time_import)();
}

void pf_cu_init(const pf_cu_settings *s, const pf_cu_sites *sites)
{
    LARGE_INTEGER f;
    unsigned char *r = (unsigned char *)&g_row, *m = (unsigned char *)&g_min;
    unsigned i;

    QueryPerformanceFrequency(&f);
    g_freq = f.QuadPart;
    g_set = *s;
    pf_cu_core_init(&g_core, s->max_skips);
    pf_cu_notice_init(&g_notice, s->notice_min);
    for (i = 0; i < sizeof g_row; i++)
        r[i] = 0;
    for (i = 0; i < sizeof g_min; i++)
        m[i] = 0;
    g_on = g_in_draw = g_skipped_draw = g_cut_this_frame = g_any_skip = 0;
    g_main = 0;
    g_site_t = g_draw_exit_t = g_minute_t = 0;
    g_minutes = g_last_tick = 0;
    g_tick_seen = 0;
    if (!s->enabled || !sites)
        return;
    S = *sites;
    g_on = 1;
}

void pf_cu_set_extras(unsigned (*extras_in)(unsigned frame_start_ms))
{
    g_extras_in = extras_in;
}

static int (*g_pacing_owner)(void);

void pf_cu_set_pacing_owner(int (*owns)(void))
{
    g_pacing_owner = owns;
}

int pf_cu_on(void)
{
    return g_on;
}

/* Skipping is for a running game only: the limiter pacing it, in a game
 * (not the shell), not paused, the client's time running (not a frozen
 * cinematic camera), no screenshot pending, no building being placed and the
 * camera not scrolling (a missing frame shows there), and no extra frames in
 * the last game frame (the game wasn't behind then). */
static int allowed(void)
{
    unsigned char *logic = *S.logic, *client = *S.client;
    if (g_pacing_owner && g_pacing_owner())
        return 0;
    if (!*S.limit_on || !logic || !client || logic[S.logic_paused] || !client[S.client_running])
        return 0;
    if (!((bool_fn)S.is_in_game)(logic) || *S.screenshots > 0)
        return 0;
    if (pf_ui_placing(&S.ui) || pf_ui_scrolling(&S.ui))
        return 0;
    return !g_extras_in || !g_core.seen || !g_extras_in(g_core.prev_start);
}

/* Steps of TheGameLogic's frame counter since the last read (read only; it
 * starts over at a new game). */
static void count_ticks(void)
{
    const unsigned char *logic = *S.logic;
    unsigned v, d;
    if (!S.logic_frame || !logic)
        return;
    v = *(const volatile unsigned *)(logic + S.logic_frame);
    if (g_tick_seen) {
        d = v >= g_last_tick ? v - g_last_tick : v;
        g_row.logic_ticks += d;
        g_min.ticks += d;
    }
    g_last_tick = v;
    g_tick_seen = 1;
}

/* The game's speed over `ms` in percent: logic frames against the game's
 * logic rate when its counter was found, else game frames against 30 (logic
 * takes one step per game frame). */
static unsigned speed_pct(unsigned ms, int *by_ticks)
{
    unsigned fps = S.logic_fps ? *S.logic_fps : 0;
    if (!ms)
        return 0;
    *by_ticks = S.logic_frame && fps >= 1 && fps <= 30;
    if (*by_ticks)
        return (unsigned)((unsigned long long)g_min.ticks * 100000 / ((unsigned long long)fps * ms));
    return (unsigned)((unsigned long long)g_min.frames * 100000 / (30ull * ms));
}

static void minute(LONGLONG now)
{
    unsigned drawn = g_min.frames - g_min.skipped, ms, speed;
    int by_ticks = 0;
    if (!g_minute_t) {
        g_minute_t = now;
        return;
    }
    if (now - g_minute_t < 60 * g_freq)
        return;
    ms = (unsigned)((unsigned long long)(now - g_minute_t) * 1000 / (unsigned long long)g_freq);
    speed = speed_pct(ms, &by_ticks);
    g_minute_t = now;
    g_minutes++;
    if (g_min.skipped || g_min.waits_cut)
        fu_log_event("catch-up: minute %u: %u of %u game frames drawn (%u renders skipped, %u "
                     "limiter waits ended at the schedule), game speed %u%% (%u logic frames%s), "
                     "behind the schedule %u ms on average, %u ms at most; render %u us, after "
                     "the draw %u us, a skipped frame's own work %u us",
                     g_minutes, drawn, g_min.frames, g_min.skipped, g_min.waits_cut, speed,
                     g_min.ticks,
                     by_ticks ? "" : ": the counter wasn't found, game frames / 30 instead",
                     g_min.tracked ? (unsigned)(g_min.behind_ms / g_min.tracked) : 0,
                     g_min.behind_max_ms, g_core.render_us, g_core.after_us,
                     g_min.skipped ? (unsigned)(g_min.skip_us / g_min.skipped) : 0);
    {
        unsigned char *m = (unsigned char *)&g_min;
        unsigned i;
        for (i = 0; i < sizeof g_min; i++)
            m[i] = 0;
    }
}

void pf_cu_frame_edge(int entering)
{
    LONGLONG t;
    if (!g_on)
        return;
    t = qpc();
    if (!entering) {
        if (g_draw_exit_t)
            pf_cu_core_after(&g_core, to_us(t - g_draw_exit_t));
        g_draw_exit_t = 0;
        return;
    }
    if (!g_main)
        g_main = GetCurrentThreadId();
    count_ticks();
    minute(t);
    pf_cu_core_begin(&g_core, *S.frame_start, allowed());
    g_cut_this_frame = 0;
    g_row.frames++;
    g_min.frames++;
    if (g_core.ok) {
        g_row.tracked++;
        g_min.tracked++;
        g_row.behind_ms += g_core.debt;
        g_min.behind_ms += g_core.debt;
        if (g_core.debt > g_row.behind_max_ms)
            g_row.behind_max_ms = g_core.debt;
        if (g_core.debt > g_min.behind_max_ms)
            g_min.behind_max_ms = g_core.debt;
    }
}

void pf_cu_draw_edge(int entering)
{
    LONGLONG t;
    if (!g_on)
        return;
    t = qpc();
    if (entering) {
        g_in_draw = 1;
        g_site_t = 0;
        g_skipped_draw = 0;
        return;
    }
    if (g_site_t && !g_skipped_draw)
        pf_cu_core_render(&g_core, to_us(t - g_site_t));
    else if (g_site_t)
        g_min.skip_us += to_us(t - g_site_t);
    g_draw_exit_t = t;
    g_in_draw = 0;
}

void pf_cu_logic(void)
{
    g_row.logic++;
    g_min.logic++;
}

/* What the skipped render would have advanced once per frame:
 * - the scene's update list, as the main view's Customized_Render walks it
 *   (W3D particle emission by the last sync step, dynamic light fades), all
 *   but the terrain, whose update only refreshes what it draws;
 * - InGameUI's floating 2D animations, moved one step per draw. */
static void frame_duties(void)
{
    unsigned char *scene = *S.scene, *ui = (unsigned char *)*S.ui.ui;
    if (scene && !*S.proj_override && !*S.shadow_flag) {
        unsigned char *sentinel = scene + PF_SCENE_UPDATE_LIST;
        unsigned char *node = *(unsigned char **)(scene + PF_SCENE_UPDATE_FIRST);
        unsigned guard = 0;
        while (node && node != sentinel && guard++ < 1000000) {
            unsigned char *iface = *(unsigned char **)(node + PF_NODE_OBJ);
            unsigned char *obj = iface ? iface - PF_NODE_OBJ_ADJUST : 0;
            if (obj) {
                void **vt = *(void ***)obj;
                if (((int_fn)vt[PF_ROBJ_CLASS_ID / 4])(obj) != PF_CLASSID_TERRAIN) {
                    ((void_fn)vt[PF_ROBJ_ON_FRAME_UPDATE / 4])(obj);
                    g_min.objects++;
                }
            }
            node = *(unsigned char **)(node + PF_NODE_NEXT);
        }
    }
    if (ui)
        ((void_fn)S.ui_anim)(ui);
}

void *pf_cu_render(void **slot, void *next)
{
    unsigned now;
    if (!g_on || !g_in_draw || GetCurrentThreadId() != g_main)
        return next;
    if (g_pacing_owner && g_pacing_owner())
        return next;
    if (!g_site_t)
        g_site_t = qpc();
    now = now_ms();
    if (!pf_cu_core_decide(&g_core, now))
        return next;
    /* The no-render path compares with BL, which is 0 here in the game. */
    if (slot[-6]) {
        g_on = 0;
        fu_log_event("catch-up: EBX isn't 0 at the render's start; catch-up off for this session");
        return next;
    }
    frame_duties();
    g_skipped_draw = 1;
    g_row.skipped++;
    g_min.skipped++;
    if (!g_any_skip)
        fu_log_event("catch-up: first render skipped: the frame started %u ms behind the "
                     "schedule (%u ms frames), render %u us, after the draw %u us",
                     g_core.debt, g_core.period, g_core.render_us, g_core.after_us);
    g_any_skip = 1;
    pf_cu_notice_skip(&g_notice, now);
    return (void *)S.no_render;
}

void pf_cu_wait(void **slot)
{
    unsigned period = (unsigned)(UINT_PTR)slot[-9];
    if (!g_on || GetCurrentThreadId() != g_main || g_cut_this_frame)
        return;
    if (g_pacing_owner && g_pacing_owner())
        return;
    /* The game's wait spins on this call: the clock is read only when behind. */
    if (!pf_cu_core_behind(&g_core)) {
        pf_cu_core_wait(&g_core, 0, 0, period);
        return;
    }
    if (pf_cu_core_wait(&g_core, now_ms(), *S.frame_start, period)) {
        /* The loop compares the elapsed time with ESI (the frame time) right
         * after this call, then reloads ESI from its frame: 0 ends it. */
        slot[-9] = 0;
        g_cut_this_frame = 1;
        g_row.waits_cut++;
        g_min.waits_cut++;
    }
}

void pf_cu_present(void)
{
    void *dev;
    if (!g_on || !g_set.notice || !g_any_skip || GetCurrentThreadId() != g_main || !S.device)
        return;
    if (!pf_cu_notice_shown(&g_notice, now_ms()) || !(dev = *S.device))
        return;
    pf_notice_draw(dev, g_set.text);
}

int pf_cu_behind(void)
{
    return g_on && pf_cu_core_behind(&g_core);
}

pf_cu_core *pf_cu_core_state(void)
{
    return &g_core;
}

void pf_cu_take_row(pf_cu_row *r)
{
    unsigned char *p = (unsigned char *)&g_row;
    unsigned i;
    g_row.render_us = g_core.render_us;
    g_row.after_us = g_core.after_us;
    g_row.ticks_on = g_on && S.logic_frame;
    *r = g_row;
    for (i = 0; i < sizeof g_row; i++)
        p[i] = 0;
}
