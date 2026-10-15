/* What catch-up reads and calls in the game, found by signature and checked
 * against each other. AotR / ROTWK 2.01 addresses are in test_sites. */
#ifndef PF_CUSITES_H
#define PF_CUSITES_H

#include "scan.h"
#include "uistate.h"

typedef struct {
    /* W3DDisplay's frame: where a frame continues without its render (the
     * frame's own path when Begin_Render fails: it clears a byte only that
     * function reads and returns as after a render). Needs EBX = 0, as at
     * the catchup_render site. */
    const unsigned char *no_render;
    /* The 3D scene the frame renders (RTS3DScene), and the two bytes its
     * update loop tests: the projection override and the shadow-map pass. */
    unsigned char *const *scene;
    const volatile unsigned char *proj_override, *shadow_flag;
    /* InGameUI's per-draw update of its floating 2D animations (thiscall,
     * no arguments): the frame calls it after drawing the UI. */
    void *ui_anim;
    /* The frame limiter: the frame start it waits from, its "wait this frame"
     * byte and the clock it reads (timeGetTime's import slot). */
    const volatile unsigned *frame_start;
    const volatile unsigned char *limit_on;
    void *const *time_import;
    /* TheGameClient (draw's "time running" byte at +client_running), TheGameLogic
     * (paused byte at +logic_paused, and the in-game check, thiscall). */
    unsigned char *const *client;
    unsigned client_running;
    unsigned char *const *logic;
    unsigned logic_paused;
    void *is_in_game;
    /* Read only, for the game's speed: TheGameLogic's frame counter
     * (+logic_frame), which GameLogic::update steps once per logic frame in
     * a game, and BfmeLogicFramesPerSecond. 0 / NULL if not found (catch-up
     * doesn't need them). */
    unsigned logic_frame;
    const volatile unsigned *logic_fps;
    int *screenshots;          /* pending screenshot tiles */
    void *const *device;       /* DX8Wrapper's IDirect3DDevice9, for the notice; NULL: no notice */
    pf_ui_state ui;            /* placing a building, scrolling */
} pf_cu_sites;

/* The scene's update list (proven by the update loop's bytes): sentinel node
 * at scene + 0x78, first node at +0x7C; a node's next at +4, its interface
 * pointer at +0xC, the object 8 bytes before it. Class_ID is vtable +0x0C
 * (4: the terrain), On_Frame_Update vtable +0x34. */
#define PF_SCENE_UPDATE_LIST 0x78
#define PF_SCENE_UPDATE_FIRST 0x7C
#define PF_NODE_NEXT 4
#define PF_NODE_OBJ 0xC
#define PF_NODE_OBJ_ADJUST 8
#define PF_ROBJ_CLASS_ID 0x0C
#define PF_ROBJ_ON_FRAME_UPDATE 0x34
#define PF_CLASSID_TERRAIN 4

/* NULL on success, else what wasn't found (*out is zeroed then). */
const char *pf_cu_find(const pf_image *img, pf_cu_sites *out);

#endif
