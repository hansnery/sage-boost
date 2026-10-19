#include "fps.h"

#include "btnclock.h"
#include "fpsmath.h"
#include "log.h"
#include "pairpace.h"
#include "sites.h"
#include "stubs.h"

/* Left before the deadline: 2 ms, 1 ms for the game clock's resolution, and
 * an eighth of the expected cost for how much one extra frame varies. */
#define PF_FPS_MARGIN_US 3000u
#define PF_FPS_SLACK_US 500u /* a planned start may be reached this late and still fit */
/* Late game frames. A late one costs a few ms of one frame, so isolated
 * ones only pause extra frames; they rest only if lates persist after
 * backing off, or add up to over 1% of a minute. */
#define PF_FPS_GRACE_MS 10000u     /* not counted after the first frame or a stall */
#define PF_FPS_STALL_MS 250u       /* a game frame this long is a stall (a load) */
#define PF_FPS_MINOR_MS 4u         /* a late up to this pauses 1 s and doesn't escalate */
#define PF_FPS_BACKOFF_MS 1000u    /* the pause after a late frame, doubling on repeats */
#define PF_FPS_BACKOFF_MAX_MS 8000u
#define PF_FPS_REPEAT_MS 10000u    /* a late within this of the last one is a repeat */
#define PF_FPS_REPEATS_OFF 5       /* counted repeats in a row (pauses 1, 2, 4, 8 s): rest */
#define PF_FPS_MINUTE_PERMILLE 10u /* counted lateness over 1% of the last 60 s: rest */
#define PF_FPS_LOG_MS 2000u        /* at most one late-frame line per 2 s */
/* Lates that persist rest extra frames for 1 minute, then 2, then 4; the
 * 4th time they are off for the session. */
#define PF_FPS_REST_MS 60000u
#define PF_FPS_RESTS_OFF 4u
/* The reference: what an extra frame is expected to cost with no
 * measurement to go by. The game's own draw (W3DDisplay::draw, renders
 * catch-up skipped left out) once this many have been timed, else 10 ms;
 * never under 1 ms, nor under the cost of an extra frame that made a game
 * frame late when it was expected at the reference. */
#define PF_FPS_REF_DRAWS 8u
#define PF_FPS_FIRST_COST_US 10000u
#define PF_FPS_MIN_COST_US 1000u
/* The expected cost is the slowest extra frame of the last 64 game frames
 * that drew one (about 2 s): one slow frame keeps the headroom for that long
 * instead of the 0.7 s a decaying maximum gave it. */
#define PF_FPS_COST_FRAMES 64u
/* Game frames that had room for an extra frame of the reference cost but
 * not of the expected cost, before the slowest samples give way to the
 * reference, one per such frame (they may have been measured during a load
 * or a stall). Frames without that room (no idle time, catching up, paused)
 * leave the estimate as it is: they say nothing about what a frame costs. */
#define PF_FPS_DRY_FRAMES 30u
/* GameEngine +0x0C (the frame limit) .. +0x5F: the frame-in-tick counter,
 * the interpolation ratio and the frame-rate metrics. */
#define PF_ENGINE_FROM 0x0C
#define PF_ENGINE_WORDS 21

typedef void(__attribute__((thiscall)) * draw_fn)(void *self);
typedef unsigned char(__attribute__((thiscall)) * bool_fn)(void *self);
typedef unsigned(__attribute__((thiscall)) * crc_fn)(void *self, int arg);
typedef void(__attribute__((thiscall)) * set_transform_fn)(void *self, const float *m);
typedef int(__stdcall *iconic_fn)(void *hwnd);
typedef DWORD(__stdcall *time_fn)(void);

static pf_fps_sites S;
static int g_on, g_in_extra, g_verbose;
/* Logged once each: the first button clock the game's draw drew, the first
 * an extra frame drew, and the first game frame with a clock request an
 * extra frame didn't draw. */
static int g_btn_noted_logged, g_btn_asked_logged, g_btn_unseen_logged;
static pf_btn_stats g_btn_row; /* the totals when the last row was taken */
static const char *g_stopped;
static unsigned g_extras;
static void *g_particles_update; /* the hooked particle manager update */
/* ExtraFrameShadows / ExtraFrameReflections: extra frames redraw these. */
static int g_redraw_shadows, g_redraw_reflections;
/* Late frames: the pause, the grace period, the chain of repeats and a
 * minute of one-second buckets of counted lateness. */
static unsigned g_pause_until, g_backoff_ms, g_last_late, g_grace_until, g_log_at,
    g_log_skipped;
static int g_paused, g_any_late, g_logged, g_repeats, g_counted_repeats;
static struct {
    unsigned sec, late_ms;
} g_minute[60];
/* Resting after lates persisted: how many times so far, and why (NULL when
 * not resting). */
static unsigned g_rests;
static const char *g_rest_why;
static DWORD g_main;
static LONGLONG g_freq;
static time_fn g_time;
static const unsigned *g_frame_start_ms; /* the limiter's start of the current game frame */
static int g_client_off = -1;            /* the client frame field, from its getter */

/* The camera is never moved: every frame, extra or not, is drawn from the
 * camera the game's view update set, the one the shadow map's light camera
 * and the terrain's loaded area were fitted to (W3DView::setCameraTransform).
 * A camera moved behind the view's back shows ground neither covers. */
static unsigned g_seq;
static int g_smoothing;

/* Units: every HLod's transform at the last two game frames, by object.
 * Open addressing; an entry is reused once it is two frames stale. */
#define PF_UNITS 4096u
#define PF_UNIT_PROBES 32u
#define PF_UNIT_MAX_STEP 60u /* world units per game frame; more is a teleport */
#define PF_UNIT_GONE ((void *)1)

typedef struct {
    void *obj;
    unsigned pprev_seq, prev_seq, cur_seq;
    int moved;
    float pprev[12], prev[12], cur[12];
} unit_rec;

static unit_rec g_units[PF_UNITS];
static int g_units_on;
static unsigned g_prev_frame_ms;
/* FrameSmoothing=3: the game's draw shows units one game frame behind (from
 * the particle update, after the view update set this frame's transforms,
 * until the draw returns), and extra frames go between the last two. */
static int g_lag_on, g_in_draw, g_lagged;
/* How far behind the game's own frames show units (q16 of a frame's step):
 * 1 while extra frames run, eased to 0 over a few game frames once none have
 * been drawn for a while (so with none, units are drawn as the game has
 * them), and back up when they resume. */
#define PF_LAG_FULL 65536u
#define PF_LAG_STEP 16384u    /* per game frame: a quarter */
#define PF_LAG_IDLE_FRAMES 8u /* game frames without an extra frame before easing out */
static unsigned g_lag, g_lag_shown, g_frames_without_extra = PF_LAG_IDLE_FRAMES;
/* The limiter's frame start that g_seq counts: a game frame that overran
 * its 33 ms never waits, so frames are counted from the start, not the wait. */
static unsigned g_seq_start;
static int g_seq_known;

/* The current game frame: its start on the game's clock, when it was shown
 * (its first wait, right after its draw presented) on the fine clock, the
 * extra frames planned for it and when each is to start (us after it was
 * shown), and how many were drawn. */
static unsigned g_frame_ms, g_done, g_drawn;
static unsigned g_shown_us, g_prev_shown_us, g_budget_us, g_period_us;
static unsigned g_plan_n, g_plan_start[4], g_cur_n, g_up, g_last_n;
/* Shown frames (the game's and extra ones) for the CSV: the last one. */
static unsigned g_last_shown_us;
static int g_have_shown;
/* The extra-frame count rises only after the larger count fit this many
 * game frames in a row (it drops at once), so it doesn't flicker. */
#define PF_FPS_RISE_FRAMES 8u
/* The expected cost of an extra frame: the slowest of the last game frames
 * that drew one (g_cost_us caches the maximum), and the average. A sample
 * that gave way to the reference is marked; the expected cost is a guess
 * while none has been measured or its maximum is such a one. */
static unsigned g_cost_ring[PF_FPS_COST_FRAMES], g_cost_head, g_cost_frame_max;
static unsigned char g_cost_given[PF_FPS_COST_FRAMES];
static unsigned g_cost_us, g_cost_avg_us, g_dry;
static int g_cost_guess;
/* This game frame had room for an extra frame of the reference cost only. */
static int g_blocked;
/* A stale learned floor is retried only after sustained spare capacity. */
#define PF_FPS_PROBE_FRAMES 300u
#define PF_FPS_PROBE_FAILURES 3u
static unsigned g_probe_ready, g_probe_cost, g_probe_failures, g_block_bits;

/* The game's own draw: its average cost, draws timed, and the one being
 * timed. g_learned_us: the reference's floor after a guess made a frame late. */
