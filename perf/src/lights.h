/* The light environment RTS3DScene::renderOneObject builds for every object
 * in every pass (see docs/specs/2026-10-13-perf-dll-design.md, Milestone 9):
 * its timers and counters, and three cuts that leave every value the game
 * hands on unchanged:
 *
 *  - LightSkipShadow: in the shadow-map pass the build is skipped and the
 *    object gets the scene's default environment, as the game gives an
 *    object without lights (nothing that pass draws reads it; but the FX
 *    batches' unlock writes the device's lights from the last group's
 *    environment, which outlives the pass: off by default, stale_draws
 *    counts the draws that could see it);
 *  - LightCull: the scene and dynamic light lists are walked once per pass
 *    (each light's enabled byte, type and bounding sphere), and each object
 *    gets the lights the game's own sphere test keeps, through the game's
 *    Add_Light, in list order, instead of every light's virtual call and
 *    test per object;
 *  - LightReuse: an object whose environment before the lists, bounding
 *    sphere and kept lights (each unchanged since it was snapshotted) match
 *    an earlier build gets that build's result without the Add_Light calls.
 *
 * LightVerify recomputes a sample of objects the game's way and compares the
 * environments byte for byte; a cut that differs twice turns itself off.
 * Main thread only; nothing here is read by logic. */
#ifndef PF_LIGHTS_H
#define PF_LIGHTS_H

#include "ltsites.h"

typedef struct {
    /* Per pass (shadow map, reflection, views), game frames, whole row. */
    unsigned calls[3];                 /* renderOneObject calls */
    unsigned long long us[3];          /* their time */
    unsigned long long pre_us[3];      /* from entry to the object's Render call */
    unsigned long long env_us[3];      /* the light environment's build */
    unsigned scene_tested[3], scene_kept[3]; /* scene light list: tested, added */
    unsigned dyn_tested[3], dyn_kept[3];     /* enabled dynamic lights: tested, added */
    /* Whole row (game frames). */
    unsigned skipped;        /* shadow-pass builds skipped */
    unsigned decided;        /* objects whose light lists the plugin decided */
    unsigned reuse_hits, reuse_misses;
    unsigned fallback;       /* passes left to the game's own loops */
    unsigned verify_samples, verify_bad;
    /* Draws outside the shadow pass that light their vertices with the
     * device's lights (fixed function) while those were last written in a
     * shadow pass whose builds were skipped. */
    unsigned stale_draws;
    /* LightReuse in extra frames (whole row; the columns above are game
     * frames'): the scene's update list is held there, so the lights are
     * the game frame's and every object whose own inputs didn't move hits. */
    unsigned extra_hits, extra_misses;
} pf_lt_counts;

/* The cuts as configured (0/1); verify: 0 off, 1 every 5 s a few objects,
 * 2 every object (tests). shadow_flag: the game's "drawing the shadow map"
 * byte (NULL: LightSkipShadow stays off). */
void pf_lt_init(const pf_lt_data *d, const volatile unsigned char *shadow_flag, int skip,
                int cull, int reuse, int verify);
/* Timers and counters (with [Timing] Enabled=1), and the pass the markers
 * say is open: 0 shadow map, 1 reflection, 2 views, -1 none. */
void pf_lt_set_timing(int on, int (*pass)(void));
/* DX8Wrapper's IDirect3DDevice9 (NULL: unknown), asked by the stale-light
 * count whether a draw uses fixed-function lighting. */
void pf_lt_set_device(void *const *device);
/* Which sites are installed: the cuts need theirs and, for LightCull and
 * LightReuse, the Customized_Render hook that tells a pass's start. */
void pf_lt_set_installed(int cut_sites, int pass_hook);
int pf_lt_skip_on(void);
int pf_lt_cull_on(void);
int pf_lt_reuse_on(void);

/* From the Customized_Render hook, before the pass draws anything. */
void pf_lt_pass_begin(void);
/* Catch-up skipped a game frame's render after walking the scene's update
 * list (dynamic lights moved and faded outside any pass): the pass's light
 * snapshot is dropped, so no object is decided from lights as they were
 * before the walk. */
void pf_lt_render_skipped(void);
/* The light sites (PF_ROO .. PF_LENV_SET); renderOneObject's own entry is
 * wrapped here and returns through pf_lt_roo_exit. */
void *pf_lt_enter(unsigned k, void **slot, void *next);
void *pf_lt_roo_exit(unsigned char *esp);
/* From the draw probe. */
void pf_lt_draw(void);

/* ShadowPassVerify draws the shadow map with LightSkipShadow held off and
 * on; two differing samples switch it off. */
int pf_lt_skip_active(void);
void pf_lt_skip_off(void);

void pf_lt_take(pf_lt_counts *c);
void pf_lt_totals(pf_lt_counts *c);
/* Once a minute: a log line with the totals and the per-pass averages. */
void pf_lt_minute(void);

#endif
