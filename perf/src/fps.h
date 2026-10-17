/* Extra frames: drawn only on this client, in the frame limiter's idle
 * wait, between the game's own frames. They call the display draw and
 * nothing else; logic, its schedule, input, audio and the network never
 * see them. Every extra frame is checked, and any failed check turns them
 * off for the session; game frames they keep making late rest them. */
#ifndef PF_FPS_H
#define PF_FPS_H

#include <windows.h>

#include "fpsites.h"

/* The extra frames of one game frame, all times in microseconds from when
 * the game's frame was shown (its first wait, just after it presented). */
typedef struct {
    unsigned period;   /* the game's frame time: its next frame is shown about this much later */
    unsigned budget;   /* from then to the game's deadline for its next frame */
    unsigned extras;   /* extra frames wanted per game frame (MaxFPS / 30 - 1) */
    unsigned cost;     /* expected (high) cost of one extra frame: what must fit */
    unsigned avg;      /* average cost: an extra frame is shown this long after it starts */
    unsigned margin;   /* time left over before the deadline */
} pf_fps_sched;

#define PF_FPS_NONE 0xFFFFFFFFu

/* How many extra frames, and when each should start (start[0..n-1]), so they
 * are shown evenly spaced between the game's frames: at period * j / (n + 1).
 * The most that fit, back to back too; if none fit evenly, one as late as
 * fits (when that is still at least period / 8 after the game's frame). */
unsigned pf_fps_schedule(const pf_fps_sched *s, unsigned *start);
/* Tests: the microsecond clock frames are timed and paced on (NULL = QPC). */
void pf_fps_set_clock(unsigned (*now_us)(void));

typedef struct {
    unsigned long long idle_us; /* time the game spent waiting, whole row */
    unsigned waits;             /* game frames that waited */
    unsigned extra;             /* extra frames drawn */
    unsigned long long extra_us;
    unsigned late;              /* game frames that started late after an extra frame */
    unsigned units;             /* units moved ahead, summed over the extra frames */
    /* Where each game frame's loop goes, whole row: from GameEngine::update
     * returning to the limiter's first wait (or to the next update if it
     * didn't wait), and from there to the next update. */
    unsigned long long outside_us, wait_us;
    unsigned sleeps;             /* the limiter's Sleep calls */
    unsigned long long budget_us; /* time left before the deadline at each frame's first wait */
    unsigned cost_us;            /* the expected cost of an extra frame, when the row was taken */
    unsigned extra_max_us;       /* the slowest extra frame */
    unsigned late_ms;            /* how late the late game frames were, summed */
    unsigned held;               /* view and particle updates held in extra frames */
    unsigned shadows;            /* shadow maps drawn in extra frames (ExtraFrameShadows) */
    unsigned reflections;        /* reflections drawn in extra frames (ExtraFrameReflections) */
    /* Frames shown (the game's and extra ones): the gaps between them, how
     * far each extra frame's shown time was from the one its positions were
     * computed for, game frames whose extra-frame count changed, and units
     * moving two frames in a row whose step changed by over a quarter. */
    unsigned long long gap_us, disp_err_us;
    unsigned gaps, gap_max_us, rate_changes, unit_steps, unit_uneven;
    unsigned clocks;             /* command button clocks drawn in extra frames */
    unsigned ref_us;             /* the game's own draw, when the row was taken (0: not timed) */
    /* The control bar's clock requests, the clocks the game's own draws
     * drew, and game frames with an extra frame that drew no clock on a
     * button asked for one (see btnclock.h). */
    unsigned clock_requests, clocks_noted, clock_unseen;
    /* Once per limiter frame; reasons can overlap: cost, catch-up, pause, stop. */
    unsigned blocked[4];
    unsigned probes[3]; /* attempted, timely, late */
    unsigned learned_us;
} pf_fps_row;

/* Before installing; max_fps 30 = off. NULL sites = nothing found.
 * smoothing: 0 or 1 = repeat the last frame, 2 = move units ahead, 3 = units
 * between the last two game frames (the game's own draw shows them one frame
 * behind). The camera is never moved. */
void pf_fps_init(const pf_fps_sites *sites, const char *why_not, unsigned max_fps, int smoothing,
                 int verbose);
/* After installing: whether the wait, the average-rate and the view-update
 * hooks are in, and the hooked particle manager update (NULL if not). */
void pf_fps_set_installed(int wait, int avg, int views, void *particles_update);
/* After installing, with FrameSmoothing>=2: whether the HLod hooks are in;
 * with 3, whether the display draw is wrapped. */
void pf_fps_set_units(int installed, int lag_installed);
/* ExtraFrameShadows / ExtraFrameReflections: 1 = extra frames redraw the
 * shadow map / the water reflection instead of reusing the game frame's. */
void pf_fps_set_passes(int shadows, int reflections);
/* From the wrapped display draw, on the game's own frames: 1 at entry, 0 at
 * return. It is timed (what an extra frame not measured yet is expected to
 * cost), and FrameSmoothing=3 puts the units back there. */
void pf_fps_draw_edge(int entering);
/* Catch-up skipped the render of the game's draw now running: not timed. */
void pf_fps_render_skipped(void);
/* Called with 1 when an extra frame starts and 0 when it ends (instancing). */
void pf_fps_set_extra_listener(void (*fn)(int on));
/* The scene_update site: where the update loop goes on to the next object.
 * In an extra frame each object's On_Frame_Update is passed by (W3D particle
 * emission, dynamic light fades step once per call), so it runs once per
 * game frame. Returns where the site continues. */
void pf_fps_set_scene_skip(void *next_object);
void *pf_fps_scene_update(void *next);
/* behind() returning 1 at a wait: no extra frames in it (catch-up). */
void pf_fps_set_hold(int (*behind)(void));
/* Extra frames drawn in the wait of the game frame that started at
 * frame_start_ms (the limiter's start); 0 if that frame didn't wait. */
unsigned pf_fps_extras_in(unsigned frame_start_ms);
/* 1 while an extra frame is being drawn (the other hooks pass through or skip). */
int pf_fps_in_extra(void);
/* From pf_enter for PF_FPS_WAIT, PF_FPS_AVG, the unit, hold and button hooks,
 * and every hook during an extra frame. */
void *pf_fps_enter(unsigned k, void **slot, void *next, unsigned arg_bytes);
void pf_fps_take_row(pf_fps_row *row);
/* From the frame hook: 1 when GameEngine::update starts, 0 when it returns. */
void pf_fps_frame_edge(int entering);
/* Why extra frames stopped, or NULL. */
const char *pf_fps_stopped(void);
/* Why extra frames are resting after lates persisted, or NULL. */
const char *pf_fps_resting(void);

#endif