static unsigned g_ref_us, g_ref_n, g_learned_us, g_draw_depth;
static LONGLONG g_draw_t0;
static int g_draw_skipped;
static LONGLONG g_prev_wait, g_idle_ticks; /* summed in ticks: the gaps are sub-microsecond */
static int g_seen_frame;
static pf_fps_row g_row;
static pf_ep g_extra_profile;
static pf_pairpace g_pair;
static pf_cu_sites g_pair_sites;
static int g_pair_enabled;
static unsigned g_pair_limit;
static unsigned g_pair_sequence, g_pair_frames, g_pair_draws, g_pair_cuts;
static unsigned g_pair_peak_debt, g_pair_misses;
static unsigned g_pair_good_cost, g_pair_good_samples, g_pair_probe_frames, g_pair_probe_failures;
static int g_pair_cut, g_pair_attempted;
static void blocked(unsigned reason)
{
    if (!(g_block_bits & (1u << reason))) {
        g_block_bits |= 1u << reason;
        g_row.blocked[reason]++;
    }
}

unsigned pf_fps_schedule(const pf_fps_sched *s, unsigned *start)
{
    unsigned n, j, last, latest = 0, shown_by = 0;
    int room = s->budget >= s->cost + s->margin + PF_FPS_SLACK_US;

    if (room) {
        /* The last start that still fits, with a little slack for when the
         * pacing gets there late. */
        latest = s->budget - s->cost - s->margin - PF_FPS_SLACK_US;
        shown_by = latest + s->avg;
    }
    for (n = s->extras; n > 0 && room; n--) {
        unsigned even = (unsigned)((unsigned long long)s->period * n / (n + 1));
        int squeezed;
        /* Back to back they must still fit. */
        if (s->period / (n + 1) < s->cost)
            continue;
        if ((even > s->avg ? even - s->avg : 0) <= latest) {
            /* Shown at period * j / (n + 1) after the game's frame, so
             * started the average cost earlier. */
            squeezed = 0;
        } else if ((unsigned long long)shown_by * 10 >= (unsigned long long)even * 7 &&
                   shown_by / n >= s->cost) {
            /* The game's next frame starts before then (this one was shown
             * late in its 33 ms): the same, closer together, the last as
             * late as fits. */
            squeezed = 1;
        } else {
            continue;
        }
        for (j = 1; j <= n; j++) {
            unsigned at = squeezed ? (unsigned)((unsigned long long)shown_by * j / n)
                                   : (unsigned)((unsigned long long)s->period * j / (n + 1));
            start[j - 1] = at > s->avg ? at - s->avg : 0;
        }
        return n;
    }
    /* None fits evenly (the game's frame came late): one, as late as fits,
     * if it is shown far enough from the game's own frame to be worth it. */
    if (s->extras && room) {
        last = latest;
        if (last + s->avg > s->period / 2)
            last = s->period / 2 > s->avg ? s->period / 2 - s->avg : 0;
        if (last + s->avg >= s->period / 8) {
            start[0] = last;
            return 1;
        }
    }
    return 0;
}

/* A draw of the game's own frame took us: the reference's average, in
 * which one slow draw (a verify sample, a pipeline compile) counts at most
 * twice. */
static void ref_measured(unsigned us)
{
    if (!us)
        us = 1;
    if (g_ref_n && us > 2 * g_ref_us)
        us = 2 * g_ref_us;
    g_ref_us = g_ref_n ? g_ref_us - g_ref_us / 8 + us / 8 : us;
    if (g_ref_n < PF_FPS_REF_DRAWS)
        g_ref_n++;
}

static unsigned reference_cost(void)
{
    unsigned c = g_ref_n >= PF_FPS_REF_DRAWS ? g_ref_us : PF_FPS_FIRST_COST_US;
    if (c < g_learned_us)
        c = g_learned_us;
    return c > PF_FPS_MIN_COST_US ? c : PF_FPS_MIN_COST_US;
}

static unsigned expected_cost(void)
{
    return g_cost_us ? g_cost_us : reference_cost();
}

static int cost_is_guess(void)
{
    return !g_cost_us || g_cost_guess;
}

static unsigned margin_for(unsigned cost)
{
    return PF_FPS_MARGIN_US + cost / 8;
}

/* Only from an extra frame that was drawn. */
static void cost_measured(unsigned us)
{
    if (!us)
        us = 1; /* 0 is "not measured yet" */
    g_cost_avg_us = g_cost_avg_us ? g_cost_avg_us - g_cost_avg_us / 8 + us / 8 : us;
    if (us > g_cost_frame_max)
        g_cost_frame_max = us;
    if (us > g_cost_us) {
        g_cost_us = us;
        g_cost_guess = 0;
    }
}

static void cost_max(void)
{
    unsigned i, m = 0;
    int given = 0;
    for (i = 0; i < PF_FPS_COST_FRAMES; i++) {
        if (g_cost_ring[i] > m) {
            m = g_cost_ring[i];
            given = g_cost_given[i];
        } else if (g_cost_ring[i] == m && !g_cost_given[i]) {
            given = 0;
        }
    }
    g_cost_us = m;
    g_cost_guess = given;
}

static void cost_push(unsigned us)
{
    g_cost_ring[g_cost_head] = us;
    g_cost_given[g_cost_head] = 0;
    g_cost_head = (g_cost_head + 1) % PF_FPS_COST_FRAMES;
    cost_max();
}

/* Only the expected cost kept extra frames out for a while: the average
 * eases toward the reference, and the slowest sample gives way to the
 * larger of the two. Neither goes below the reference. */
static void cost_forget_slowest(void)
{
    unsigned i, at = 0, ref = reference_cost(), to;
    if (g_cost_avg_us > ref)
        g_cost_avg_us -= (g_cost_avg_us - ref + 15) / 16;
    to = g_cost_avg_us > ref ? g_cost_avg_us : ref;
    for (i = 1; i < PF_FPS_COST_FRAMES; i++)
        if (g_cost_ring[i] > g_cost_ring[at])
            at = i;
    if (g_cost_ring[at] > to) {
        g_cost_ring[at] = to;
        g_cost_given[at] = 1;
    }
    cost_max();
    if (g_cost_us < g_cost_avg_us) {
        g_cost_us = g_cost_avg_us;
        g_cost_guess = 1;
    }
}

/* At the end of each game frame. Only a frame that drew an extra frame, or
 * one in which nothing but the expected cost kept them out (plan_frame),
 * changes the estimate: frames without idle time, caught up or paused
 * leave it, so a slow frame's cost still sets the headroom when there is
 * room again. */
static void cost_frame_end(void)
{
    if (g_drawn) {
        g_dry = 0;
        cost_push(g_cost_frame_max);
    } else if (g_blocked) {
        if (g_dry <= PF_FPS_DRY_FRAMES)
            g_dry++;
        if (g_dry > PF_FPS_DRY_FRAMES && g_cost_us)
            cost_forget_slowest();
    }
    g_blocked = 0;
    g_cost_frame_max = 0;
}

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

/* The fine clock frames are timed on: microseconds, wrapping (only
 * differences are used). QPC unless a test set its own. */
static unsigned qpc_us(void)
{
    unsigned long long t = (unsigned long long)qpc(), f = (unsigned long long)g_freq;
    return f ? (unsigned)(t / f * 1000000ull + t % f * 1000000ull / f) : 0;
}
static unsigned (*g_fine)(void) = qpc_us;

void pf_fps_set_clock(unsigned (*now_us)(void))
{
    g_fine = now_us ? now_us : qpc_us;
}

/* A frame was shown now (the game's at its first wait, an extra one when
 * its draw returned): the gaps between them, for the CSV. */
static void shown(unsigned now)
{
    if (g_have_shown) {
        unsigned gap = now - g_last_shown_us;
        if (gap < 200000) {
            g_row.gap_us += gap;
            g_row.gaps++;
            if (gap > g_row.gap_max_us)
                g_row.gap_max_us = gap;
        }
    }
    g_last_shown_us = now;
    g_have_shown = 1;
}

static void (*g_extra_listener)(int on);
static int (*g_hold)(void);
/* Where Customized_Render's update loop takes the next object, and whether
 * the current extra frame has held it yet (counted once per extra frame). */
static void *g_scene_skip;
static int g_scene_held;

void pf_fps_set_scene_skip(void *next_object)
{
    g_scene_skip = next_object;
}

void *pf_fps_scene_update(void *next)
{
    if (!g_in_extra || !g_scene_skip || GetCurrentThreadId() != g_main)
        return next;
    if (!g_scene_held) {
        g_scene_held = 1;
        g_row.held++;
    }
    return g_scene_skip;
}

void pf_fps_set_extra_listener(void (*fn)(int on))
{
    g_extra_listener = fn;
}

void pf_fps_set_hold(int (*behind)(void))
{
    g_hold = behind;
}

unsigned pf_fps_extras_in(unsigned frame_start_ms)
{
    return g_on && !g_stopped && g_seen_frame && g_frame_ms == frame_start_ms ? g_drawn : 0;
}

