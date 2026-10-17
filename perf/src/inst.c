#include "inst.h"

#include <stddef.h>

#include "d3d9vt.h"
#include "declcache.h"
#include "depthcmp.h"
#include "inst_shaders.h"
#include "log.h"
#include "stubs.h"

_Static_assert(offsetof(pf_inst_counts, shadow) == PF_INST_PASS_COUNTS * sizeof(unsigned) &&
                   offsetof(pf_inst_counts, bonelist) + sizeof(unsigned) ==
                       offsetof(pf_inst_counts, shadow),
               "the per-pass counts mirror meshes .. bonelist");

/* A verify category is a game shader variant in one pass: the variant's
 * index, plus PF_INST_VARIANTS in the shadow-map pass. */
#define PF_INST_CATS (2 * PF_INST_VARIANTS)

/* Both caches hold a reference on the game's object, so its address can't be
 * reused by another one while it is cached. */
#define VS_CACHE 256
/* Groups whose bone list differs in length from their joints per vertex get most of
 * the verify samples until this many of them were checked. */
#define VERIFY_BONELIST_TARGET 60
#define MAX_REGS 256
#define MAX_FUNCTION 32768
#define VERIFY_SIZE 512
#define VERIFY_FIRST_MS 15000
#define VERIFY_PERIOD_MS 5000
#define VERIFY_WINDOW_MS 2000
#define VERIFY_PER_WINDOW 6
/* The shadow pass comes first in a frame; it gets at most half of a window. */
#define VERIFY_SHADOW_PER_WINDOW (VERIFY_PER_WINDOW / 2)
#define VERIFY_PER_CATEGORY 2
#define VERIFY_BAD_LIMIT 2
#define VERIFY_TOLERANCE 24
#define VERIFY_LOGGED 3
#define SUMMARY_MS 60000
#define LOGGED_SHADERS 32

enum { M_IDLE, M_LEADER, M_INST, M_VERIFY, M_STOCK };

#define REASON(f) ((unsigned)(offsetof(pf_inst_counts, f) / sizeof(unsigned)))

typedef struct {
    void *vs;
    int variant;  /* -1: not in the table */
    int unknown;  /* then its slot in g_unknown, or -1 */
} vs_entry;

typedef void *(__attribute__((thiscall)) * htree_fn)(void *self);
typedef void(__attribute__((thiscall)) * render_fn)(const void *mesh, void *method, int count);

typedef struct {
    int mode;
    unsigned reason; /* REASON() of the group's stock meshes */
    const unsigned char *end;
    render_fn render; /* the game's per-mesh render */
    void *method;
    const unsigned char *model;
    unsigned base_vertex;
    int skinned;
    const short *bonemap;
    unsigned nbones, stride, cap, count;
    unsigned joints;  /* skinned: joints per vertex (geometry +0x20) */
    int bonelist;     /* skinned, and the bone list isn't as long as the joints */
    int unknown;      /* no_shader: the unknown shader's slot, or -1 */
    int shadow;       /* drawn into the shadow map */
    /* The leader's draw, captured at DrawIndexedPrimitive. */
    int armed;
    unsigned dips, capture_reason;
    unsigned mm; /* why the group's stock meshes count as mismatch (MM_*) */
    int variant;
    unsigned cat; /* the verify category: variant, + PF_INST_VARIANTS in the shadow pass */
    pf_decl_entry *decl;
    void *dev, *ea_vs;
    unsigned dip[6];
    void *prev_vb;
    unsigned prev_off, prev_stride;
    const unsigned char *verify_mesh[MAX_REGS / 2];
    unsigned verify_n;
} group;

static group g;

/* The kinds of "mismatch", for the log only (the CSV keeps one column). The
 * device-state ones are where capture() found the leader's draw unlike what
 * the instanced draw needs. */
enum {
    MM_REFS, MM_GEOMETRY, MM_FACING, MM_BONES_NONE, MM_JOINTS_BAD, MM_JOINTS_SHADER, MM_DRAWS,
    MM_STATE, MM_MODEL, MM_SKIN_SHADER, MM_DECL, MM_FREQ, MM_COUNT
};
static unsigned g_mm[MM_COUNT], g_splits, g_bone_logs;
/* The unknown vertex shaders (no_shader): meshes per shader. */
#define UNKNOWN_SHADERS 32
static unsigned g_unknown_hash[UNKNOWN_SHADERS], g_unknown_size[UNKNOWN_SHADERS],
    g_unknown_meshes[UNKNOWN_SHADERS], g_unknown_n;
/* The groups with a bone list unlike their joints in length, checked on their own:
 * per category, verify results and the switch that sends them back to the
 * game (the category's other groups keep instancing). */
static unsigned g_bl_ok[PF_INST_CATS], g_bl_bad[PF_INST_CATS], g_bl_samples;
static unsigned char g_bl_off[PF_INST_CATS], g_bl_window[PF_INST_CATS];
static unsigned g_stage[MAX_REGS * 4], g_saved[MAX_REGS * 4];
static unsigned char g_function[MAX_FUNCTION];
static vs_entry g_vs[VS_CACHE];
static unsigned g_vs_n;
static pf_decl_cache g_decl;
static void *g_ours[PF_INST_VARIANTS];
static unsigned char g_ours_failed[PF_INST_VARIANTS];
static void *g_dev;
static void *g_ivb;
static unsigned g_ivb_off[MAX_REGS + 1]; /* first float of each stride's base offsets */
static int g_can_read_consts;
static int g_failed;
static int g_verify;
static DWORD g_thread;
static unsigned g_mesh_site = ~0u, g_dip_site = ~0u;
static pf_inst_counts g_take, g_total;
static unsigned g_cat_meshes[PF_INST_CATS], g_cat_ok[PF_INST_CATS];
static unsigned g_cat_bad[PF_INST_CATS], g_cat_empty[PF_INST_CATS];
static unsigned char g_cat_off[PF_INST_CATS], g_cat_window[PF_INST_CATS];
static int g_started, g_window_open;
static DWORD g_verify_next, g_verify_close, g_summary_next;
static unsigned g_window_samples, g_window_shadow;
static unsigned g_logged_hash[LOGGED_SHADERS], g_logged_n;
/* The shadow-map pass. */
static const volatile unsigned char *g_shadow_flag;
static int g_shadow_inst = 1;
static const unsigned char *g_prev_end, *g_prev_model;
static const void *g_prev_method;
static LONGLONG g_qpf, g_shadow_ticks, g_shadow_ticks_total;
static unsigned g_formats_logged;

