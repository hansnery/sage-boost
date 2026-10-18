/* Instancing of unit meshes in the FX shader batch: each supported group's
 * first mesh is drawn by the game, the rest with our instanced vertex shader
 * (see docs/specs/2026-10-13-perf-instancing-design.md). */
#ifndef PF_INST_H
#define PF_INST_H

/* meshes .. bonelist below, the counts kept per pass too. */
#define PF_INST_PASS_COUNTS 15

/* Meshes per reason, draws and verify samples. "Since the last take" for the
 * CSV row, or totals for the log. */
typedef struct {
    unsigned meshes;    /* drawn by the instanced path */
    unsigned draws;     /* instanced draw calls */
    unsigned groups;    /* groups that used it */
    unsigned leaders;   /* first meshes of those groups, drawn by the game */
    unsigned small;     /* groups of fewer than 3 meshes */
    unsigned multipass; /* methods with more than one pass */
    unsigned game_inst; /* the game's own instancing applies */
    unsigned no_shader; /* vertex shader not in the table */
    unsigned disabled;  /* category switched off by the verify mode, or instancing off */
    unsigned bones;     /* too many bones for 2 instances per draw */
    unsigned mismatch;  /* mesh unlike its leader, or the leader's draw not captured */
    unsigned verify;    /* meshes of groups sampled by the verify mode */
    unsigned verify_samples, verify_bad;
    /* Of `meshes`: those whose bone list isn't as long as their joints per
     * vertex (0.8.7 and earlier compared the two and drew them stock). */
    unsigned bonelist;
    /* The shadow-map pass's share of every count above, in the same order. */
    unsigned shadow[PF_INST_PASS_COUNTS];
    unsigned shadow_off;   /* shadow-pass meshes left to the game: ShadowInstancing=0 */
    /* Shadow-pass meshes in groups that continue the previous group's model
     * and method, which the game split by light, colour or opacity (drawn as
     * their own group says; counted to size a merge across groups). */
    unsigned shadow_split;
    unsigned shadow_inst_us; /* time in the shadow pass's instanced draws */
    /* Consumed followers by method count 1..6, regular then extra frames.
     * Leaders are excluded; consumption is not proof of successful drawing. */
    unsigned bulk_followers[2][6];
} pf_inst_counts;

/* Game structure offsets (AotR / ROTWK 2.01; read by the plugin, never written). */
#define PF_MESH_TRANSFORM 0x18 /* Matrix3D, 12 floats */
#define PF_MESH_CONTAINER 0x7C
#define PF_MESH_MODEL 0xC4
#define PF_MESH_BASE_VERTEX 0x300
#define PF_MESH_ANCHOR 0x310 /* pointer to the skeletal world-transform anchor */
#define PF_MODEL_FLAGS 0x18
#define PF_MODEL_SKIN 0x400
#define PF_MODEL_FACING 0xA00 /* ALIGNED | ORIENTED: built toward the camera */
#define PF_MODEL_GEOMETRY 0xBC
#define PF_GEOM_BONES_BEGIN 0x14 /* vector<short> of HTree pivot indices: WorldBones[i] */
#define PF_GEOM_BONES_END 0x18
/* Joints per vertex (1 or 2): what the game sets as NumJointsPerVertex, which
 * picks the shader variant. Not the bone count (that is the list's length). */
#define PF_GEOM_JOINTS 0x20
#define PF_OBJ_GET_HTREE 0xE8 /* vtable offset */
#define PF_HTREE_PIVOTS 0x10  /* count, then the array */
#define PF_PIVOT_STRIDE 0x58
#define PF_PIVOT_TRANSFORM 0x30 /* rotation quaternion, translation: 7 floats */
#define PF_METHOD_REFS 4
#define PF_RECORD_SIZE 32
/* The batch function's frame, at the chunk-of-one call. */
#define PF_FRAME_GROUP_END 8
#define PF_FRAME_PASS (-0x14)
#define PF_FRAME_PASS_COUNT (-0x24)
#define PF_FRAME_MAX_INSTANCES (-0x2C)

/* Before installing. verify: sample groups and compare with the game's draw
 * (1: from 15 s after the first group; 2: at once, for tests). */
void pf_inst_init(int verify);
/* From pf_enter for the two instancing sites (the site's index in pf_sites
 * is passed as mesh_site / dip_site). next: where the hooked call goes. */
void pf_inst_set_sites(unsigned mesh_site, unsigned dip_site);
/* The shadow-map pass: flag is the game's "rendering the shadow map" byte
 * (NULL if not found: then only the shadow-map shaders tell the pass);
 * instancing 0 leaves every mesh of that pass to the game. */
void pf_inst_set_shadow(const volatile unsigned char *flag, int instancing);
void *pf_inst_enter(unsigned k, void **slot, void *next);
/* 1 while the plugin draws an extra frame (fps.c): groups are still drawn
 * instanced, but only bulk-consumption coverage is counted and no verify
 * sample is taken. */
void pf_inst_set_extra(int on);
void pf_inst_take(pf_inst_counts *c);
void pf_inst_totals(pf_inst_counts *c);
/* 0 once instancing switched itself off for the session. */
int pf_inst_active(void);
unsigned pf_inst_variant_count(void);

#endif