static void set_extra(int on)
{
    g_in_extra = on;
    if (g_extra_listener)
        g_extra_listener(on);
}

static void stop(const char *why)
{
    if (g_stopped)
        return;
    g_stopped = why;
    fu_log_event("fps: extra frames off for this session: %s", why);
}

void pf_fps_init(const pf_fps_sites *sites, const char *why_not, unsigned max_fps, int smoothing,
                 int verbose)
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_freq = f.QuadPart;
    g_verbose = verbose;
    g_smoothing = smoothing;
    g_seq = 0;
    g_seq_known = 0;
    g_lag = g_lag_shown = 0;
    g_frames_without_extra = PF_LAG_IDLE_FRAMES;
    g_units_on = g_lag_on = g_in_draw = g_lagged = 0;
    g_prev_frame_ms = 0;
    g_particles_update = 0;
    g_redraw_shadows = g_redraw_reflections = 0;
    g_btn_noted_logged = g_btn_asked_logged = g_btn_unseen_logged = 0;
    pf_btn_get_stats(&g_btn_row);
    {
        unsigned i;
        for (i = 0; i < PF_UNITS; i++) {
            g_units[i].obj = 0;
            g_units[i].moved = 0;
            g_units[i].prev_seq = g_units[i].cur_seq = 0;
        }
        for (i = 0; i < PF_FPS_COST_FRAMES; i++) {
            g_cost_ring[i] = 0;
            g_cost_given[i] = 0;
        }
    }
    g_on = g_in_extra = g_seen_frame = 0;
    g_pair_enabled = 0;
    g_pair_limit = 1;
    g_pair_sequence = g_pair_frames = g_pair_draws = g_pair_cuts = 0;
    g_pair_peak_debt = g_pair_misses = 0;
    g_pair_good_cost = g_pair_good_samples = g_pair_probe_frames = g_pair_probe_failures = 0;
    g_pair_cut = g_pair_attempted = 0;
    pf_pair_reset(&g_pair);
    g_stopped = 0;
    g_done = g_drawn = g_cost_us = g_cost_avg_us = g_dry = g_cost_head = g_cost_frame_max = 0;
    g_cost_guess = g_blocked = g_draw_skipped = 0;
    g_ref_us = g_ref_n = g_learned_us = g_draw_depth = 0;
    g_rests = 0;
    g_probe_ready = g_probe_cost = g_probe_failures = g_block_bits = 0;
    g_rest_why = 0;
    g_paused = g_any_late = g_logged = g_repeats = g_counted_repeats = 0;
    g_pause_until = g_backoff_ms = g_last_late = g_grace_until = g_log_at = g_log_skipped = 0;
    g_idle_ticks = 0;
    {
        unsigned i;
        for (i = 0; i < 60; i++)
            g_minute[i].sec = g_minute[i].late_ms = 0;
    }
    g_time = 0;
    g_client_off = -1;
    pf_ep_init(&g_extra_profile);
    pf_fps_take_row(&g_row);
    g_extras = max_fps > 30 ? max_fps / 30 - 1 : 0;
    if (g_extras > sizeof g_plan_start / sizeof g_plan_start[0])
        g_extras = sizeof g_plan_start / sizeof g_plan_start[0];
    g_shown_us = g_prev_shown_us = g_budget_us = g_period_us = g_plan_n = g_up = 0;
    g_cur_n = g_last_n = g_extras;
    g_have_shown = 0;
    if (!g_extras)
        return;
    if (!sites) {
        g_stopped = why_not ? why_not : "the game's draw state wasn't found";
        return;
    }
    S = *sites;
}

void pf_fps_set_installed(int wait, int avg, int views, void *particles_update)
{
    if (!g_extras || g_stopped)
        return;
    if (!wait || !avg || !views || !particles_update) {
        g_stopped = !wait  ? "the frame limiter hook isn't installed"
                    : !avg ? "the average frame rate hook isn't installed"
                           : "the hooks that hold the view update, the particles and the "
                             "scene's update list in extra frames aren't installed (effects "
                             "would run too fast)";
        return;
    }
    g_particles_update = particles_update;
    g_on = 1;
}

void pf_fps_set_passes(int shadows, int reflections)
{
    g_redraw_shadows = shadows;
    g_redraw_reflections = reflections;
}

void pf_fps_set_units(int installed, int lag_installed)
{
    g_units_on = installed && g_on && !g_stopped && g_smoothing >= 2;
    g_lag_on = g_units_on && g_smoothing >= 3 && lag_installed;
    if (g_smoothing >= 2 && !g_units_on && g_on)
        fu_log_event("fps: the unit hooks aren't installed; units aren't moved ahead");
    else if (g_smoothing >= 3 && g_units_on && !g_lag_on)
        fu_log_event("fps: the draw hook isn't installed; units are moved ahead instead of "
                     "between the last two game frames");
}

int pf_fps_in_extra(void)
{
    return g_in_extra;
}

const char *pf_fps_stopped(void)
{
    return g_stopped;
}

const char *pf_fps_resting(void)
{
    return g_stopped ? 0 : g_rest_why;
}

static void btn_report(void)
{
    pf_btn_stats s;
    pf_btn_get_stats(&s);
    if (s.noted && !g_btn_noted_logged) {
        g_btn_noted_logged = 1;
        fu_log_event("fps: first command button clock drawn by the game's own draw (%u control "
                     "bar request(s) so far)",
                     s.requests);
    }
    if (s.asked && !g_btn_asked_logged) {
        g_btn_asked_logged = 1;
        fu_log_event("fps: first command button clock drawn in an extra frame, its request set "
                     "back for the next draw");
    }
    if (s.unseen && !g_btn_unseen_logged) {
        g_btn_unseen_logged = 1;
        fu_log_event("fps: the control bar asked for a button clock in a game frame with extra "
                     "frames, but an extra frame drew no clock on that button: it wasn't drawn, "
                     "or not through the hooked draw (%u request(s), %u clock(s) in extra frames "
                     "so far)",
                     s.requests, s.asked);
    }
}

/* Totals restart at pf_btn_init. */
static unsigned since(unsigned now, unsigned then)
{
    return now >= then ? now - then : now;
}

void pf_fps_take_row(pf_fps_row *row)
{
    pf_btn_stats b;
    pf_btn_get_stats(&b);
    g_row.clock_requests = since(b.requests, g_btn_row.requests);
    g_row.clocks_noted = since(b.noted, g_btn_row.noted);
    g_row.clock_unseen = since(b.unseen, g_btn_row.unseen);
    g_btn_row = b;
    g_row.learned_us = g_learned_us;
    g_row.cost_us = expected_cost();
    g_row.ref_us = g_ref_n >= PF_FPS_REF_DRAWS ? g_ref_us : 0;
    g_row.idle_us = to_us(g_idle_ticks);
    g_idle_ticks = 0;
    pf_ep_take(&g_extra_profile, &g_row.profile);
    *row = g_row;
    {
        unsigned i;
        for (i = 0; i < 4; i++) g_row.blocked[i] = 0;
        for (i = 0; i < 3; i++) g_row.probes[i] = 0;
    }
    g_row.idle_us = g_row.extra_us = g_row.outside_us = g_row.wait_us = g_row.budget_us = 0;
    g_row.waits = g_row.extra = g_row.late = g_row.units = g_row.sleeps = 0;
    g_row.extra_max_us = g_row.late_ms = g_row.held = 0;
    g_row.shadows = g_row.reflections = g_row.clocks = 0;
    g_row.gap_us = g_row.disp_err_us = 0;
    g_row.gaps = g_row.gap_max_us = g_row.rate_changes = g_row.unit_steps = g_row.unit_uneven = 0;
}

/* The game loop around the frame hook and the limiter's wait. */
static LONGLONG g_update_exit, g_first_wait;
static int g_after_update, g_waited;

void pf_fps_frame_edge(int entering)
{
    LONGLONG t;
    if (!g_on)
        return;
    if (entering) {
        g_pair_sequence++;
        if (g_after_update && !g_waited) {
            pf_pair_reset(&g_pair);
            g_pair_probe_frames = 0;
        }
    }
    t = qpc();
    if (!entering) {
        g_update_exit = t;
        g_after_update = 1;
        g_waited = 0;
        return;
    }
    if (!g_after_update)
        return;
    g_after_update = 0;
    if (g_waited) {
        g_row.outside_us += to_us(g_first_wait - g_update_exit);
        g_row.wait_us += to_us(t - g_first_wait);
    } else {
        g_probe_ready = g_probe_cost = 0;
        g_row.outside_us += to_us(t - g_update_exit);
    }
}