static int g_extra; /* an extra frame of fps.c */

void pf_inst_set_extra(int on)
{
    g_extra = on;
}

static void add(unsigned reason, unsigned n)
{
    if (g_extra)
        return;
    if (reason == REASON(mismatch))
        g_mm[g.mm < MM_COUNT ? g.mm : MM_STATE] += n;
    else if (reason == REASON(no_shader) && g.unknown >= 0 && (unsigned)g.unknown < g_unknown_n)
        g_unknown_meshes[g.unknown] += n;
    ((unsigned *)&g_take)[reason] += n;
    ((unsigned *)&g_total)[reason] += n;
    if (g.shadow && reason < PF_INST_PASS_COUNTS) {
        g_take.shadow[reason] += n;
        g_total.shadow[reason] += n;
    }
}

static LONGLONG qpc(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

static unsigned long long ticks_us(LONGLONG ticks)
{
    return g_qpf ? (unsigned long long)ticks * 1000000u / (unsigned long long)g_qpf : 0;
}

static void release(void *obj)
{
    if (obj)
        ((pf_release_fn)PF_VT(obj)[PF_UNK_RELEASE])(obj);
}

static void add_ref(void *obj)
{
    if (obj)
        ((pf_release_fn)PF_VT(obj)[PF_UNK_ADDREF])(obj);
}

static unsigned fnv1a(const unsigned char *p, unsigned n)
{
    unsigned h = 0x811C9DC5u, i;
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 0x01000193u;
    return h;
}

/* IEEE single bits of a small non-negative integer, without floating point. */
static unsigned float_bits(unsigned v)
{
    unsigned e = 0;
    if (!v)
        return 0;
    while (v >> (e + 1))
        e++;
    return (127 + e) << 23 | ((v << (23 - e)) & 0x7FFFFF);
}

static void copy_dwords(unsigned *dst, const unsigned *src, unsigned n)
{
    while (n--)
        *dst++ = *src++;
}

static void fail_session(const char *why, unsigned code)
{
    if (!g_failed)
        fu_log_event("inst: switched off for this session: %s (%08X); the game draws every mesh",
                     why, code);
    g_failed = 1;
}

/* Caches belong to one device. Declaration references are released on
 * transitions; the pre-existing shader/buffer caches retain their policy. */
static int use_device(void *dev)
{
    unsigned caps[PF_CAPS_SIZE / 4], probe[4], total = 0, s, j;
    void **vt = PF_VT(dev);
    unsigned *p;
    HRESULT hr;

    if (dev == g_dev)
        return g_ivb != 0;
    if (g_dev)
        fu_log_event("inst: the device changed, cached objects dropped");
    g_dev = dev;
    g_vs_n = 0;
    /* Held COM references keep declarations alive across device recreation.
     * capture is entered only for a new leader, with no pending draw. */
    pf_decl_cache_clear(&g_decl);
    g_ivb = 0;
    for (j = 0; j < PF_INST_VARIANTS; j++) {
        g_ours[j] = 0;
        g_ours_failed[j] = 0;
    }
    hr = ((pf_get_caps_fn)vt[PF_DEV_GET_DEVICE_CAPS])(dev, caps);
    if (FAILED(hr)) {
        fail_session("GetDeviceCaps failed", (unsigned)hr);
        return 0;
    }
    if ((caps[PF_CAPS_VS_VERSION / 4] & 0xFFFF) < 0x0200 || caps[PF_CAPS_MAX_VS_CONST / 4] < 256) {
        fail_session("vertex shader 2.0 with 256 constants needed",
                     caps[PF_CAPS_MAX_VS_CONST / 4]);
        return 0;
    }
    g_can_read_consts =
        SUCCEEDED(((pf_const_f_fn)vt[PF_DEV_GET_VS_CONST_F])(dev, 0, probe, 1));
    for (s = 1; s <= MAX_REGS; s++) {
        g_ivb_off[s] = total;
        total += MAX_REGS / s;
    }
    hr = ((pf_create_vb_fn)vt[PF_DEV_CREATE_VERTEX_BUFFER])(dev, total * 4, PF_D3DUSAGE_WRITEONLY,
                                                            0, PF_D3DPOOL_MANAGED, &g_ivb, 0);
    if (FAILED(hr) || !g_ivb) {
        g_ivb = 0;
        fail_session("can't create the instance buffer", (unsigned)hr);
        return 0;
    }
    hr = ((pf_vb_lock_fn)PF_VT(g_ivb)[PF_VB_LOCK])(g_ivb, 0, 0, (void **)&p, 0);
    if (FAILED(hr)) {
        release(g_ivb);
        g_ivb = 0;
        fail_session("can't fill the instance buffer", (unsigned)hr);
        return 0;
    }
    for (s = 1; s <= MAX_REGS; s++)
        for (j = 0; j < MAX_REGS / s; j++)
            p[g_ivb_off[s] + j] = float_bits(j * s);
    ((pf_unlock_fn)PF_VT(g_ivb)[PF_VB_UNLOCK])(g_ivb);
    fu_log_event("inst: device ready: vertex shader %X, %u constants, constant read-back %s",
                 caps[PF_CAPS_VS_VERSION / 4] & 0xFFFF, caps[PF_CAPS_MAX_VS_CONST / 4],
                 g_can_read_consts ? "yes" : "no");
    return 1;
}

/* The table variant of the bound vertex shader, or -1 (then *unknown is the
 * shader's slot among the unknown ones, or -1). */
static int variant_of(void *vs, int *unknown)
{
    UINT size = 0;
    unsigned i, h;
    int variant = -1, slot = -1;
    pf_get_data_fn get;

    *unknown = -1;
    if (!vs)
        return -1;
    for (i = 0; i < g_vs_n; i++) {
        if (g_vs[i].vs == vs) {
            *unknown = g_vs[i].unknown;
            return g_vs[i].variant;
        }
    }
    get = (pf_get_data_fn)PF_VT(vs)[PF_VS_GET_FUNCTION];
    if (SUCCEEDED(get(vs, 0, &size)) && size && size <= MAX_FUNCTION &&
        SUCCEEDED(get(vs, g_function, &size))) {
        h = fnv1a(g_function, size);
        for (i = 0; i < PF_INST_VARIANTS; i++)
            if (pf_inst_variants[i].ea_hash == h && pf_inst_variants[i].ea_size == size)
                variant = (int)i;
        if (variant < 0) {
            for (i = 0; i < g_unknown_n && g_unknown_hash[i] != h; i++)
                ;
            if (i == g_unknown_n && g_unknown_n < UNKNOWN_SHADERS) {
                g_unknown_hash[g_unknown_n] = h;
                g_unknown_size[g_unknown_n] = size;
                g_unknown_meshes[g_unknown_n++] = 0;
            }
            if (i < g_unknown_n)
                slot = (int)i;
        }
        if (variant < 0 && g_logged_n < LOGGED_SHADERS) {
            for (i = 0; i < g_logged_n && g_logged_hash[i] != h; i++)
                ;
            if (i == g_logged_n) {
                g_logged_hash[g_logged_n++] = h;
                fu_log_event("inst: vertex shader %08X (%u bytes) is not in the table: its groups "
                             "stay with the game",
                             h, size);
            }
        }
    }
    if (g_vs_n < VS_CACHE) {
        add_ref(vs);
        g_vs[g_vs_n].vs = vs;
        g_vs[g_vs_n].unknown = slot;
        g_vs[g_vs_n++].variant = variant;
    }
    *unknown = slot;
    return variant;
}

/* At the leader's DrawIndexedPrimitive: the device holds the group's full
 * state. Records the draw and decides whether the group can be instanced. */
static void capture(void **slot)
{
    void *dev = slot[1], *vs = 0, *decl = 0;
    void **vt;
    const pf_inst_variant *v;
    UINT freq = 1;
    unsigned i, s;

    if (++g.dips > 1)
        return;
    g.capture_reason = REASON(mismatch);
    g.mm = MM_STATE;
    if (!dev || !use_device(dev)) {
        g.capture_reason = REASON(disabled);
        return;
    }
    vt = PF_VT(dev);
    g.dev = dev;
    for (i = 0; i < 6; i++)
        g.dip[i] = (unsigned)(UINT_PTR)slot[2 + i];
    if (FAILED(((pf_ptr_fn)vt[PF_DEV_GET_VS])(dev, &vs)))
        vs = 0;
    release(vs); /* the effect keeps it alive */
    g.ea_vs = vs;
    g.variant = variant_of(vs, &g.unknown);
    if (g.variant < 0) {
        g.capture_reason = REASON(no_shader);
        return;
    }
    v = &pf_inst_variants[g.variant];
    /* The shadow-map shaders draw only into the shadow map, which tells the
     * pass when the game's flag wasn't found. */
    if (v->family == 3)
        g.shadow = 1;
    if (g.shadow && !g_shadow_inst) {
        g.capture_reason = REASON(shadow_off);
        return;
    }
    g.cat = (unsigned)g.variant + (g.shadow ? PF_INST_VARIANTS : 0);
    if (g_cat_off[g.cat] || g_ours_failed[g.variant] || (g.bonelist && g_bl_off[g.cat])) {
        g.capture_reason = REASON(disabled);
        return;
    }
    if ((v->joints != 0) != (g.skinned != 0)) {
        g.mm = MM_SKIN_SHADER;
        return;
    }
    /* The game picks the variant by the geometry's joints per vertex; if the
     * bound one has another count, the mesh isn't what this path assumes. */
    if (g.skinned && v->joints != g.joints) {
        g.mm = MM_JOINTS_SHADER;
        return;
    }
    if (FAILED(((pf_ptr_fn)vt[PF_DEV_GET_DECL])(dev, &decl)))
        decl = 0;
    release(decl);
    g.mm = MM_DECL;
    g.decl = pf_decl_cache_get(&g_decl, dev, decl);
    if (!g.decl)
        return;
    g.mm = MM_FREQ;
    for (s = 0; s <= g.decl->inst_stream; s++) {
        if (!(g.decl->streams >> s & 1) && s != g.decl->inst_stream)
            continue;
        if (FAILED(((pf_get_freq_fn)vt[PF_DEV_GET_STREAM_FREQ])(dev, s, &freq)) || freq != 1)
            return;
    }
    g.mm = MM_STATE;
    g.prev_vb = 0;
    if (FAILED(((pf_get_stream_fn)vt[PF_DEV_GET_STREAM])(dev, g.decl->inst_stream, &g.prev_vb,
                                                         &g.prev_off, &g.prev_stride)))
        return;
    release(g.prev_vb);
    if (!g_ours[g.variant]) {
        HRESULT hr = ((pf_create_obj_fn)vt[PF_DEV_CREATE_VS])(dev, v->code, &g_ours[g.variant]);
        if (FAILED(hr) || !g_ours[g.variant]) {
            g_ours[g.variant] = 0;
            g_ours_failed[g.variant] = 1;
            fu_log_event("inst: can't create our shader for %s (%08X): left to the game", v->name,
                         (unsigned)hr);
            g.capture_reason = REASON(disabled);
            return;
        }
    }
    /* More bones than the game's palette: the game skips them, we don't match. */
    if (g.stride * 2 > v->ir_count || (g.skinned && g.nbones > v->ea_bones)) {
        g.capture_reason = REASON(bones);
        return;
    }
    g.cap = v->ir_count / g.stride;
    g.capture_reason = 0;
}

/* Draws the staged instances with our shader, then puts back exactly what
 * the game had bound. counted: 0 for a verify sample's off-screen copy. */
static int draw_batch(int counted)
{
    void *dev = g.dev;
    void **vt;
    const pf_inst_variant *v;
    unsigned n = g.count, regs = g.count * g.stride, s, inst;
    pf_set_freq_fn freq;
    pf_const_f_fn set_const;
    int saved = 0, timed = counted && g.shadow && !g_extra;
    LONGLONG t0 = timed ? qpc() : 0;
    HRESULT hr;

    g.count = 0;
    if (!n || !dev || !g.decl)
        return 1;
    vt = PF_VT(dev);
    v = &pf_inst_variants[g.variant];
    inst = g.decl->inst_stream;
    freq = (pf_set_freq_fn)vt[PF_DEV_SET_STREAM_FREQ];
    set_const = (pf_const_f_fn)vt[PF_DEV_SET_VS_CONST_F];
    if (g_can_read_consts)
        saved = SUCCEEDED(((pf_const_f_fn)vt[PF_DEV_GET_VS_CONST_F])(dev, v->ir_reg, g_saved, regs));
    ((pf_ptr_fn)vt[PF_DEV_SET_VS])(dev, g_ours[g.variant]);
    ((pf_ptr_fn)vt[PF_DEV_SET_DECL])(dev, g.decl->ours);
    ((pf_set_stream_fn)vt[PF_DEV_SET_STREAM])(dev, inst, g_ivb, g_ivb_off[g.stride] * 4, 4);
    for (s = 0; s < inst; s++)
        if (g.decl->streams >> s & 1)
            freq(dev, s, PF_STREAM_INDEXED | n);
    freq(dev, inst, PF_STREAM_INSTANCE | 1);
    set_const(dev, v->ir_reg, g_stage, regs);
    hr = ((pf_dip_fn)vt[PF_DEV_DRAW_INDEXED])(dev, g.dip[0], (INT)g.dip[1], g.dip[2], g.dip[3],
                                               g.dip[4], g.dip[5]);
    for (s = 0; s <= inst; s++)
        if (g.decl->streams >> s & 1 || s == inst)
            freq(dev, s, 1);
    ((pf_set_stream_fn)vt[PF_DEV_SET_STREAM])(dev, inst, g.prev_vb, g.prev_off, g.prev_stride);
    ((pf_ptr_fn)vt[PF_DEV_SET_DECL])(dev, g.decl->game);
    ((pf_ptr_fn)vt[PF_DEV_SET_VS])(dev, g.ea_vs);
    if (saved)
        set_const(dev, v->ir_reg, g_saved, regs);
    if (FAILED(hr)) {
        fail_session("an instanced draw failed", (unsigned)hr);
        return 0;
    }
    if (counted) {
        add(REASON(meshes), n);
        add(REASON(draws), 1);
        if (g.bonelist)
            add(REASON(bonelist), n);
        if (!g_extra)
            g_cat_meshes[g.cat] += n;
    }
    if (timed)
        g_shadow_ticks += qpc() - t0;
    return 1;
}

/* Copies one mesh's bones (or world matrix) into the next instance slot. */
static int stage(const unsigned char *mesh)
{
    unsigned *dst = g_stage + g.count * g.stride * 4;
    unsigned j;

    if (g.skinned) {
        void *container = *(void *const *)(mesh + PF_MESH_CONTAINER);
        const unsigned char *htree, *pivots;
        int count;
        if (!container)
            return 0;
        htree = ((htree_fn)PF_VT(container)[PF_OBJ_GET_HTREE / 4])(container);
        if (!htree)
            return 0;
        count = *(const int *)(htree + PF_HTREE_PIVOTS);
        pivots = *(const unsigned char *const *)(htree + PF_HTREE_PIVOTS + 4);
        if (!pivots)
            return 0;
        for (j = 0; j < g.nbones; j++) {
            int idx = g.bonemap[j];
            if (idx < 0 || idx >= count)
                return 0;
            copy_dwords(dst, (const unsigned *)(pivots + idx * PF_PIVOT_STRIDE + PF_PIVOT_TRANSFORM),
                        7);
            dst[7] = 0;
            dst += 8;
        }
    } else {
        copy_dwords(dst, (const unsigned *)(mesh + PF_MESH_TRANSFORM), 12);
    }
    g.count++;
    return 1;
}

static void verify_tick(DWORD now)
{
    unsigned i;
    if (!g_verify)
        return;
    if (g_window_open) {
        if (g_window_samples >= VERIFY_PER_WINDOW || (int)(now - g_verify_close) >= 0) {
            g_window_open = 0;
            g_verify_next = now + VERIFY_PERIOD_MS;
        }
    } else if ((int)(now - g_verify_next) >= 0) {
        g_window_open = 1;
        g_window_samples = g_window_shadow = 0;
        g_verify_close = now + VERIFY_WINDOW_MS;
        for (i = 0; i < PF_INST_CATS; i++)
            g_cat_window[i] = g_bl_window[i] = 0;
    }
}

static void summary(void)
{
    unsigned i;
    pf_inst_counts *t = &g_total;
    fu_log_event("inst: totals: %u meshes in %u instanced draws (%u groups, %u leaders); left to "
                 "the game: small %u, multipass %u, game instancing %u, no shader %u, disabled %u, "
                 "bones %u, mismatch %u, verify %u; verify samples %u, bad %u",
                 t->meshes, t->draws, t->groups, t->leaders, t->small, t->multipass,
                 t->game_inst, t->no_shader, t->disabled, t->bones, t->mismatch, t->verify,
                 t->verify_samples, t->verify_bad);
    fu_log_event("inst:   instanced with a bone list unlike the joints per vertex in length (refused "
                 "before 0.8.8): %u meshes",
                 t->bonelist);
    fu_log_event("inst:   mismatch by kind: no reference count %u, geometry without its own buffer "
                 "%u, camera-facing %u, bone map %u, leader not one draw %u, device state %u, model "
                 "change %u; groups split at a model change %u",
                 g_mm[MM_REFS], g_mm[MM_GEOMETRY], g_mm[MM_FACING],
                 g_mm[MM_BONES_NONE] + g_mm[MM_JOINTS_BAD] + g_mm[MM_JOINTS_SHADER], g_mm[MM_DRAWS],
                 g_mm[MM_STATE] + g_mm[MM_SKIN_SHADER] + g_mm[MM_DECL] +
                     g_mm[MM_FREQ],
                 g_mm[MM_MODEL], g_splits);
    fu_log_event("inst:   bone map: no bone list %u, joints per vertex not 1 or 2 %u, joints unlike "
                 "the bound shader's %u",
                 g_mm[MM_BONES_NONE], g_mm[MM_JOINTS_BAD], g_mm[MM_JOINTS_SHADER]);
    fu_log_event("inst:   device state: shader skinning unlike the mesh's %u, vertex declaration "
                 "not usable %u, stream frequency set %u, stream not "
                 "readable %u (declarations cached %u, shaders %u)",
                 g_mm[MM_SKIN_SHADER], g_mm[MM_DECL], g_mm[MM_FREQ],
                 g_mm[MM_STATE], g_decl.count, g_vs_n);
    fu_log_event("inst:   declaration cache (all draws): hits %u, misses %u, evictions %u, "
                 "creation failures %u", g_decl.hits, g_decl.misses, g_decl.evictions,
                 g_decl.failures);
    for (i = 0; i < g_unknown_n; i++)
        fu_log_event("inst:   no shader: vertex shader %08X (%u bytes): %u meshes",
                     g_unknown_hash[i], g_unknown_size[i], g_unknown_meshes[i]);
    {
        const unsigned *s = t->shadow;
        unsigned ms = (unsigned)(ticks_us(g_shadow_ticks_total + g_shadow_ticks) / 1000);
        fu_log_event(
            "inst:   shadow-map pass%s: %u meshes in %u instanced draws (%u groups, %u leaders, "
            "%u ms in those draws); left to the game: ShadowInstancing=0 %u, small %u, "
            "multipass %u, game instancing %u, no shader %u, disabled %u, bones %u, mismatch %u, "
            "verify %u; verify samples %u, bad %u; in groups the game split from a group of the "
            "same model and method %u",
            g_shadow_flag ? "" : " (flag not found: only groups with a shadow-map shader)",
            s[REASON(meshes)], s[REASON(draws)], s[REASON(groups)], s[REASON(leaders)], ms,
            t->shadow_off, s[REASON(small)], s[REASON(multipass)], s[REASON(game_inst)],
            s[REASON(no_shader)], s[REASON(disabled)], s[REASON(bones)], s[REASON(mismatch)],
            s[REASON(verify)], s[REASON(verify_samples)], s[REASON(verify_bad)], t->shadow_split);
    }
    for (i = 0; i < PF_INST_CATS; i++) {
        const pf_inst_variant *v = &pf_inst_variants[i % PF_INST_VARIANTS];
        if (!g_cat_meshes[i] && !g_cat_ok[i] && !g_cat_bad[i] && !g_cat_empty[i] && !g_bl_ok[i] &&
            !g_bl_bad[i])
            continue;
        fu_log_event("inst:   %s (game shader %u%s): %u meshes, verify ok %u, bad %u, empty %u%s; "
                     "with a bone list unlike the joints: verify ok %u, bad %u%s",
                     v->name, v->ea_index, i >= PF_INST_VARIANTS ? ", shadow-map pass" : "",
                     g_cat_meshes[i], g_cat_ok[i], g_cat_bad[i], g_cat_empty[i],
                     g_cat_off[i] ? ", OFF" : "", g_bl_ok[i], g_bl_bad[i],
                     g_bl_off[i] ? ", OFF" : "");
    }
}

/* Two R32F shadow-map images: covered where either isn't the clear depth,
 * bad where the depths differ by more than PF_DEPTH_TOLERANCE 2^-24 steps
 * (*maxd in the same steps). */
static void compare_depth(const pf_locked_rect *la, const pf_locked_rect *lb, unsigned *covered,
                          unsigned *bad, unsigned *maxd)
{
    unsigned x, y;
    for (y = 0; y < VERIFY_SIZE; y++) {
        const unsigned *pa = (const unsigned *)((const unsigned char *)la->bits + y * la->pitch);
        const unsigned *pb = (const unsigned *)((const unsigned char *)lb->bits + y * lb->pitch);
        for (x = 0; x < VERIFY_SIZE; x++) {
            unsigned d;
            if (pa[x] == PF_DEPTH_CLEAR && pb[x] == PF_DEPTH_CLEAR)
                continue;
            (*covered)++;
            d = pf_depth_diff(pa[x], pb[x]);
            if (d > *maxd)
                *maxd = d;
            if (d > PF_DEPTH_TOLERANCE)
                (*bad)++;
        }
    }
}

/* Two A8R8G8B8 images: covered where either isn't black, bad where a
 * channel differs by more than VERIFY_TOLERANCE (*maxd in 1/255 steps). */
static void compare_color(const pf_locked_rect *la, const pf_locked_rect *lb, unsigned *covered,
                          unsigned *bad, unsigned *maxd)
{
    unsigned x, y, c;
    for (y = 0; y < VERIFY_SIZE; y++) {
        const unsigned *pa = (const unsigned *)((const unsigned char *)la->bits + y * la->pitch);
        const unsigned *pb = (const unsigned *)((const unsigned char *)lb->bits + y * lb->pitch);
        for (x = 0; x < VERIFY_SIZE; x++) {
            unsigned a = pa[x], b = pb[x], worst = 0;
            if (!a && !b)
                continue;
            (*covered)++;
            for (c = 0; c < 32; c += 8) {
                unsigned ca = a >> c & 0xFF, cb = b >> c & 0xFF;
                unsigned d = ca > cb ? ca - cb : cb - ca;
                if (d > worst)
                    worst = d;
            }
            if (worst > *maxd)
                *maxd = worst;
            if (worst > VERIFY_TOLERANCE)
                (*bad)++;
        }
    }
}

static void compare(void *sa, void *sb, int depth, unsigned *covered, unsigned *bad,
                    unsigned *maxd)
{
    pf_locked_rect la, lb;
    pf_lock_rect_fn lock_a = (pf_lock_rect_fn)PF_VT(sa)[PF_SURF_LOCK_RECT];
    pf_lock_rect_fn lock_b = (pf_lock_rect_fn)PF_VT(sb)[PF_SURF_LOCK_RECT];

    *covered = *bad = *maxd = 0;
    if (FAILED(lock_a(sa, &la, 0, PF_D3DLOCK_READONLY)))
        return;
    if (FAILED(lock_b(sb, &lb, 0, PF_D3DLOCK_READONLY))) {
        ((pf_unlock_fn)PF_VT(sa)[PF_SURF_UNLOCK_RECT])(sa);
        return;
    }
    if (depth)
        compare_depth(&la, &lb, covered, bad, maxd);
    else
        compare_color(&la, &lb, covered, bad, maxd);
    ((pf_unlock_fn)PF_VT(sb)[PF_SURF_UNLOCK_RECT])(sb);
    ((pf_unlock_fn)PF_VT(sa)[PF_SURF_UNLOCK_RECT])(sa);
}

/* The group's meshes after the leader, drawn off screen by the game and by
 * the instanced path, compared. Every surface lives only inside the call. */
static void verify_sample(void)
{
    void *dev = g.dev, *rt = 0, *rt1 = 0, *ds = 0, *a = 0, *b = 0, *d = 0, *sa = 0, *sb = 0;
    void **vt = PF_VT(dev);
    pf_create_rt_fn create_rt = (pf_create_rt_fn)vt[PF_DEV_CREATE_RENDER_TARGET];
    pf_create_rt_fn create_ds = (pf_create_rt_fn)vt[PF_DEV_CREATE_DEPTH_STENCIL];
    pf_create_plain_fn create_plain = (pf_create_plain_fn)vt[PF_DEV_CREATE_OFFSCREEN_PLAIN];
    pf_set_rt_fn set_rt = (pf_set_rt_fn)vt[PF_DEV_SET_RENDER_TARGET];
    pf_ptr_fn set_ds = (pf_ptr_fn)vt[PF_DEV_SET_DEPTH_STENCIL];
    pf_clear_fn clear = (pf_clear_fn)vt[PF_DEV_CLEAR];
    pf_two_ptr_fn get_data = (pf_two_ptr_fn)vt[PF_DEV_GET_RENDER_TARGET_DATA];
    const pf_inst_variant *v = &pf_inst_variants[g.variant];
    const char *pass = g.shadow ? ", shadow-map pass" : "";
    pf_viewport vp;
    pf_surface_desc desc = { 0 };
    RECT sc, full = { 0, 0, VERIFY_SIZE, VERIFY_SIZE };
    unsigned i, n = g.verify_n, covered, bad, maxd, c = g.cat, fmt = PF_D3DFMT_A8R8G8B8;
    unsigned clear_color = 0, tolerance = VERIFY_TOLERANCE;
    unsigned *refs = (unsigned *)((unsigned char *)g.method + PF_METHOD_REFS);
    const char *unit = "pixels", *scale = "";
    int failed;

    if (!n)
        return;
    g_window_samples++;
    if (g.shadow)
        g_window_shadow++;
    if (g.bonelist) {
        g_bl_window[c]++;
        g_bl_samples++;
    } else {
        g_cat_window[c]++;
    }
    if (FAILED(((pf_get_rt_fn)vt[PF_DEV_GET_RENDER_TARGET])(dev, 0, &rt)) || !rt)
        return;
    if (SUCCEEDED(((pf_get_rt_fn)vt[PF_DEV_GET_RENDER_TARGET])(dev, 1, &rt1)) && rt1)
        goto out;
    /* The shadow map is R32F (the depth as colour): drawn and compared in
     * that format, as depths. Anything else as 8-bit colour. */
    if (SUCCEEDED(((pf_ptr_fn)PF_VT(rt)[PF_SURF_GET_DESC])(rt, &desc)) &&
        desc.format == PF_D3DFMT_R32F) {
        fmt = PF_D3DFMT_R32F;
        clear_color = 0xFFFFFFFFu; /* red 1.0: the far depth, as the game clears it */
        tolerance = PF_DEPTH_TOLERANCE;
        unit = "texels";
        scale = "/2^24 of the depth range";
    } else if (g.shadow && g_formats_logged < 4) {
        g_formats_logged++;
        fu_log_event("inst: verify: the shadow map's format is %u, not R32F: compared as 8-bit "
                     "colour",
                     desc.format);
    }
    if (FAILED(((pf_ptr_fn)vt[PF_DEV_GET_DEPTH_STENCIL])(dev, &ds)))
        ds = 0;
    if (FAILED(((pf_ptr_fn)vt[PF_DEV_GET_VIEWPORT])(dev, &vp)) ||
        FAILED(((pf_ptr_fn)vt[PF_DEV_GET_SCISSOR])(dev, &sc)))
        goto out;
    if (FAILED(create_rt(dev, VERIFY_SIZE, VERIFY_SIZE, fmt, 0, 0, FALSE, &a, 0)) ||
        FAILED(create_rt(dev, VERIFY_SIZE, VERIFY_SIZE, fmt, 0, 0, FALSE, &b, 0)) ||
        FAILED(create_ds(dev, VERIFY_SIZE, VERIFY_SIZE, PF_D3DFMT_D24S8, 0, 0, FALSE, &d, 0)) ||
        FAILED(create_plain(dev, VERIFY_SIZE, VERIFY_SIZE, fmt, PF_D3DPOOL_SYSTEMMEM, &sa, 0)) ||
        FAILED(create_plain(dev, VERIFY_SIZE, VERIFY_SIZE, fmt, PF_D3DPOOL_SYSTEMMEM, &sb, 0)))
        goto out;

    set_rt(dev, 0, a);
    set_ds(dev, d);
    ((pf_ptr_fn)vt[PF_DEV_SET_SCISSOR])(dev, &full);
    clear(dev, 0, 0, PF_D3DCLEAR_ALL, clear_color, PF_FLOAT_ONE, 0);
    for (i = 0; i < n; i++) {
        (*refs)++; /* the callee releases its by-value argument */
        g.render(g.verify_mesh[i], g.method, 1);
    }
    set_rt(dev, 0, b);
    set_ds(dev, d);
    ((pf_ptr_fn)vt[PF_DEV_SET_SCISSOR])(dev, &full);
    clear(dev, 0, 0, PF_D3DCLEAR_ALL, clear_color, PF_FLOAT_ONE, 0);
    g.count = n;
    draw_batch(0);
    set_rt(dev, 0, rt);
    set_ds(dev, ds);
    ((pf_ptr_fn)vt[PF_DEV_SET_VIEWPORT])(dev, &vp);
    ((pf_ptr_fn)vt[PF_DEV_SET_SCISSOR])(dev, &sc);
    if (FAILED(get_data(dev, a, sa)) || FAILED(get_data(dev, b, sb)))
        goto out;
    compare(sa, sb, fmt == PF_D3DFMT_R32F, &covered, &bad, &maxd);
    add(REASON(verify_samples), 1);
    if (!covered) {
        g_cat_empty[c]++;
        if (g.bonelist && g_bl_samples)
            g_bl_samples--; /* an empty one proves nothing */
        goto out;
    }
    failed = bad * 50 > covered;
    if (failed)
        add(REASON(verify_bad), 1);
    if (g.bonelist) {
        /* Judged on their own: a fault here sends only these back to the game. */
        if (failed)
            g_bl_bad[c]++;
        else
            g_bl_ok[c]++;
        if (failed || g_bl_ok[c] <= VERIFY_LOGGED)
            fu_log_event("inst: verify %s (game shader %u%s), bone list of %u for %u joint(s): "
                         "%u meshes, %u %s drawn, %u differ by more than %u%s, largest "
                         "difference %u: %s",
                         v->name, v->ea_index, pass, g.nbones, g.joints, n, covered, unit, bad,
                         tolerance, scale, maxd, failed ? "BAD" : "ok");
        if (failed && g_bl_bad[c] >= VERIFY_BAD_LIMIT && !g_bl_off[c]) {
            g_bl_off[c] = 1;
            fu_log_event("inst: %s (game shader %u%s): meshes with a bone list unlike their "
                         "joints switched off after %u bad samples",
                         v->name, v->ea_index, pass, g_bl_bad[c]);
        }
        goto out;
    }
    if (failed)
        g_cat_bad[c]++;
    else
        g_cat_ok[c]++;
    if (failed || g_cat_ok[c] <= VERIFY_LOGGED)
        fu_log_event("inst: verify %s (game shader %u%s): %u meshes, %u %s drawn, %u differ by "
                     "more than %u%s, largest difference %u: %s",
                     v->name, v->ea_index, pass, n, covered, unit, bad, tolerance, scale, maxd,
                     failed ? "BAD" : "ok");
    if (failed && g_cat_bad[c] >= VERIFY_BAD_LIMIT && !g_cat_off[c]) {
        g_cat_off[c] = 1;
        fu_log_event("inst: %s (game shader %u%s) switched off after %u bad samples", v->name,
                     v->ea_index, pass, g_cat_bad[c]);
    }
out:
    release(sb);
    release(sa);
    release(d);
    release(b);
    release(a);
    release(ds);
    release(rt1);
    release(rt);
}

static unsigned *refs_of(void *method)
{
    return (unsigned *)((unsigned char *)method + PF_METHOD_REFS);
}

/* mesh leads the meshes after it: the game draws it and its draw is
 * captured. 0 (g.reason set) if its model can't be instanced. */
static int lead(const unsigned char *mesh)
{
    const unsigned char *model = *(const unsigned char *const *)(mesh + PF_MESH_MODEL);
    unsigned flags = *(const unsigned *)(model + PF_MODEL_FLAGS);
    const unsigned char *geom = *(const unsigned char *const *)(model + PF_MODEL_GEOMETRY);

    g.reason = REASON(mismatch);
    g.mm = MM_GEOMETRY;
    g.model = model;
    g.base_vertex = *(const unsigned *)(mesh + PF_MESH_BASE_VERTEX);
    g.skinned = (flags & PF_MODEL_SKIN) != 0;
    /* Its first byte set: the geometry doesn't bind its own vertex buffer
     * (and skips the bones), so meshes of one model may not share it. */
    if (!geom || geom[0])
        return 0;
    g.bonelist = 0;
    if (g.skinned) {
        /* What the game's skeleton source uploads: WorldBones[i] = pivot
         * bonemap[i] for every entry of the list (NumJoints is the list's
         * length); +0x20 is NumJointsPerVertex. */
        const short *b = *(const short *const *)(geom + PF_GEOM_BONES_BEGIN);
        const short *e = *(const short *const *)(geom + PF_GEOM_BONES_END);
        int joints = *(const int *)(geom + PF_GEOM_JOINTS);
        if (!b || e <= b) {
            g.mm = MM_BONES_NONE;
            return 0;
        }
        if (joints < 1 || joints > 2) {
            g.mm = MM_JOINTS_BAD;
            if (g_bone_logs < 8) {
                g_bone_logs++;
                fu_log_event("inst: skinned geometry %08X: bone list of %d, %d joints per vertex",
                             (unsigned)(UINT_PTR)geom, (int)(e - b), joints);
            }
            return 0;
        }
        if (e - b > MAX_REGS / 2) {
            g.reason = REASON(bones);
            return 0;
        }
        g.bonemap = b;
        g.nbones = (unsigned)(e - b);
        g.stride = 2 * g.nbones;
        g.joints = (unsigned)joints;
        g.bonelist = g.nbones != g.joints;
    } else {
        g.mm = MM_FACING;
        if (flags & PF_MODEL_FACING)
            return 0;
        g.nbones = 0;
        g.stride = 3;
    }
    /* Previous subgroups were drawn and restored by member(); new groups
     * reset their counts in begin_group(). No cache entry is live here. */
    g.decl = 0;
    g.mode = M_LEADER;
    g.armed = 1;
    return 1;
}

static void begin_group(const unsigned char *mesh, void *method, unsigned count,
                        const unsigned char *frame, const unsigned char *first, void *next)
{
    unsigned n = (unsigned)(g.end - first) / PF_RECORD_SIZE;
    int passes = *(const int *)(frame + PF_FRAME_PASS_COUNT);
    int game_max = *(const int *)(frame + PF_FRAME_MAX_INSTANCES);
    const unsigned char *model = *(const unsigned char *const *)(mesh + PF_MESH_MODEL);
    DWORD now = GetTickCount();

    if (!g_started) {
        g_started = 1;
        g_verify_next = now + VERIFY_FIRST_MS;
        g_summary_next = now + SUMMARY_MS;
    }
    if ((int)(now - g_summary_next) >= 0) {
        g_summary_next = now + SUMMARY_MS;
        summary();
    }
    if (!g_extra)
        verify_tick(now);

    g.mode = M_STOCK;
    g.render = (render_fn)next;
    g.method = method;
    g.armed = 0;
    g.dips = 0;
    g.count = g.verify_n = 0;
    g.shadow = g_shadow_flag && *g_shadow_flag;
    if (g.shadow && !g_extra && first == g_prev_end && model == g_prev_model &&
        method == g_prev_method) {
        g_take.shadow_split += n;
        g_total.shadow_split += n;
    }
    g_prev_end = g.end;
    g_prev_model = model;
    g_prev_method = method;
    if (g_failed)
        g.reason = REASON(disabled);
    else if (g.shadow && !g_shadow_inst)
        g.reason = REASON(shadow_off);
    else if (n < 3)
        g.reason = REASON(small);
    else if (passes != 1)
        g.reason = REASON(multipass);
    else if (game_max > 1 || count != 1)
        g.reason = REASON(game_inst);
    else if (!model || *refs_of(method) < 2) {
        g.reason = REASON(mismatch);
        g.mm = MM_REFS;
    } else
        lead(mesh);
}

/* The leader was drawn: was it captured, and is the group supported? */
static void evaluate(void)
{
    g.armed = 0;
    if (g.dips != 1 || g.capture_reason) {
        if (g.dips != 1)
            g.mm = MM_DRAWS;
        g.reason = g.dips != 1 ? REASON(mismatch) : g.capture_reason;
        g.mode = M_STOCK;
        add(g.reason, 1); /* the leader */
        return;
    }
    add(REASON(groups), 1);
    add(REASON(leaders), 1);
    g.mode = M_INST;
    if (g_extra || !g_verify || !g_window_open || g_window_samples >= VERIFY_PER_WINDOW ||
        (g.shadow && g_window_shadow >= VERIFY_SHADOW_PER_WINDOW))
        return;
    if (g.bonelist) {
        if (g_bl_window[g.cat] < VERIFY_PER_CATEGORY)
            g.mode = M_VERIFY;
    } else if (g_cat_window[g.cat] < VERIFY_PER_CATEGORY &&
               (g_bl_samples >= VERIFY_BONELIST_TARGET ||
                g_window_samples < VERIFY_PER_WINDOW / 2)) {
        /* Until enough bone-list groups were checked, the others get at most
         * half of each window. */
        g.mode = M_VERIFY;
    }
}

static void *member(const unsigned char *mesh, const unsigned char *rec, void *method, void *next)
{
    int same = *(const unsigned char *const *)(mesh + PF_MESH_MODEL) == g.model &&
               *(const unsigned *)(mesh + PF_MESH_BASE_VERTEX) == g.base_vertex &&
               method == g.method && *refs_of(method) >= 2;

    if (g_failed) {
        g.count = 0;
        g.mode = M_STOCK;
        g.reason = REASON(disabled);
        add(g.reason, 1);
        return next;
    }
    if (g.mode == M_VERIFY) {
        if (same && g.verify_n < g.cap && stage(mesh))
            g.verify_mesh[g.verify_n++] = mesh;
        add(REASON(verify), 1);
        return next;
    }
    if (same && stage(mesh)) {
        (*refs_of(method))--; /* what the skipped callee would have released */
        if (g.count == g.cap)
            draw_batch(1);
        return pf_skip_thunk(8);
    }
    /* Another model part-way through: its draw rebinds the buffers. With at
     * least three meshes left it leads the rest, like a group's first. */
    draw_batch(1);
    if ((unsigned)(g.end - rec) / PF_RECORD_SIZE >= 3 && method == g.method &&
        *refs_of(method) >= 2 && *(const void *const *)(mesh + PF_MESH_MODEL)) {
        g.dips = 0;
        g.count = g.verify_n = 0;
        if (lead(mesh)) {
            g_splits++;
            return next;
        }
        g.mode = M_STOCK;
        add(g.reason, 1);
        return next;
    }
    g.mode = M_STOCK;
    g.reason = REASON(mismatch);
    g.mm = MM_MODEL;
    add(g.reason, 1);
    return next;
}

static void *mesh_call(void **slot, void *next)
{
    const unsigned char *mesh = (const unsigned char *)slot[-4];
    const unsigned char *frame = (const unsigned char *)slot[-8];
    const unsigned char *rec = (const unsigned char *)slot[-10];
    const unsigned char *first = (const unsigned char *)slot[-6];
    const unsigned char *end = *(const unsigned char *const *)(frame + PF_FRAME_GROUP_END);
    void *method = slot[1];
    void *ret = next;

    if (rec == first || g.mode == M_IDLE || g.end != end) {
        if (g.mode == M_INST && g.count) {
            /* The previous group ended without its last mesh: its state is gone. */
            fu_log_event("inst: %u staged meshes dropped (a group ended early)", g.count);
            g.count = 0;
        }
        g.end = end;
        begin_group(mesh, method, (unsigned)(UINT_PTR)slot[2], frame, first, next);
        if (g.mode != M_LEADER)
            add(g.reason, 1);
    } else if (g.mode == M_LEADER) {
        evaluate();
        if (g.mode == M_STOCK)
            add(g.reason, 1);
        else
            ret = member(mesh, rec, method, next);
    } else if (g.mode == M_INST || g.mode == M_VERIFY) {
        ret = member(mesh, rec, method, next);
    } else {
        add(g.reason, 1);
    }
    if (rec + PF_RECORD_SIZE == end) {
        if (g.mode == M_INST)
            draw_batch(1);
        else if (g.mode == M_VERIFY)
            verify_sample();
        g.mode = M_IDLE;
        g.armed = 0;
    }
    return ret;
}

void pf_inst_init(int verify)
{
    LARGE_INTEGER f;
    g_verify = verify != 0;
    g.mode = M_IDLE;
    QueryPerformanceFrequency(&f);
    g_qpf = f.QuadPart;
    if (verify == 2) {
        g_started = 1;
        g_window_open = 0;
        g_verify_next = g_summary_next = GetTickCount();
    }
}

void pf_inst_set_sites(unsigned mesh_site, unsigned dip_site)
{
    g_mesh_site = mesh_site;
    g_dip_site = dip_site;
}

void pf_inst_set_shadow(const volatile unsigned char *flag, int instancing)
{
    g_shadow_flag = flag;
    g_shadow_inst = instancing != 0;
}

void *pf_inst_enter(unsigned k, void **slot, void *next)
{
    DWORD tid;
    /* Every indexed draw of the game comes here: leave at once unless a
     * leader is being drawn. */
    if (k == g_dip_site && !g.armed)
        return next;
    tid = GetCurrentThreadId();
    if (!g_thread)
        g_thread = tid;
    if (tid != g_thread)
        return next;
    if (k == g_dip_site) {
        capture(slot);
        return next;
    }
    if (k == g_mesh_site)
        return mesh_call(slot, next);
    return next;
}

void pf_inst_take(pf_inst_counts *c)
{
    *c = g_take;
    c->shadow_inst_us = (unsigned)ticks_us(g_shadow_ticks);
    g_shadow_ticks_total += g_shadow_ticks;
    g_shadow_ticks = 0;
    g_take = (pf_inst_counts){ 0 };
}

void pf_inst_totals(pf_inst_counts *c)
{
    *c = g_total;
    c->shadow_inst_us = (unsigned)ticks_us(g_shadow_ticks_total + g_shadow_ticks);
}

int pf_inst_active(void)
{
    return !g_failed;
}

unsigned pf_inst_variant_count(void)
{
    return PF_INST_VARIANTS;
}
