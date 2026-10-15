/* Game data the shadow-pass fixes read (never write, except the particle
 * walk's bookkeeping that ShadowSkipParticles reproduces), found by signature.
 * AotR / ROTWK 2.01 addresses are in the test. */
#ifndef PF_SPSITES_H
#define PF_SPSITES_H

#include "scan.h"

typedef struct {
    /* TheWritableGlobalData: +0x62 shadow mapping on (UpdateShadowMap's own
     * test), +0x60 stencil shadow volumes on (flushOccludedObjectsIntoStencil's),
     * +0x25 the particle walk's smudge switch. */
    const unsigned char *const *gd;
    /* DX8Wrapper's IDirect3DDevice9 (RTS3DScene::Flush's SetVertexShader(0)). */
    void *const *device;
    /* The W3D particle manager's render (vtable +0x40), the walk the shadow
     * pass's RTS3DScene::Flush starts: it returns unless the manager's +0xA8
     * byte is set, clears it, zeroes the smudge counter and the smudge
     * manager's +0x20 and walks every particle system. */
    const unsigned char *particle_render;
    unsigned char *const *smudge;       /* TheSmudgeManager */
    volatile unsigned *smudge_count;    /* smudges the last walk added */
    /* RVA of the FX batch's group scan (0: not found once); the same_batch
     * and fx_group sites must sit at its PF_SP_SCAN_* offsets. */
    unsigned batch_scan;
} pf_sp_data;

#define PF_SP_SCAN_SAME 0x17  /* the batch test's call */
#define PF_SP_SCAN_SETUP 0xA4 /* the group setup's call */
#define PF_SP_RECORD 32       /* { mesh, method count, methods[] } */

#define PF_GD_SHADOW_MAP 0x62
#define PF_GD_SHADOW_VOLUMES 0x60
#define PF_GD_SMUDGES 0x25
#define PF_PMGR_RENDER_PENDING 0xA8 /* the manager's "render pending" byte */
#define PF_PMGR_RENDER_SLOT 0x40   /* vtable offset of the render */
#define PF_SMUDGE_COUNT 0x20       /* TheSmudgeManager's count of the last pass */
#define PF_SMUDGE_STATE 0x04       /* 1: the hardware can't draw smudges */

/* NULL on success, else what wasn't found. */
const char *pf_sp_find(const pf_image *img, pf_sp_data *d);

/* The shadow_particles site calls a forwarder that reads TheParticleSystemManager
 * and calls its vtable +0x40: the global it reads, or NULL if callee isn't that
 * forwarder. */
unsigned char *const *pf_sp_forwarder(const pf_image *img, const unsigned char *callee);

#endif