/* `mov eax, [ecx+imm32]; ret` or `mov eax, [ecx+imm8]; ret`. */
static int getter_offset(const unsigned char *f)
{
    if (f[0] == 0x8B && f[1] == 0x81 && f[6] == 0xC3)
        return (int)((unsigned)f[2] | (unsigned)f[3] << 8 | (unsigned)f[4] << 16 |
                     (unsigned)f[5] << 24);
    if (f[0] == 0x8B && f[1] == 0x41 && f[3] == 0xC3)
        return f[2];
    return -1;
}

static int client_frame(unsigned *out)
{
    const unsigned char *client = (const unsigned char *)*S.client;
    if (!client)
        return 0;
    if (g_client_off == -2)
        return 0;
    if (g_client_off < 0) {
        void *const *vt = *(void *const *const *)client;
        g_client_off = getter_offset((const unsigned char *)vt[0x7C / 4]);
        if (g_client_off < 0 || g_client_off > 0x10000) {
            g_client_off = -2;
            fu_log_event("fps: the client frame getter isn't a plain field read; that check is off");
            return 0;
        }
    }
    *out = *(const unsigned *)(client + g_client_off);
    return 1;
}

typedef struct {
    unsigned sync_acc, sync_time, prev_sync_time, last_draw_ms, skip_count, init_flags,
        last_client_frame;
} draw_state;

static void save(draw_state *d)
{
    d->sync_acc = *S.sync_acc;
    d->sync_time = *S.sync_time;
    d->prev_sync_time = *S.prev_sync_time;
    d->last_draw_ms = *S.last_draw_ms;
    d->skip_count = *S.skip_count;
    d->init_flags = *S.init_flags;
    d->last_client_frame = *S.last_client_frame;
}

static void restore(const draw_state *d)
{
    *S.sync_acc = d->sync_acc;
    *S.sync_time = d->sync_time;
    *S.prev_sync_time = d->prev_sync_time;
    *S.last_draw_ms = d->last_draw_ms;
    *S.skip_count = d->skip_count;
    *S.init_flags = d->init_flags;
    *S.last_client_frame = d->last_client_frame;
}

static int same_bytes(const void *a, const void *b, unsigned n)
{
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    while (n--)
        if (*x++ != *y++)
            return 0;
    return 1;
}

static void copy_bytes(void *to, const void *from, unsigned n)
{
    unsigned char *t = (unsigned char *)to;
    const unsigned char *f = (const unsigned char *)from;
    while (n--)
        *t++ = *f++;
}

static unsigned char *view_camera(void)
{
    const unsigned char *view = (const unsigned char *)*S.tactical_view;
    return view ? *(unsigned char *const *)(view + S.camera_off) : 0;
}

static void set_transform(unsigned char *robj, const float *m)
{
    void *const *vt = *(void *const *const *)robj;
    ((set_transform_fn)vt[S.set_transform / 4])(robj, m);
}

/* The current game frame's number: it changes with the limiter's frame start,
 * which the limiter sets for every frame, also one that overran and never
 * waited. 0 until the limiter has been seen. */
static unsigned frame_seq(void)
{
    if (!g_frame_start_ms)
        return 0;
    if (!g_seq_known || *g_frame_start_ms != g_seq_start) {
        g_seq_start = *g_frame_start_ms;
        g_seq_known = 1;
        g_seq++;
    }
    return g_seq;
}

static unsigned unit_slot(const void *obj)
{
    return ((unsigned)(UINT_PTR)obj >> 4) * 2654435761u >> 20 & (PF_UNITS - 1);
}

static unit_rec *unit_find(const void *obj, int for_insert)
{
    unsigned h = unit_slot(obj), i;
    unit_rec *spare = 0;
    for (i = 0; i < PF_UNIT_PROBES; i++) {
        unit_rec *r = &g_units[(h + i) & (PF_UNITS - 1)];
        if (r->obj == obj)
            return r;
        if (!spare && (!r->obj || r->obj == PF_UNIT_GONE || r->cur_seq + 2 <= g_seq))
            spare = r;
        if (!r->obj)
            break;
    }
    return for_insert ? spare : 0;
}

/* HLodClass::Set_Transform(m) on a game frame: slot[-4] ECX = the HLod,
 * slot[1] = &m. Recorded as the current game frame's. Not while the
 * draw shows units one frame behind: those (and the sub-objects the pose
 * update places from them) aren't the game's transforms. */
static void unit_record(void **slot)
{
    void *obj = slot[-4];
    const float *m = (const float *)slot[1];
    unsigned seq;
    unit_rec *r;

    if (!g_units_on || g_stopped || g_lagged || !g_main || GetCurrentThreadId() != g_main ||
        !obj || !m || !(seq = frame_seq()))
        return;
    if (!(r = unit_find(obj, 1)))
        return;
    if (r->obj != obj) {
        r->obj = obj;
        r->pprev_seq = r->prev_seq = r->cur_seq = 0;
        r->moved = 0;
    }
    if (r->cur_seq != seq) {
        copy_bytes(r->pprev, r->prev, sizeof r->prev);
        r->pprev_seq = r->prev_seq;
        copy_bytes(r->prev, r->cur, sizeof r->cur);
        r->prev_seq = r->cur_seq;
        r->cur_seq = seq;
    }
    copy_bytes(r->cur, m, sizeof r->cur);
}

/* ~HLodClass: the object is never touched again. */
static void unit_forget(void **slot)
{
    unit_rec *r = unit_find(slot[-4], 0);
    if (r) {
        r->obj = PF_UNIT_GONE;
        r->moved = 0;
    }
}

/* A unit set in the game frames `seq - 1` and `seq`, moving by less than a
 * teleport, and still where the game put it. */
static int unit_moving(const unit_rec *r, unsigned seq)
{
    const unsigned char *obj = (const unsigned char *)r->obj;
    return obj && r->obj != PF_UNIT_GONE && r->cur_seq == seq && r->prev_seq + 1 == seq &&
           !same_bytes(r->prev, r->cur, sizeof r->cur) &&
           same_bytes(obj + PF_RENDOBJ_TRANSFORM, r->cur, sizeof r->cur);
}

/* Where an extra frame f (q16) of the way to the game's next frame shows a
 * unit, as a step offset from this game frame's transform (q16, may be
 * negative): from where this frame's own draw showed it (g_lag_shown behind)
 * to where the next one will (g_lag behind the next step). With the delay
 * steady that is f - delay: ahead with FrameSmoothing=2 (no delay), between
 * the last two game frames with 3. */
static int unit_offset(unsigned f)
{
    long long span = (long long)PF_LAG_FULL + g_lag_shown - g_lag;
    if (f > PF_LAG_FULL)
        f = PF_LAG_FULL;
    return (int)(-(long long)g_lag_shown + span * f / PF_LAG_FULL);
}

/* Moves every unit that moved between the last two game frames for an extra
 * frame f (q16: 65536 is the game's next frame) of the way there.
 * Returns how many. */
static unsigned units_ahead(unsigned f)
{
    unsigned i, n = 0;
    float t[12];
    int at;

    if (f == PF_FPS_NONE || g_smoothing < 2 || !g_units_on)
        return 0;
    at = unit_offset(f);
    for (i = 0; i < PF_UNITS; i++) {
        unit_rec *r = &g_units[i];
        if (!unit_moving(r, g_seq))
            continue;
        if (!pf_mat_offset(r->prev, r->cur, at, PF_UNIT_MAX_STEP, t))
            continue;
        set_transform((unsigned char *)r->obj, t);
        r->moved = 1;
        n++;
    }
    return n;
}

/* At the first wait of a game frame, for the CSV: of the units that moved in
 * each of the last two game frames, how many changed their step by over a
 * quarter (uneven steps are what moving them ahead would amplify). */
static void units_steps(void)
{
    unsigned i;
    if (!g_units_on || g_stopped)
        return;
    for (i = 0; i < PF_UNITS; i++) {
        const unit_rec *r = &g_units[i];
        int k;
        if (!r->obj || r->obj == PF_UNIT_GONE || r->cur_seq != g_seq ||
            r->prev_seq + 1 != r->cur_seq || r->pprev_seq + 1 != r->prev_seq)
            continue;
        k = pf_step_uneven(r->pprev, r->prev, r->cur);
        if (k < 0)
            continue;
        g_row.unit_steps++;
        g_row.unit_uneven += (unsigned)k;
    }
}

/* FrameSmoothing=3, in the game's draw after the view update: every unit
 * that moved this game frame is shown g_lag of a step behind (where it was in
 * the last one, with the full delay). Nothing with no delay. */
static void units_lag(void)
{
    unsigned i, seq = frame_seq();
    float t[12];
    if (!g_lag || !seq)
        return;
    g_lagged = 1;
    for (i = 0; i < PF_UNITS; i++) {
        unit_rec *r = &g_units[i];
        if (!unit_moving(r, seq) || !pf_mat_step_ok(r->prev, r->cur, PF_UNIT_MAX_STEP))
            continue;
        if (g_lag >= PF_LAG_FULL)
            set_transform((unsigned char *)r->obj, r->prev);
        else if (pf_mat_offset(r->prev, r->cur, -(int)g_lag, PF_UNIT_MAX_STEP, t))
            set_transform((unsigned char *)r->obj, t);
        else
            continue;
        r->moved = 1;
    }
}

