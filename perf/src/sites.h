/* The functions the timing hooks wrap or probe, found by signature. */
#ifndef PF_SITES_H
#define PF_SITES_H

enum {
    PF_FRAME, PF_LOGIC, PF_RENDER, PF_SKEL_HLOD, PF_SKEL_ANIM,
    PF_VIS, PF_CUST, PF_FLUSH, PF_END_SCENE, PF_EVENT_BEGIN, PF_EVENT_END, PF_DRAW,
    PF_REFLECT, PF_SHADOW, PF_SHADOW_INFO, PF_FX_GROUP, PF_INST_MESH, PF_INST_DIP, PF_FPS_WAIT,
    PF_FPS_AVG, PF_HLOD_SET, PF_HLOD_DTOR, PF_PUMP, PF_VIEWS, PF_PARTICLES, PF_DISPLAY_DRAW,
    PF_BUTTON_DRAW, PF_CLOCK_SET, PF_CLOCK_SET_LEFT, PF_SAME_BATCH, PF_STENCIL_OCCLUDEE,
    PF_STENCIL_OCCLUDER, PF_STENCIL_OTHER, PF_SHADOW_PARTICLES, PF_CU_RENDER,
    PF_SCENE_UPDATE,
    PF_ROO, PF_ROO_RESET, PF_ROO_GLIGHTS, PF_ROO_RESET_ND, PF_ROO_LISTS, PF_ROO_ADD_STATIC,
    PF_ROO_ADD_DYN, PF_ROO_ENV, PF_ROO_RENDER, PF_ROO_RENDER2, PF_LENV_SET, PF_SITE_COUNT
};
#define PF_LIGHT_FIRST PF_ROO
#define PF_LIGHT_LAST PF_LENV_SET
/* The installed sites, one bit each (main.c, timing.c). */
typedef unsigned long long pf_site_mask;
#define PF_BIT(k) ((pf_site_mask)1 << (k))
_Static_assert(PF_SITE_COUNT <= 64, "more sites than bits in pf_site_mask");

/* A group's hooks are installed together or not at all. */
enum {
    PF_GROUP_FRAME = 1, PF_GROUP_LOGIC, PF_GROUP_RENDER, PF_GROUP_SKEL, PF_GROUP_SCENE,
    PF_GROUP_PRESENT, PF_GROUP_EVENTS, PF_GROUP_DRAWS, PF_GROUP_REFLECT, PF_GROUP_SHADOW,
    PF_GROUP_BATCH, PF_GROUP_INST, PF_GROUP_FPS, PF_GROUP_UNITS, PF_GROUP_PUMP, PF_GROUP_HOLD,
    PF_GROUP_LAG, PF_GROUP_BUTTONS, PF_GROUP_CLOCKS, PF_GROUP_SBATCH, PF_GROUP_STENCIL,
    PF_GROUP_SPART, PF_GROUP_CATCHUP, PF_GROUP_LIGHTS, PF_GROUP_LTIME, PF_GROUP_LENV,
    PF_GROUP_COUNT
};

/* call_hook: */
#define PF_CALL_INDIRECT 3 /* the site is `call [import]` (FF 15, 6 bytes) */

typedef struct {
    const char *id;
    const char *function;
    const char *signature;
    unsigned site_offset; /* the hooked instruction, from the signature's start */
    unsigned steal;       /* bytes run from the trampoline: whole, position-independent
                           * instructions, or a single jmp rel32 */
    unsigned arg_bytes;   /* bytes the function's ret pops */
    unsigned group;
    int probe;            /* 1: only seen at entry, never wrapped */
    int call_hook;        /* 1: the site is a call (E8 rel32); the stub continues at its callee.
                           * 2: the site is `call [reg+disp32]` (FF 90..97 but not 94),
                           * rewritten as a call to the stub and a nop; the stub continues at
                           * [reg+disp32].
                           * PF_CALL_INDIRECT (3): the same for `call [import]` (FF 15) */
    unsigned rva_201;     /* ROTWK 2.01 and Age of the Ring; log and tests only */
} pf_site;

extern const pf_site pf_sites[PF_SITE_COUNT];

#endif
