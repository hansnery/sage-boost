/* bfme2perf.ini next to the plugin. A missing file means "all defaults". */
#ifndef PF_CONFIG_H
#define PF_CONFIG_H

#include <windows.h>

typedef struct {
    int ini_found;
    int enabled; /* [Perf] Enabled, default 1 */
    int log;     /* [Perf] Log, default 1 */
    int timing;  /* [Timing] Enabled, default 1 */
    int threads; /* [Skeleton] Threads, -1..4, default 0 */
    int verify;  /* [Skeleton] Verify, default 0 */
    int detail;  /* [Timing] Detail, default 1 */
    int reflect_requested; /* [Render] ReflectionInterval as written, default 1 */
    int reflect_interval;  /* the same, clamped to 1..4 */
    int shadow_requested;  /* [Render] ShadowInterval as written, default 1 */
    int shadow_interval;   /* the same, clamped to 1..4 */
    int shadow_scrolling;  /* [Render] ShadowFullWhileScrolling, default 0 */
    int batching;          /* [Render] Batching, default 0 (read and logged only) */
    int queue_insert;      /* [Render] QueueInsert, default 1: verified spare-capacity append */
    int queue_sort;        /* [Render] QueueSort, default 1: verified native-order raw moves */
    int instancing;        /* [Render] Instancing, default 0 */
    int instancing_verify; /* [Render] InstancingVerify, default 1 (only with Instancing=1) */
    int shadow_instancing; /* [Render] ShadowInstancing, default 1: instancing in the shadow-map
                            * pass too (only with Instancing=1) */
    int max_fps_requested; /* [Render] MaxFPS as written, default 30 */
    int max_fps;           /* 30 (off), 60, 90 or 120 */
    int frame_smoothing;   /* [Render] FrameSmoothing 0..3: what extra frames move */
    int extra_shadows;     /* [Render] ExtraFrameShadows, default 0: extra frames redraw the
                            * shadow map instead of reusing the game frame's */
    int extra_reflections; /* [Render] ExtraFrameReflections, default 0: the same for the
                            * water reflection */
    int shadow_batch;      /* [Render] ShadowBatchIgnoreLight, default 1: the shadow pass's
                            * FX batch ignores the light environment and fog colour */
    int stencil_flushes;   /* [Render] NoStencilFlushes 0..2, default 1: 1 = no stencil-only
                            * flushes in the shadow pass, 2 = in the other passes too while
                            * shadow mapping (not stencil volumes) is on */
    int shadow_particles;  /* [Render] ShadowSkipParticles 0..2, default 1: skip the shadow
                            * pass's particle walk (1: when no smudge can be added) */
    int shadow_verify;     /* [Render] ShadowPassVerify, default 0: every 10 s draw the shadow
                            * map with a fix off and on and compare */
    int light_skip;        /* [Render] LightSkipShadow, default 0: no light environment build
                            * in the shadow-map pass */
    int light_cull;        /* [Render] LightCull, default 1: the light lists walked once per
                            * pass, each object's lights from the game's own test */
    int light_reuse;       /* [Render] LightReuse, default 1 (needs LightCull): an object whose
                            * light inputs match an earlier build gets its result */
    int light_verify;      /* [Render] LightVerify 0..2, default 1: every 5 s a few objects'
                            * environments are built both ways and compared */
    int catchup;           /* [CatchUp] Enabled, default 1: skip renders when behind */
    int catchup_max_skips; /* [CatchUp] MaxSkips 1..3, default 1: renders skipped in a row */
    int catchup_notice;    /* [CatchUp] Notice, default 1: the on-screen notice */
    char catchup_text[48]; /* [CatchUp] NoticeText, printable ASCII, default "Catching up" */
    int catchup_notice_min; /* [CatchUp] NoticeMinSkips 1..30, default 6: renders skipped in
                             * 2 s before the notice shows */
} pf_config;

/* FrameSmoothing when the INI doesn't say: units between the last two game
 * frames. */
#define PF_DEFAULT_SMOOTHING 3
/* NoticeMinSkips when the INI doesn't say. */
#define PF_DEFAULT_NOTICE_SKIPS 6

void pf_config_load(pf_config *cfg, const WCHAR *ini_path);

/* Decimal with an optional '-', spaces around it allowed; def otherwise. */
int pf_parse_int(const WCHAR *s, int def);

const char *pf_detect_game(const WCHAR *command_line, const WCHAR *exe_path);

#endif