static unsigned lag_toward(unsigned lag, unsigned want)
{
    if (lag < want)
        return lag + PF_LAG_STEP > want ? want : lag + PF_LAG_STEP;
    if (lag > want)
        return lag < want + PF_LAG_STEP ? want : lag - PF_LAG_STEP;
    return lag;
}

/* At the first wait of a game frame, after the last one's extra frames: the
 * delay this frame's draw used, and the next frame's, a quarter nearer full
 * while extra frames are being drawn and nearer none once they have stopped
 * for PF_LAG_IDLE_FRAMES game frames. */
static void lag_frame_end(unsigned drawn_last_frame)
{
    unsigned want;
    g_lag_shown = g_lag;
    if (!g_lag_on) {
        g_lag = g_lag_shown = 0;
        return;
    }
    if (drawn_last_frame && !g_paused && !g_stopped)
        g_frames_without_extra = 0;
    else if (g_frames_without_extra < PF_LAG_IDLE_FRAMES)
        g_frames_without_extra++;
    want = g_frames_without_extra < PF_LAG_IDLE_FRAMES && !g_stopped ? PF_LAG_FULL : 0;
    g_lag = lag_toward(g_lag, want);
}

/* Puts every moved unit back exactly. 0 if one didn't take it. */
static int units_back(void)
{
    unsigned i;
    int ok = 1;
    for (i = 0; i < PF_UNITS; i++) {
        unit_rec *r = &g_units[i];
        if (!r->moved)
            continue;
        r->moved = 0;
        if (!r->obj || r->obj == PF_UNIT_GONE)
            continue;
        set_transform((unsigned char *)r->obj, r->cur);
        if (!same_bytes((unsigned char *)r->obj + PF_RENDOBJ_TRANSFORM, r->cur, sizeof r->cur))
            ok = 0;
    }
    return ok;
}

static int crc_now(unsigned *out)
{
    void *logic = *S.logic;
    if (!*S.verify_crc || !logic || !((bool_fn)S.is_in_game)(logic))
        return 0;
    *out = ((crc_fn)S.get_crc)(logic, 0);
    return 1;
}

/* One extra frame, with units f (q16) of a game frame's motion on from the
 * game's last frame and the camera where the game has it. 0 if a check
 * failed (extra frames are then off). */
static int extra_frame(const unsigned *engine, unsigned f, unsigned *cost_us)
{
    void *display = *S.display;
    unsigned before[PF_ENGINE_WORDS], seed = *S.logic_seed, frame0 = 0, frame1 = 0, crc0 = 0,
                                      crc1 = 0;
    int have_frame, have_crc;
    draw_state d;
    LONGLONG t0;
    unsigned i;
    unsigned char *cam = view_camera();
    unsigned units;
    float game_cam[12];

    if (!display)
        return 1;
    if ((*(void *const *const *)display)[12] != S.draw) {
        stop("the display's draw isn't the function that was checked");
        return 0;
    }
    if (*S.particles &&
        (*(void *const *const *)*S.particles)[S.particles_slot / 4] != g_particles_update) {
        stop("the particle manager's update isn't the hooked function (effects would run too "
             "fast)");
        return 0;
    }
    for (i = 0; i < PF_ENGINE_WORDS; i++)
        before[i] = engine[PF_ENGINE_FROM / 4 + i];
    have_frame = client_frame(&frame0);
    have_crc = crc_now(&crc0);
    save(&d);
    t0 = qpc();
    /* Set first: the units moved here aren't recorded as game transforms. */
    if (cam)
        copy_bytes(game_cam, cam + PF_RENDOBJ_TRANSFORM, sizeof game_cam);
    g_scene_held = 0;
    pf_ep_begin(&g_extra_profile, qpc_us());
    set_extra(1);
    units = units_ahead(f);
    ((draw_fn)S.draw)(display);
    g_row.clocks += pf_btn_extra_end(frame_seq());
    btn_report();
    if (units && !units_back()) {
        set_extra(0);
        stop("a unit couldn't be put back");
        pf_ep_end(&g_extra_profile, qpc_us(), 0);
        return 0;
    }
    /* The view update is held, so nothing in the draw may move the camera. */
    if (cam && !same_bytes(cam + PF_RENDOBJ_TRANSFORM, game_cam, sizeof game_cam)) {
        set_transform(cam, game_cam);
        set_extra(0);
        stop("an extra frame's draw moved the camera");
        pf_ep_end(&g_extra_profile, qpc_us(), 0);
        return 0;
    }
    set_extra(0);
    g_row.units += units;
    *cost_us = to_us(qpc() - t0);
    restore(&d);
    if (*S.logic_seed != seed) {
        stop("an extra frame drew a logic random number");
        pf_ep_end(&g_extra_profile, qpc_us(), 0);
        return 0;
    }
    for (i = 0; i < PF_ENGINE_WORDS; i++) {
        if (engine[PF_ENGINE_FROM / 4 + i] != before[i]) {
            stop("an extra frame changed the game engine's frame counters");
            pf_ep_end(&g_extra_profile, qpc_us(), 0);
            return 0;
        }
    }
    if (have_frame && (!client_frame(&frame1) || frame1 != frame0)) {
        stop("an extra frame changed the client frame");
        pf_ep_end(&g_extra_profile, qpc_us(), 0);
        return 0;
    }
    if (have_crc && (!crc_now(&crc1) || crc1 != crc0)) {
        stop("an extra frame changed the logic CRC (-verifyClientCRC)");
        pf_ep_end(&g_extra_profile, qpc_us(), 0);
        return 0;
    }
    pf_ep_end(&g_extra_profile, qpc_us(), 1);
    return 1;
}

/* Counted lateness in the last 60 s, after adding `late_ms` at `ms`. */
static unsigned minute_late_ms(unsigned ms, unsigned late_ms)
{
    unsigned sec = ms / 1000, i = sec % 60, sum = 0;
    if (g_minute[i].sec != sec) {
        g_minute[i].sec = sec;
        g_minute[i].late_ms = 0;
    }
    g_minute[i].late_ms += late_ms;
    for (i = 0; i < 60; i++)
        if (sec - g_minute[i].sec < 60)
            sum += g_minute[i].late_ms;
    return sum;
}

/* Lates persisted: extra frames rest 1, 2, then 4 minutes, with the chain
 * of repeats and the minute's lateness starting over; the 4th time they
 * are off for the session. */
static void rest(const char *why)
{
    unsigned ms, i;
    if (++g_rests >= PF_FPS_RESTS_OFF) {
        fu_log_event("fps: %s, the %uth time", why, g_rests);
        stop("the game's frames were made late again after resting 1, 2 and 4 minutes");
        return;
    }
    ms = PF_FPS_REST_MS << (g_rests - 1);
    g_paused = 1;
    g_pause_until = g_frame_ms + ms;
    g_rest_why = why;
    g_repeats = g_counted_repeats = g_any_late = 0;
    g_backoff_ms = 0;
    for (i = 0; i < 60; i++)
        g_minute[i].sec = g_minute[i].late_ms = 0;
    fu_log_event("fps: extra frames rest for %u s: %s (rest %u of %u; a %uth time turns them off "
                 "for the session)",
                 ms / 1000, why, g_rests, PF_FPS_RESTS_OFF - 1, PF_FPS_RESTS_OFF);
}

/* An extra frame made the game's frame `late_ms` late: pause, and rest
 * if lates persist after backing off to the longest pause, or add up to
 * over 1% of the last minute. A late of a few ms (the cost varied a little
 * more than the margin) pauses 1 s and doesn't escalate. Neither does one
 * after an extra frame whose expected cost was a guess (`guess`: none
 * measured yet, or the reference standing in for forgotten samples): its
 * cost is a sample now, and the reference doesn't go below it again until
 * extra frames that fit say otherwise. It still counts toward the minute. */
