/* What an extra frame needs from the game, found by signature: the display
 * and its draw, the draw's own timing state (saved and restored around an
 * extra frame), the W3D sync time, the client frame, the logic random seed
 * and the logic CRC the game's -verifyClientCRC check uses. */
#ifndef PF_FPSITES_H
#define PF_FPSITES_H

#include "scan.h"

typedef struct {
    void *const *display;      /* TheDisplay */
    void *draw;                /* W3DDisplay::draw, the display's vtable slot 12 */
    void *const *hwnd;         /* the game window */
    void *is_iconic;           /* IsIconic, from the game's import table */
    void *const *client;       /* the object draw reads the client frame from (vtable +0x7C) */
    unsigned *last_client_frame;
    unsigned *sync_acc;        /* draw's W3D time accumulator */
    unsigned *last_draw_ms;    /* draw's own frame-limit clock */
    unsigned *skip_count;      /* draw's frame-skip countdown */
    unsigned *init_flags;      /* draw's one-time init bits */
    int *screenshots;          /* pending screenshot tiles: no extra frames while > 0 */
    unsigned *sync_time, *prev_sync_time; /* WW3D */
    unsigned *logic_seed;      /* the logic random generator's state */
    unsigned char *verify_crc; /* set by -verifyClientCRC */
    void *const *logic;        /* TheGameLogic */
    void *is_in_game;          /* the check BFMEDesyncCheck makes before getCRC, thiscall */
    void *get_crc;             /* GameLogic::getCRC(int), thiscall */
    void *const *tactical_view; /* TheTacticalView (a W3DView) */
    unsigned camera_off;       /* W3DView: its CameraClass* */
    unsigned set_transform;    /* RenderObjClass vtable byte offset of Set_Transform */
    void *const *particles;    /* TheFXParticleSystemManager */
    unsigned particles_slot;   /* its vtable byte offset of update, as the draw calls it */
} pf_fps_sites;

/* RenderObjClass: the Matrix3D transform (3 rows x 4 floats). */
#define PF_RENDOBJ_TRANSFORM 0x18

/* NULL on success, else what wasn't found. *out is zeroed on failure. */
const char *pf_fps_find(const pf_image *img, pf_fps_sites *out);

#endif
