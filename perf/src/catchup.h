/* Catch-up: when the game has fallen behind its frame schedule, a game frame
 * runs without its render (shadow map, reflection, scene, UI, present), and
 * the frame limiter's next wait ends at the schedule instead of a whole frame
 * after a late start. Every frame still runs its whole update: logic, the
 * network, input, the client and UI updates and audio, so logic sees the
 * same frames with the same inputs whether a frame is drawn or not. */
#ifndef PF_CATCHUP_H
#define PF_CATCHUP_H

#include "cusites.h"

/* The decision, on the game's millisecond clock (the limiter's) and measured
 * costs in microseconds. No game memory: tests drive it directly. */
typedef struct {
    unsigned max_skips;   /* renders skipped in a row at most */
    int seen;
    unsigned prev_start;  /* the last frame's start */
    unsigned sched;       /* this frame's start on the schedule */
    unsigned period;      /* the limiter's frame time */
    unsigned debt;        /* how late this frame started: start - sched */
    int ok;               /* skipping and catching up are allowed this frame */
    int decided, skip;    /* this frame's render site was reached; what it decided */
    unsigned in_row;      /* renders skipped in a row */
    unsigned render_us;   /* expected cost of the render (drawn frames) */
    unsigned after_us;    /* expected time from the draw's end to the frame's end */
    unsigned samples;     /* renders measured */
} pf_cu_core;

#define PF_CU_DEFAULT_PERIOD 33u
#define PF_CU_STALL_MS 250u      /* a frame starting this long after the last: a stall */
#define PF_CU_MAX_DEBT_FRAMES 3u /* lateness beyond three frames is forgiven */
#define PF_CU_MIN_LATE_US 4000u  /* never skip for less lateness than this */
#define PF_CU_MIN_SAMPLES 8u     /* renders measured before the first skip */
/* The notice shows once NoticeMinSkips renders were skipped within
 * PF_CU_NOTICE_WINDOW_MS; while shown, half that many (rounded up) keep it,
 * and it stays PF_CU_NOTICE_MS after the last skip that did, so it doesn't
 * flash on and off. A single stutter's skip or two never shows it. */
#define PF_CU_NOTICE_MS 2000u
#define PF_CU_NOTICE_WINDOW_MS 2000u
#define PF_CU_NOTICE_MAX_SKIPS 30u /* NoticeMinSkips at most: every other frame for 2 s */

void pf_cu_core_init(pf_cu_core *c, unsigned max_skips);
/* GameEngine::update starts: start_ms is the limiter's start of this frame.
 * ok = 0 (menus, pause, placing a building, extra frames, ...) follows the
 * game's own pacing: the schedule restarts from this frame. */
void pf_cu_core_begin(pf_cu_core *c, unsigned start_ms, int ok);
/* At the render's start, the first time in the frame: 1 = skip it. Skips when
 * drawing would end the frame later than one render's cost behind the
 * schedule (so a skipped frame never waits), at most max_skips in a row. */
int pf_cu_core_decide(pf_cu_core *c, unsigned now_ms);
/* A drawn frame's render took us. */
void pf_cu_core_render(pf_cu_core *c, unsigned us);
/* From the draw's end to the frame's end. */
void pf_cu_core_after(pf_cu_core *c, unsigned us);
/* In the limiter's wait (elapsed < period): 1 = end the wait now, the frame
 * has reached the schedule. Also records the period. */
int pf_cu_core_wait(pf_cu_core *c, unsigned now_ms, unsigned start_ms, unsigned period_ms);
/* 1 while this frame started behind the schedule (no extra frames then). */
int pf_cu_core_behind(const pf_cu_core *c);

/* When the notice is drawn, on the game's millisecond clock: only that, the
 * skipping doesn't look at it. */
typedef struct {
    unsigned min_skips;                   /* 1..PF_CU_NOTICE_MAX_SKIPS */
    unsigned at[PF_CU_NOTICE_MAX_SKIPS];  /* the last skips' times, a ring */
    unsigned head, count;
    int shown;
    unsigned until;                       /* shown until then */
} pf_cu_notice;

/* min_skips 0 counts as 1 (every skip shows it, as before 0.9.9). */
void pf_cu_notice_init(pf_cu_notice *n, unsigned min_skips);
/* A render was skipped at now_ms. */
void pf_cu_notice_skip(pf_cu_notice *n, unsigned now_ms);
/* 1 = draw the notice at now_ms. */
int pf_cu_notice_shown(pf_cu_notice *n, unsigned now_ms);

typedef struct {
    int enabled;        /* [CatchUp] Enabled */
    unsigned max_skips; /* [CatchUp] MaxSkips, 1..3 */
    int notice;         /* [CatchUp] Notice */
    char text[48];      /* [CatchUp] NoticeText, printable ASCII */
    unsigned notice_min; /* [CatchUp] NoticeMinSkips, 1..30 (0 = 1) */
} pf_cu_settings;

/* Per CSV row (whole row). */
typedef struct {
    unsigned frames;            /* game frames that started */
    unsigned skipped;           /* renders skipped */
    unsigned tracked;           /* frames on the schedule (catch-up allowed) */
    unsigned long long behind_ms; /* lateness at frame start, summed over tracked frames */
    unsigned behind_max_ms;
    unsigned waits_cut;         /* limiter waits ended at the schedule */
    unsigned logic;             /* GameLogic::update calls (6 per 5 game frames) */
    unsigned render_us, after_us; /* the estimates when the row was taken */
    unsigned logic_ticks;       /* steps of TheGameLogic's frame counter */
    int ticks_on;               /* that counter was found */
} pf_cu_row;

/* NULL sites: catch-up stays off (why is logged by main.c). */
void pf_cu_init(const pf_cu_settings *s, const pf_cu_sites *sites);
/* Extra frames drawn in the wait of the frame that started at a given time
 * (fps.c): a frame after one that drew extra frames never skips. */
void pf_cu_set_extras(unsigned (*extras_in)(unsigned frame_start_ms));
int pf_cu_on(void);
/* From timing.c, on the main thread: GameEngine::update (1 entry, 0 return),
 * W3DDisplay::draw of a game frame (the same), GameLogic::update's return. */
void pf_cu_frame_edge(int entering);
void pf_cu_draw_edge(int entering);
void pf_cu_logic(void);
/* The catchup_render site: where the render continues, next or the frame's
 * no-render path. */
void *pf_cu_render(void **slot, void *next);
/* The limiter's Sleep call (slot as in fps.c: slot[-9] ESI = the frame time). */
void pf_cu_wait(void **slot);
/* DX8Wrapper::End_Scene(flip) of a game or extra frame: the notice. */
void pf_cu_present(void);
int pf_cu_behind(void);
void pf_cu_take_row(pf_cu_row *r);
/* Tests: the decision's state the hooks use. */
pf_cu_core *pf_cu_core_state(void);

#endif