static void late_frame(unsigned cost, unsigned expected, unsigned late_ms, int guess)
{
    unsigned now = g_frame_ms, minute = 0, pause_ms;
    int counted = (int)(now - g_grace_until) >= 0, minor = late_ms <= PF_FPS_MINOR_MS;
    const char *why = 0;

    g_row.late++;
    g_row.late_ms += late_ms;
    if (guess && cost > g_learned_us)
        g_learned_us = cost;
    if (minor) {
        g_backoff_ms = PF_FPS_BACKOFF_MS;
        g_repeats = g_counted_repeats = 0;
    } else if (guess) {
        /* The chain of repeats stays as it was. */
    } else if (g_any_late && g_repeats && now - g_last_late < PF_FPS_REPEAT_MS) {
        g_backoff_ms = g_backoff_ms * 2 > PF_FPS_BACKOFF_MAX_MS ? PF_FPS_BACKOFF_MAX_MS
                                                                : g_backoff_ms * 2;
        g_repeats++;
        g_counted_repeats = counted ? g_counted_repeats + 1 : 0;
    } else {
        g_backoff_ms = PF_FPS_BACKOFF_MS;
        g_repeats = 1;
        g_counted_repeats = counted;
    }
    pause_ms = guess && !minor ? PF_FPS_BACKOFF_MS : g_backoff_ms;
    /* Late right after pauses of 1, 2, 4 and 8 s. */
    if (g_counted_repeats >= PF_FPS_REPEATS_OFF)
        why = "extra frames kept making the game's frames late after backing off to 8 s";
    if (!guess || minor) {
        g_any_late = 1;
        g_last_late = now;
    }
    g_paused = 1;
    g_pause_until = now + pause_ms;
    if (counted) {
        minute = minute_late_ms(now, late_ms);
        if (!why && (unsigned long long)minute * 1000 > 60000ull * PF_FPS_MINUTE_PERMILLE)
            why = "extra frames made the game's frames late by over 1% of the last minute";
    }
    if (!g_logged || now - g_log_at >= PF_FPS_LOG_MS || why) {
        fu_log_event("fps: a game frame was %u ms late after an extra frame of %u us (expected "
                     "%u us%s); extra frames paused for %u ms%s (%u ms late in the last minute "
                     "counted)%s",
                     late_ms, cost, expected,
                     guess ? ", not measured yet: not a repeat" : "", pause_ms,
                     counted ? "" : ", not counted (grace period)", minute,
                     g_log_skipped ? ", more since the last line" : "");
        g_logged = 1;
        g_log_at = now;
        g_log_skipped = 0;
    } else {
        g_log_skipped++;
    }
    if (why)
        rest(why);
}

/* Spins until `us` after the game's frame was shown, on the fine clock. 0 if
 * the game's deadline came first, or the clock didn't get there within that
 * time plus 2 ms of real time. */
static int pace(unsigned us, unsigned period_ms)
{
    unsigned el = g_fine() - g_shown_us;
    LONGLONG give_up;
    if (el >= us)
        return 1;
    give_up = qpc() + (LONGLONG)(us - el + 2000) * g_freq / 1000000;
    while (g_fine() - g_shown_us < us) {
        if (qpc() > give_up || g_time() - g_frame_ms >= period_ms)
            return 0;
        __asm__ volatile("pause");
    }
    return 1;
}

/* At the first wait of a game frame (right after it was shown): how many
 * extra frames, and when, evenly between it and the next. */
static void plan_frame(unsigned period_ms, unsigned elapsed_ms)
{
    pf_fps_sched s;
    unsigned start[4], fit;

    g_probe_cost = 0;
    g_plan_n = 0;
    g_period_us = period_ms * 1000;
    if (elapsed_ms >= period_ms) {
        g_probe_ready = 0;
        return;
    }
    g_budget_us = (period_ms - elapsed_ms) * 1000;
    s.period = g_period_us;
    s.budget = g_budget_us;
    s.extras = g_extras;
    s.cost = expected_cost();
    s.avg = g_cost_avg_us;
    s.margin = margin_for(s.cost);
    fit = pf_fps_schedule(&s, start);
    if (!fit && !(g_paused && (int)(g_frame_ms - g_pause_until) < 0)) {
        /* Would one of the reference cost have fit? Then only the estimate
         * kept them out this frame. */
        pf_fps_sched r = s;
        r.cost = reference_cost();
        r.margin = margin_for(r.cost);
        if (r.avg > r.cost)
            r.avg = r.cost;
        g_blocked = r.cost < s.cost && pf_fps_schedule(&r, start) > 0;
    }
    if (fit < g_cur_n) {
        g_cur_n = fit;
        g_up = 0;
    } else if (fit > g_cur_n) {
        if (++g_up >= PF_FPS_RISE_FRAMES) {
            g_cur_n = fit;
            g_up = 0;
        }
    } else {
        g_up = 0;
    }
    s.extras = g_cur_n;
    g_plan_n = pf_fps_schedule(&s, g_plan_start);
    if (!fit) blocked(0);
    /* This is a separate one-draw plan: do not decay the learned floor
     * without a successful measurement. Never probe while held or paused. */
    if (fit || g_ref_n < PF_FPS_REF_DRAWS || !g_learned_us ||
        (g_hold && g_hold()) || *S.screenshots || !*S.hwnd ||
        ((iconic_fn)S.is_iconic)(*S.hwnd) ||
        (g_paused && (int)(g_frame_ms - g_pause_until) < 0)) {
        g_probe_ready = 0;
        return;
    }
    s.extras = 1;
    s.cost = g_ref_us > 0x7fffffffu ? ~0u : 2 * g_ref_us;
    if (s.cost < PF_FPS_MIN_COST_US) s.cost = PF_FPS_MIN_COST_US;
    /* Avoid overflow in scheduler cost + margin calculations. */
    if (s.cost >= s.budget) {
        g_probe_ready = 0;
        return;
    }
    s.avg = s.cost;
    s.margin = margin_for(s.cost);
    if (!pf_fps_schedule(&s, start)) {
        g_probe_ready = 0;
        return;
    }
    if (++g_probe_ready >= PF_FPS_PROBE_FRAMES) {
        g_probe_ready = 0;
        g_probe_cost = s.cost;
        g_plan_n = 1;
        g_plan_start[0] = start[0];
    }
}

/* At the limiter's Sleep call. slot[0] is the return address (the site + 5),
 * slot[-9] ESI = the frame time in ms, slot[-8] EBP: [EBP-0x20] = GameEngine. */
int pf_fps_set_pair_pacing(const pf_cu_sites *sites, int frame_hook)
{
    g_pair_enabled = 0;
    pf_pair_reset(&g_pair);
    if (!g_on || g_stopped || !frame_hook || !sites ||
        !g_redraw_shadows || !g_units_on || !g_lag_on ||
        !sites->frame_start || !sites->limit_on || !sites->client || !sites->logic ||
        !sites->is_in_game || !sites->screenshots ||
        (const void *)sites->client != (const void *)S.client ||
        (const void *)sites->logic != (const void *)S.logic)
        return 0;
    g_pair_sites = *sites;
    g_pair_enabled = 1;
    return 1;
}

void pf_fps_set_pair_limit(unsigned limit)
{
    g_pair_limit = limit >= 1 && limit <= 3 ? limit : 1;
    if (g_pair_limit > g_extras) g_pair_limit = g_extras;
}

int pf_fps_pair_owner(void)
{
    return g_pair_enabled && g_on && !g_stopped && g_pair.active;
}

static int pair_allowed(void)
{
    const pf_cu_sites *s = &g_pair_sites;
    unsigned char *logic, *client;
    void *hwnd;
    if (!g_pair_enabled || g_stopped || g_draw_skipped || !*S.display ||
        (const void *)s->frame_start != (const void *)g_frame_start_ms)
        return 0;
    logic = *s->logic;
    client = *s->client;
    hwnd = *S.hwnd;
    if (!*s->limit_on || !logic || !client || logic[s->logic_paused] ||
        !client[s->client_running] || *s->screenshots > 0 || !hwnd ||
        ((iconic_fn)S.is_iconic)(hwnd) || !((bool_fn)s->is_in_game)(logic))
        return 0;
    if (pf_ui_placing(&s->ui) || pf_ui_scrolling(&s->ui))
        return 0;
    return !g_hold || !g_hold();
}

