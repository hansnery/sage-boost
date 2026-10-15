/* Three cuts in the shadow-map pass, each proven to leave the map as the game
 * draws it (see docs/specs/2026-10-13-perf-dll-design.md, Milestone 7):
 *
 *  - ShadowBatchIgnoreLight: the FX batch's "same batch" test ignores the
 *    light environment and the fog colour while the shadow map is drawn
 *    (no shadow-map technique reads them), so units of one model and
 *    material form one group instead of one per light environment; the
 *    group's setup stores the light environment the game's last group of
 *    that run would have stored (the batches' unlock writes the device's
 *    lights from it, and those outlive the pass);
 *  - NoStencilFlushes: the meshes of SHADOW_VOLUME_NON_SELF_* objects (big
 *    monsters) are no longer drawn between two mesh flushes that only give
 *    stencil shadow volumes a stencil reference: always in the shadow pass,
 *    and with 2 in the other passes too while shadow mapping (not stencil
 *    volumes) is on;
 *  - ShadowSkipParticles: the particle walk the shadow pass's scene flush
 *    starts (every particle draw module returns at once in that pass) is
 *    skipped, and its bookkeeping reproduced, when its smudge branch can't run
 *    (1), or always (2, the heat-haze screen filter may then decide on the
 *    last pass's smudge count).
 *
 * Main thread only; nothing here is read by logic. */
#ifndef PF_SHADOWPASS_H
#define PF_SHADOWPASS_H

#include "spsites.h"

/* PF_SP_LIGHT: LightSkipShadow (lights.c), verified here as a fourth variant. */
enum { PF_SP_BATCH = 1, PF_SP_STENCIL = 2, PF_SP_PARTICLES = 4, PF_SP_ALL = 7, PF_SP_LIGHT = 8 };

/* Game structure offsets (AotR / ROTWK 2.01). */
#define PF_MESH_LIGHT_ENV 0xC8   /* LightEnvironmentClass *, compared by value */
#define PF_MESH_FOG_COLOR 0x314  /* Vector3, the batch test's "colour" */

typedef struct {
    /* Shadow pass, game frames: batch tests, those the game would end a group
     * at, those still ending one (groups saved = split_game - split_now). */
    unsigned batch_tests, batch_split_game, batch_split_now;
    /* Objects drawn without their two stencil-only flushes. */
    unsigned stencil_shadow, stencil_other;
    /* Shadow-pass particle walks: skipped, and kept because the smudge branch
     * could run (ShadowSkipParticles=1). */
    unsigned particles_skipped, particles_kept;
    unsigned verify_samples, verify_bad; /* ShadowPassVerify */
    /* Merged groups whose setup was handed the light environment the game's
     * own groups would have left stored, and those where the setup's call
     * didn't come (0 by construction). */
    unsigned batch_light_fixed, batch_light_missed;
} pf_sp_counts;

/* What a fix may do (0 = off); the shadow flag is the game's "rendering the
 * shadow map" byte. particles_render etc. may be absent (NULL): that fix
 * then stays off. verify: 1 every 10 s from 20 s on, 2 every redraw (tests). */
void pf_sp_init(const volatile unsigned char *shadow_flag, const pf_sp_data *d, int batch,
                int stencil, int particles, int verify);
/* The sites' indices in pf_sites (pf_sp_enter is told which one runs). */
void pf_sp_set_sites(unsigned occludee, unsigned occluder, unsigned other, unsigned particles);
/* The batch test the same_batch site called (it now calls pf_sp_same_batch). */
void pf_sp_set_same_batch(void *original);
/* The effect-file guard's verdict (fxguard.c, from its own thread): until it
 * is 1, ShadowBatchIgnoreLight leaves the game's test alone. */
void pf_sp_set_batch_proven(int proven);
/* TheParticleSystemManager, read from the forwarder the particle site calls. */
void pf_sp_set_particle_manager(unsigned char *const *mgr);
/* Effective switches after the checks (for the log). */
int pf_sp_batch_on(void);
int pf_sp_stencil_mode(void);
int pf_sp_particles_mode(void);

/* 1 when the fx_group and lenv_set sites are installed where the checked
 * group scan says: without them ShadowBatchIgnoreLight stays off. */
void pf_sp_set_batch_light(int installed);

/* The same_batch site's call target: Is_Rendering_Identical(self, other). */
unsigned char __attribute__((thiscall)) pf_sp_same_batch(unsigned char *self, unsigned char *other);
/* The fx_group site (the group setup's call) and Set_Light_Environment's
 * entry, in every frame: a merged group's setup stores the light environment
 * the game's own groups would have stored last. */
void pf_sp_group(void **slot);
void pf_sp_light_env(void **slot);
/* The stencil and particle sites (from pf_enter, main thread). */
void *pf_sp_enter(unsigned k, void **slot, void *next, void *skip_thunk);

/* LightSkipShadow's state (is it on) and its switch-off, for the verify. */
void pf_sp_set_light_cut(int (*active)(void), void (*off)(void));
/* 1 while the verify holds that fix off. */
int pf_sp_holding(unsigned fix);
/* 1 while the verify draws the shadow map itself (nothing is counted). */
int pf_sp_verifying(void);

/* ShadowPassVerify: due on this game frame's shadow redraw? */
int pf_sp_verify_due(void);
/* Draws the shadow map twice at the shadow site (slot: its stack, callee:
 * UpdateShadowMap), with one fix (or all) off and then as configured,
 * compares the maps and the particle bookkeeping, and times both. The caller
 * then skips the game's own call. */
void pf_sp_verify(void **slot, void *callee);

void pf_sp_take(pf_sp_counts *c);
void pf_sp_totals(pf_sp_counts *c);
/* Once a minute from the frame: a log line with the totals and the verify
 * timings. */
void pf_sp_minute(void);

#endif
