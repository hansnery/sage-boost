/* What the light-environment cuts rely on in RTS3DScene::renderOneObject and
 * its callees, found by signature and checked against the original code
 * before any hook is installed. AotR / ROTWK 2.01 addresses are in the test. */
#ifndef PF_LTSITES_H
#define PF_LTSITES_H

#include "scan.h"

typedef struct {
    /* In renderOneObject: where the global lights loop's skip continues (the
     * second material pass check), the light lists' start, the "lights
     * added" test after both lists, and the default light environment's
     * choice (taken when nothing was added and there's no lightmap). */
    unsigned char *glights_skip;
    unsigned char *lists;
    unsigned char *lists_after;
    unsigned char *lists_default;
    /* LightEnvironmentClass::Add_Light (thiscall, the light),
     * Pre_Render_Update (thiscall, the camera's transform), the cdecl sphere
     * test the lists call, and RenderObjClass::Get_Bounding_Sphere. */
    void *add_light;
    void *pre_render_update;
    void *spheres_intersect;
    void *bsphere;
    /* The global int the point light setup reads (a brightness cut-off),
     * and DX8Wrapper's cached D3DRS_LIGHTING. */
    const volatile int *threshold;
    const volatile unsigned *lighting;
    /* Set_Light_Environment's cached environment and the byte the FX batches
     * set while they hold the lights (it then only stores the pointer). */
    const unsigned char *const volatile *light_env;
    const volatile unsigned char *light_lock;
} pf_lt_data;

/* renderOneObject's frame (EBP-relative) and the scene's layout, as the
 * checked code uses them. */
#define PF_LT_F_ENV (-0x2A0)    /* LightEnvironmentClass, 0x228 bytes */
#define PF_LT_F_OBJ_SPHERE (-0x78)
#define PF_LT_F_ADDED (-0x54)    /* int: lights were added */
#define PF_LT_F_LIGHTMAP (-0x0D) /* byte: the terrain lightmap was sampled */
#define PF_LT_F_RINFO 8
#define PF_LT_ENV_SIZE 0x228
#define PF_LT_SCENE_LIGHTS 0x90  /* list sentinel; first node at +4 */
#define PF_LT_SCENE_DYNAMIC 0x118
#define PF_LT_SCENE_DEFAULT_ENV 0x164
#define PF_LT_SCENE_INFANTRY_ENV 0x5B4
#define PF_LT_LIGHT_TYPE 0xC4    /* 0 = point */
#define PF_LT_LIGHT_ENABLED 0x144 /* W3DDynamicLight */
#define PF_LT_CONTAINER 0x7C
#define PF_LT_BSPHERE_SLOT 0x104
#define PF_LT_CAMERA_TM 0x18
/* The light fields Add_Light and its setup read: the transform, bits,
 * type, intensity, colours, attenuation, spot data and the cached sphere. */
#define PF_LT_PARAMS_FROM 0x10
#define PF_LT_PARAMS_TO 0x120

/* NULL on success, else what wasn't found or doesn't match. */
const char *pf_lt_find(const pf_image *img, pf_lt_data *d);

#endif