static void pair_wait(void **slot, const unsigned *engine, unsigned period_ms)
{
    unsigned now_ms = g_time(), now, delay, expected, avg, before, end, cost = 0;
    unsigned disp, f;
    int debt, guess, planned, probe = 0;
    if (!pair_allowed()) {
        pf_pair_reset(&g_pair);
        g_pair_probe_frames = 0;
        g_prev_wait = qpc();
        return;
    }
    now = now_ms * 1000u;
    if (g_paused && (int)(g_frame_ms - g_pause_until) >= 0) {
        g_paused = 0;
        g_rest_why = 0;
    }
    if (!g_pair.used && !g_paused && !g_pair_attempted) {
        g_pair_attempted = 1;
        pf_pair_limit(&g_pair, now, expected_cost(),
                      g_pair_good_samples >= 8 ? g_pair_limit : 1);
        while (g_pair.used < g_pair.quota && !g_paused && !g_stopped) {
            now = g_time() * 1000u;
            probe = 0;
            expected = expected_cost();
            guess = cost_is_guess();
            avg = g_cost_avg_us ? g_cost_avg_us : expected;
            if (avg > expected) avg = expected;
            planned = pf_pair_plan(&g_pair, now, expected, avg, &delay);
            if (!planned && !g_pair.used) {
                /* A regular draw includes work the checked extra path holds.
                 * Prefer its own established successful cost. Requiring the
                 * larger ordinary draw to fit can permanently forbid recovery.
                 * A probe is bounded by frequency and failure count below. */
                unsigned candidate = g_pair_good_samples >= 8 ? g_pair_good_cost :
                                     g_ref_n >= PF_FPS_REF_DRAWS ? g_ref_us : 0;
                if (candidate && pf_pair_plan(&g_pair, now, candidate, candidate, &delay)) {
                    if (++g_pair_probe_frames >= 60) {
                        planned = probe = 1;
                        expected = avg = candidate;
                        guess = 0;
                    }
                }
            }
            /* An admitted ordinary extra must not erase recovery credit from
             * earlier eligible cost-blocked frames. Otherwise intermittent
             * headroom can keep a stale maximum alive while preventing all
             * recovery probes. A frame without candidate headroom neither
             * adds nor removes credit. Ineligible UI/game state, attempts
             * and pauses still reset it; the 60-observation limit is unchanged. */
            if (planned) {
                unsigned start = g_fine();
                LONGLONG wait_start = qpc();
                /* The fine clock only paces starts. Absolute cadence always uses
                 * the same native clock that the surrounding limiter reads. */
                while (g_fine() - start < delay)
                    Sleep(0);
                g_idle_ticks += qpc() - wait_start;
                if (!pair_allowed()) {
                    pf_pair_reset(&g_pair);
                    g_prev_wait = qpc();
                    return;
                }
                now = g_time() * 1000u;
                if (pf_pair_claim(&g_pair, now, expected)) {
                    if (probe) {
                        g_pair_probe_frames = 0;
                        g_row.probes[0]++;
                    }
                    before = g_fine();
                    disp = before - g_shown_us + avg;
                    f = g_shown_us - g_prev_shown_us > 200000u ? PF_FPS_NONE :
                        (unsigned)((unsigned long long)disp * 65536u / g_period_us);
                    if (!extra_frame(engine, f, &cost)) {
                        pf_pair_reset(&g_pair);
                        return;
                    }
                    end = g_fine();
                    cost = end - before; /* include the checked draw's restoration */
                    shown(end);
                    g_drawn++;
                    g_done++;
                    g_pair_draws++;
                    g_row.extra++;
                    g_row.extra_us += cost;
                    if (cost > g_row.extra_max_us) g_row.extra_max_us = cost;
                    cost_measured(cost);
                    end -= g_shown_us;
                    g_row.disp_err_us += end > disp ? end - disp : disp - end;
                    now = g_time() * 1000u;
                    debt = (int)(now - g_pair.deadline);
                    if (debt > 0 && (unsigned)debt > g_pair_peak_debt)
                        g_pair_peak_debt = (unsigned)debt;
                    if (debt > (int)PF_PAIR_BORROW_US) {
                        g_pair_misses++;
                        if (probe) g_row.probes[2]++;
                        late_frame(cost, expected, ((unsigned)debt + 999u) / 1000u,
                                   guess);
                        if (probe && ++g_pair_probe_failures >= 3 && !g_stopped) {
                            g_pair_probe_failures = 0;
                            if (!g_rest_why) rest("three paired recovery probes exceeded their debt allowance");
                        }
                    } else {
                        g_pair_good_cost = cost ? cost : 1;
                        if (g_pair_good_samples < 8) g_pair_good_samples++;
                        g_pair_probe_failures = 0;
                        if (probe) {
                            unsigned i;
                            g_row.probes[1]++;
                            g_learned_us = g_cost_us = g_cost_avg_us = g_pair_good_cost;
                            g_cost_guess = 0;
                            for (i = 0; i < PF_FPS_COST_FRAMES; i++) {
                                g_cost_ring[i] = g_pair_good_cost;
                                g_cost_given[i] = 0;
                            }
                            fu_log_event("paired cadence: bounded recovery probe fit in %u us; "
                                         "stale cost history reset", cost);
                        }
                    }
                } else break;
            } else {
                unsigned unused;
                blocked(0);
                /* Age an excessive estimate only when the reference itself
                 * would fit this new policy, never just because time passed. */
                g_blocked = pf_pair_plan(&g_pair, now, reference_cost(), reference_cost(), &unused);
                break;
            }
            /* A recovery probe is a single draw even if its result is cheap. */
            if (probe) break;
        }
    } else if (g_paused) {
        g_pair_probe_frames = 0;
        blocked(2);
    }
    /* The existing verified wait-hook protocol: zero the saved threshold,
     * never the game's frame clock or counters. The next native comparison
     * exits, and the game records its actual clock as the next frame start. */
    if (!g_pair_cut && pf_pair_wait_done(&g_pair, g_time() * 1000u) &&
        g_time() - g_frame_ms < period_ms) {
        slot[-9] = 0;
        g_pair_cut = 1;
        g_pair_cuts++;
    }
    g_prev_wait = qpc();
}

static void wait(void **slot)
{
    const unsigned char *site = (const unsigned char *)slot[0] - 5;
    const unsigned *engine = *(const unsigned *const *)((const unsigned char *)slot[-8] - 0x20);
    unsigned period_ms = (unsigned)(UINT_PTR)slot[-9], now_ms, elapsed_ms, cost = 0;
    void *hwnd;
    LONGLONG now;

    if (!g_on)
        return;
    now = qpc();
    if (g_after_update && !g_waited) {
        g_waited = 1;
        g_first_wait = now;
    }
    if (!g_time) {
        /* push ebx; call [Sleep] | call [timeGetTime]; mov edi, eax; sub eax, [start] */
        g_time = *(time_fn const *)(UINT_PTR)(*(const unsigned *)(site + 8));
        g_frame_start_ms = (const unsigned *)(UINT_PTR)(*(const unsigned *)(site + 16));
        g_main = GetCurrentThreadId();
    }
    if (GetCurrentThreadId() != g_main || !engine)
        return;
    if (!g_seen_frame || *g_frame_start_ms != g_frame_ms) {
        if (g_seen_frame) {
            cost_frame_end();
            if (g_drawn != g_last_n)
                g_row.rate_changes++;
            g_last_n = g_drawn;
        }
        lag_frame_end(g_seen_frame ? g_drawn : 0);
        g_prev_frame_ms = g_frame_ms;
        g_frame_ms = *g_frame_start_ms;
        if (!g_seen_frame || g_frame_ms - g_prev_frame_ms > PF_FPS_STALL_MS)
            g_grace_until = g_frame_ms + PF_FPS_GRACE_MS;
        g_seen_frame = 1;
        if (g_frame_ms - g_prev_frame_ms > 200)
            g_probe_ready = 0;
        g_block_bits = 0;
        g_done = g_drawn = 0;
        g_plan_n = 0;
        g_pair_cut = g_pair_attempted = 0;
        g_row.waits++;
        g_prev_wait = now;
        /* The game's frame was presented just before this first wait: the
         * extra frames are placed, and their units moved, by time
         * since then, not since the frame started (its update and draw take
         * a few ms that vary). */
        g_prev_shown_us = g_shown_us;
        g_shown_us = g_fine();
        shown(g_shown_us);
        elapsed_ms = g_time() - g_frame_ms;
        if (elapsed_ms < period_ms)
            g_row.budget_us += (unsigned long long)(period_ms - elapsed_ms) * 1000;
        if (!g_stopped) {
            btn_report();
            frame_seq();
            units_steps();
            if (g_pair_enabled) {
                int eligible = pair_allowed();
                unsigned stamp = g_time();
                pf_pair_begin(&g_pair, g_pair_sequence, stamp * 1000u,
                              (stamp - g_frame_ms) * 1000u, period_ms * 1000u, eligible);
                if (!g_pair.active) g_pair_probe_frames = 0;
                if (g_pair.active) {
                    g_period_us = period_ms * 1000u;
                    if (g_pair_frames >= 60) {
                        fu_log_event("paired cadence: %u eligible frames, %u extra draws, %u "
                                     "waits shortened, peak debt %u us, %u prediction misses; "
                                     "native period %u ms, expected extra %u us",
                                     g_pair_frames, g_pair_draws, g_pair_cuts,
                                     g_pair_peak_debt, g_pair_misses, period_ms, expected_cost());
                        g_pair_frames = g_pair_draws = g_pair_cuts = 0;
                        g_pair_peak_debt = g_pair_misses = 0;
                    }
                    g_pair_frames++;
                }
            }
            if (!g_pair.active && period_ms >= 10 && period_ms <= 200)
                plan_frame(period_ms, elapsed_ms);
        }
    }
    g_row.sleeps++;
    g_idle_ticks += now - g_prev_wait;
    g_prev_wait = now;
    if (pf_fps_pair_owner()) {
        pair_wait(slot, engine, period_ms);
        return;
    }
    if (period_ms < 10 || period_ms > 200) {
        g_probe_ready = g_probe_cost = 0;
        return;
    }
    /* Behind the schedule (catch-up): the wait's time goes to catching up,
     * and says nothing about what an extra frame costs. */
    if (g_hold && g_hold()) {
        blocked(1);
        g_probe_ready = g_probe_cost = 0;
        g_blocked = 0;
        return;
    }
    /* The extra frames are paced here rather than relying on the game
     * calling again in time. */
    for (;;) {
        unsigned at, now_us, expected, margin, disp, f, end_us;
        int guess;
        if (g_stopped) {
            blocked(3);
            g_probe_ready = g_probe_cost = 0;
            return;
        }
        if (*S.screenshots > 0) {
            g_probe_ready = g_probe_cost = 0;
            g_blocked = 0;
            return;
        }
        if (g_paused) {
            if ((int)(g_frame_ms - g_pause_until) < 0) {
                blocked(2);
                g_probe_ready = g_probe_cost = 0;
                return;
            }
            g_paused = 0;
            if (g_rest_why) {
                g_rest_why = 0;
                fu_log_event("fps: extra frames back after resting (expected cost %u us)",
                             expected_cost());
            }
        }
        hwnd = *S.hwnd;
        if (!hwnd || ((iconic_fn)S.is_iconic)(hwnd)) {
            g_probe_ready = g_probe_cost = 0;
            g_blocked = 0;
            return;
        }
        if (g_done >= g_plan_n)
            return;
        now_ms = g_time();
        elapsed_ms = now_ms - g_frame_ms;
        if (elapsed_ms >= period_ms)
            return;
        expected = g_probe_cost ? g_probe_cost : expected_cost();
        guess = cost_is_guess();
        margin = margin_for(expected);
        now_us = g_fine() - g_shown_us;
        at = g_plan_start[g_done];
        if (at > now_us) {
            LONGLONG t0 = qpc();
            int reached = pace(at, period_ms);
            g_prev_wait = qpc();
            g_idle_ticks += g_prev_wait - t0;
            if (!reached)
                return;
            continue;
        }
        /* Behind the plan (the last one ran long): only if it still fits,
         * on both clocks. */
        if ((unsigned long long)now_us + expected + margin > g_budget_us ||
            (unsigned long long)elapsed_ms * 1000 + expected + margin > period_ms * 1000)
            return;
        /* Where things are when this frame will be on screen: when its draw
         * returns, the average cost from now. */
        disp = now_us + (g_probe_cost ? g_probe_cost : g_cost_avg_us);
        f = g_shown_us - g_prev_shown_us > 200000
                ? PF_FPS_NONE
                : (unsigned)((unsigned long long)disp * 65536u / g_period_us);
        if (g_probe_cost) g_row.probes[0]++;
        if (!extra_frame(engine, f, &cost))
            return;
        end_us = g_fine() - g_shown_us;
        shown(g_shown_us + end_us);
        g_row.disp_err_us += end_us > disp ? end_us - disp : disp - end_us;
        g_done++;
        g_drawn++;
        g_row.extra++;
        g_row.extra_us += cost;
        if (cost > g_row.extra_max_us)
            g_row.extra_max_us = cost;
        cost_measured(cost);
        elapsed_ms = g_time() - g_frame_ms;
        g_probe_ready = 0;
        if (elapsed_ms > period_ms) {
            int probe = g_probe_cost != 0;
            if (probe) {
                g_row.probes[2]++;
                if (cost > g_learned_us) g_learned_us = cost;
                g_probe_cost = 0;
            }
            late_frame(cost, expected, elapsed_ms - period_ms, probe ? 0 : guess);
            if (probe && ++g_probe_failures >= PF_FPS_PROBE_FAILURES && !g_stopped) {
                g_probe_failures = 0;
                if (!g_rest_why) rest("three recovery probes made game frames late");
            }
            g_prev_wait = qpc();
            return;
        }
        if (g_probe_cost) {
            unsigned i;
            g_row.probes[1]++;
            g_probe_cost = g_probe_failures = 0;
            g_learned_us = g_cost_us = g_cost_avg_us = cost ? cost : 1;
            g_cost_guess = 0;
            for (i = 0; i < PF_FPS_COST_FRAMES; i++) {
                g_cost_ring[i] = g_cost_us;
                g_cost_given[i] = 0;
            }
            fu_log_event("fps: recovery probe fit in %u us; stale cost history reset", cost);
        }
        g_probe_failures = 0;
        /* It fit: the reference's floor from an earlier late eases toward it. */
        if (g_learned_us > cost)
            g_learned_us -= (g_learned_us - cost) / 8;
        if (g_verbose && g_row.extra == 1)
            fu_log_event("fps: extra frame drawn in %u us (frame time %u ms)", cost, period_ms);
        g_prev_wait = qpc();
    }
}

void *pf_fps_enter(unsigned k, void **slot, void *next, unsigned arg_bytes)
{
    if (k == PF_HLOD_DTOR) {
        unit_forget(slot);
        return next;
    }
    if (k == PF_HLOD_SET) {
        if (!g_in_extra)
            unit_record(slot);
        return next;
    }
    /* slot[1] is the window. */
    if (k == PF_BUTTON_DRAW) {
        if (g_on && !g_stopped && g_main && GetCurrentThreadId() == g_main)
            pf_btn_draw(slot[1], g_in_extra);
        return next;
    }
    if (k == PF_CLOCK_SET || k == PF_CLOCK_SET_LEFT) {
        if (g_on && !g_stopped && !g_in_extra && g_main && GetCurrentThreadId() == g_main)
            pf_btn_request(slot[1], frame_seq());
        return next;
    }
    if (g_in_extra) {
        /* Only four pass markers are timed. Unknown per-mesh markers cost
         * no clock read and cannot displace a tracked pass on the stack. */
        if (k == PF_EVENT_BEGIN || k == PF_EVENT_END) {
            if (!g_main || GetCurrentThreadId() != g_main)
                return next;
            if (k == PF_EVENT_BEGIN) {
                const char *label = (const char *)slot[1];
                if (pf_ep_label(label) >= 0)
                    pf_ep_marker_begin(&g_extra_profile, slot[-4], label, qpc_us());
            } else if (pf_ep_tracks(&g_extra_profile, slot[-4])) {
                pf_ep_marker_end(&g_extra_profile, slot[-4], qpc_us());
            }
            return next;
        }

        /* The stored shadow map and reflection are reused, and the game's
         * average frame rate counts only its own frames. The view update
         * (camera motion, follow and shake, the terrain under the camera)
         * and the particles step once per call: an extra frame draws them
         * as the game's last frame left them. */
        if (k == PF_VIEWS || k == PF_PARTICLES) {
            void *skip = pf_skip_thunk(arg_bytes);
            if (GetCurrentThreadId() != g_main || !skip)
                return next;
            g_row.held++;
            return skip;
        }
        /* With ExtraFrameShadows / ExtraFrameReflections the pass runs as in
         * a game frame, with the units this extra frame moved;
         * timing.c then keeps the shadow matrix that matches the new map.
         * The extra frame's measured cost includes it, so the pacing sees it. */
        if ((k == PF_SHADOW && g_redraw_shadows) || (k == PF_REFLECT && g_redraw_reflections)) {
            if (GetCurrentThreadId() == g_main) {
                if (k == PF_SHADOW)
                    g_row.shadows++;
                else
                    g_row.reflections++;
                return next;
            }
        }
        if (k == PF_SHADOW || k == PF_REFLECT || k == PF_FPS_AVG) {
            void *skip = pf_skip_thunk(arg_bytes);
            return skip ? skip : next;
        }
        return next;
    }
    /* The game's own draw, after its view update set this frame's
     * transforms: FrameSmoothing=3 shows the units one frame behind. */
    if (k == PF_PARTICLES) {
        if (g_lag_on && g_in_draw && !g_lagged && !g_stopped && g_main &&
            GetCurrentThreadId() == g_main)
            units_lag();
        return next;
    }
    if (k == PF_FPS_WAIT)
        wait(slot);
    return next;
}

void pf_fps_render_skipped(void)
{
    g_draw_skipped = 1;
}

void pf_fps_draw_edge(int entering)
{
    if (!g_on || g_in_extra || !g_main || GetCurrentThreadId() != g_main)
        return;
    if (entering) {
        if (!g_draw_depth++) {
            g_draw_skipped = 0;
            g_draw_t0 = qpc();
        }
        if (g_lag_on)
            g_in_draw++;
        return;
    }
    if (g_draw_depth && !--g_draw_depth && !g_draw_skipped)
        ref_measured(to_us(qpc() - g_draw_t0));
    if (!g_lag_on)
        return;
    if (g_in_draw)
        g_in_draw--;
    if (g_in_draw || !g_lagged)
        return;
    if (!units_back())
        stop("a unit couldn't be put back after the game's draw");
    g_lagged = 0;
}
