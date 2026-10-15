#include "lights.h"

#include "d3d9vt.h"
#include "fps.h"
#include "log.h"
#include "shadowpass.h"
#include "sites.h"
#include "stubs.h"

typedef void(__attribute__((thiscall)) * add_fn)(void *env, void *light);
typedef void(__attribute__((thiscall)) * pru_fn)(void *env, const void *camera_tm);
typedef const unsigned *(__attribute__((thiscall)) * bsphere_fn)(void *obj);
typedef unsigned char (*spheres_fn)(const void *a, const void *b);

#define ENV_DWORDS (PF_LT_ENV_SIZE / 4)
#define PARAM_DWORDS ((PF_LT_PARAMS_TO - PF_LT_PARAMS_FROM) / 4)
#define MAX_LIGHTS 256 /* per list; more and the pass is left to the game */
#define MAX_KEPT 8
#define LIST_GUARD 4096
#define VERSION_SLOTS 1024
#define REUSE_SLOTS 2048
#define DEPTH 4
#define VERIFY_PERIOD_MS 5000u
#define VERIFY_PER_PERIOD 8u

static pf_lt_data g_d;
static void *const *g_device;
static const volatile unsigned char *g_flag;
static int g_skip, g_cull, g_reuse, g_verify, g_timing;
static int (*g_pass)(void);
static int g_skip_off, g_cull_off, g_reuse_off; /* switched off for the session */
static unsigned g_bad_cull, g_bad_reuse;
static pf_lt_counts g_take, g_total;

/* ---- the per-pass light snapshot ---- */

typedef struct {
    void *obj;
    int point;
    unsigned version; /* LightReuse: 0 = unknown, never reused */
    unsigned sphere[4];
} snap_light;

static snap_light g_scene[MAX_LIGHTS], g_dyn[MAX_LIGHTS];
static unsigned g_nscene, g_ndyn;  /* lights the game tests per object */
static int g_snap_ok;              /* every light could be snapshotted */
static unsigned g_pass_serial = 1, g_snap_serial;
static const unsigned char *g_snap_scene;
static int g_skipped_pass;         /* this pass skipped a build */
static int g_stale;                /* the device's lights were last written in such a pass */

/* LightReuse: each light's fields as last snapshotted, and a version that
 * changes whenever they do. */
typedef struct {
    void *obj;
    unsigned version;
    unsigned params[PARAM_DWORDS];
} version_slot;
static version_slot *g_versions;
static unsigned g_version_next = 1;

typedef struct {
    void *obj;
    unsigned version;
} kept_light;

typedef struct {
    int valid;
    unsigned context[3]; /* the brightness cut-off, the x87 control word, MXCSR's control bits */
    unsigned sphere[4];
    int added_in;
    unsigned n;
    kept_light kept[MAX_KEPT];
    unsigned before[ENV_DWORDS], after[ENV_DWORDS];
} reuse_slot;
static reuse_slot *g_reuse_slots;

/* ---- per object ---- */

typedef struct {
    void **slot;
    void *ret;
} wrap_entry;
static wrap_entry g_wrap[DEPTH];
static unsigned g_depth;

typedef struct {
    int bucket, count, game_lists;
    unsigned long long t0, env0, pre;
} object_state;
static object_state g_obj;

/* LightVerify: the sampled object's frame and what the plugin made of its
 * lists, compared with the game's at the render info's set. */
enum { V_CULL, V_REUSE };
static struct {
    unsigned char *ebp;
    int kind, added;
    unsigned env[ENV_DWORDS];
} g_v;
static DWORD g_verify_next;
static unsigned g_verify_left;

static LONGLONG g_qpf, g_qpc0;
static unsigned long long g_tsc0;

static unsigned long long tsc(void)
{
    unsigned lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (unsigned long long)hi << 32 | lo;
}

static LONGLONG qpc(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

static void copy_dwords(unsigned *to, const unsigned *from, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        to[i] = from[i];
}

/* The first dword that differs, or n. */
static unsigned diff_dwords(const unsigned *a, const unsigned *b, unsigned n)
{
    unsigned i;
    for (i = 0; i < n && a[i] == b[i]; i++)
        ;
    return i;
}

static int in_shadow(void)
{
    return g_flag && *g_flag;
}

static int bucket(void)
{
    int p = g_pass ? g_pass() : -1;
    return p >= 0 && p <= 2 ? p : -1;
}

static int counting(void)
{
    return g_timing && !pf_fps_in_extra() && !pf_sp_verifying();
}

static int skip_applies(void)
{
    return g_skip && !g_skip_off && in_shadow() && !pf_sp_holding(PF_SP_LIGHT);
}

static int cull_applies(void)
{
    return g_cull && !g_cull_off;
}

static int reuse_applies(void)
{
    return cull_applies() && g_reuse && !g_reuse_off && g_reuse_slots && g_versions;
}

void pf_lt_init(const pf_lt_data *d, const volatile unsigned char *shadow_flag, int skip,
                int cull, int reuse, int verify)
{
    LARGE_INTEGER f;
    g_d = *d;
    g_flag = shadow_flag;
    g_skip = skip && shadow_flag;
    g_cull = cull;
    g_reuse = reuse && cull;
    g_verify = verify < 0 ? 0 : verify > 2 ? 2 : verify;
    g_skip_off = g_cull_off = g_reuse_off = 0;
    g_bad_cull = g_bad_reuse = 0;
    g_take = g_total = (pf_lt_counts){ 0 };
    g_depth = 0;
    g_pass_serial = 1;
    g_snap_serial = 0;
    g_snap_scene = 0;
    g_stale = g_skipped_pass = 0;
    g_v.ebp = 0;
    g_verify_next = 0;
    g_verify_left = 0;
    if (g_reuse && !g_versions)
        g_versions = VirtualAlloc(NULL, VERSION_SLOTS * sizeof(version_slot),
                                  MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_reuse && !g_reuse_slots)
        g_reuse_slots = VirtualAlloc(NULL, REUSE_SLOTS * sizeof(reuse_slot),
                                     MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_reuse && (!g_versions || !g_reuse_slots)) {
        g_reuse = 0;
        fu_logf("render: LightReuse: no memory for its tables, off");
    }
    if (g_versions) {
        unsigned i;
        for (i = 0; i < VERSION_SLOTS; i++)
            g_versions[i].obj = 0;
    }
    if (g_reuse_slots) {
        unsigned i;
        for (i = 0; i < REUSE_SLOTS; i++)
            g_reuse_slots[i].valid = 0;
    }
    QueryPerformanceFrequency(&f);
    g_qpf = f.QuadPart;
    g_qpc0 = qpc();
    g_tsc0 = tsc();
}

void pf_lt_set_timing(int on, int (*pass)(void))
{
    g_timing = on;
    g_pass = pass;
}

void pf_lt_set_device(void *const *device)
{
    g_device = device;
}

void pf_lt_set_installed(int cut_sites, int pass_hook)
{
    if (!cut_sites)
        g_skip = g_cull = g_reuse = 0;
    if (!pass_hook)
        g_cull = g_reuse = 0;
}

int pf_lt_skip_on(void)
{
    return g_skip && !g_skip_off;
}

int pf_lt_cull_on(void)
{
    return cull_applies();
}

int pf_lt_reuse_on(void)
{
    return reuse_applies();
}

int pf_lt_skip_active(void)
{
    return pf_lt_skip_on();
}

void pf_lt_skip_off(void)
{
    g_skip_off = 1;
}

void pf_lt_pass_begin(void)
{
    g_pass_serial++;
    g_skipped_pass = 0;
}

/* LightReuse's builds stay: each is keyed by what Add_Light reads, never by
 * frame, so a light the walk changed gets a new version at the next
 * snapshot and every build that kept it misses. */
void pf_lt_render_skipped(void)
{
    g_pass_serial++;
    g_v.ebp = 0;
}

/* ---- snapshot ---- */

static unsigned version_of(unsigned char *obj)
{
    const unsigned *params = (const unsigned *)(obj + PF_LT_PARAMS_FROM);
    unsigned h = ((unsigned)(UINT_PTR)obj >> 3) * 2654435761u, i;

    for (i = 0; i < VERSION_SLOTS; i++) {
        version_slot *s = &g_versions[(h + i) & (VERSION_SLOTS - 1)];
        if (!s->obj) {
            s->obj = obj;
            copy_dwords(s->params, params, PARAM_DWORDS);
            return s->version = g_version_next++;
        }
        if (s->obj == obj) {
            if (diff_dwords(s->params, params, PARAM_DWORDS) != PARAM_DWORDS) {
                copy_dwords(s->params, params, PARAM_DWORDS);
                s->version = g_version_next++;
            }
            return s->version;
        }
    }
    /* Full: start over. Versions only grow, so no stored build matches an
     * older one. */
    for (i = 0; i < VERSION_SLOTS; i++)
        g_versions[i].obj = 0;
    return 0;
}

/* One of the scene's lists, as the game walks it: node+4 the next node,
 * node+0xC the light's +8 (or 0); dynamic lights only while enabled. */
static unsigned walk(unsigned char *scene, unsigned list, int dynamic, snap_light *out,
                     int want_spheres, int *ok)
{
    unsigned char *sentinel = scene + list;
    unsigned char *node = *(unsigned char *const *)(sentinel + 4);
    unsigned n = 0, guard = 0;

    while (node != sentinel) {
        unsigned v;
        unsigned char *obj;
        if (!node || ++guard > LIST_GUARD) {
            *ok = 0;
            break;
        }
        v = *(const unsigned *)(node + 0xC);
        obj = v ? (unsigned char *)(UINT_PTR)(v - 8) : 0;
        node = *(unsigned char *const *)(node + 4);
        if (!obj) {
            *ok = 0;
            continue;
        }
        if (dynamic && !obj[PF_LT_LIGHT_ENABLED])
            continue;
        if (n < MAX_LIGHTS && want_spheres && *ok) {
            void *const *vt = *(void *const *const *)obj;
            snap_light *l = &out[n];
            /* The standard getter on a light without a container: its
             * sphere only changes when its own transform or attenuation does,
             * which nothing does while a pass draws. */
            if (vt[PF_LT_BSPHERE_SLOT / 4] != g_d.bsphere ||
                *(void *const *)(obj + PF_LT_CONTAINER)) {
                *ok = 0;
            } else {
                const unsigned *s = ((bsphere_fn)g_d.bsphere)(obj);
                l->obj = obj;
                l->point = *(const int *)(obj + PF_LT_LIGHT_TYPE) == 0;
                copy_dwords(l->sphere, s, 4);
                l->version = g_versions && reuse_applies() ? version_of(obj) : 0;
            }
        } else if (n >= MAX_LIGHTS) {
            *ok = 0;
        }
        n++;
    }
    return n;
}

static void snapshot(unsigned char *scene)
{
    int ok = 1, spheres = cull_applies();
    if (g_snap_serial == g_pass_serial && g_snap_scene == scene)
        return;
    g_snap_serial = g_pass_serial;
    g_snap_scene = scene;
    g_nscene = walk(scene, PF_LT_SCENE_LIGHTS, 0, g_scene, spheres, &ok);
    g_ndyn = walk(scene, PF_LT_SCENE_DYNAMIC, 1, g_dyn, spheres, &ok);
    g_snap_ok = spheres && ok;
    if (spheres && !ok && counting())
        g_take.fallback++;
}

/* The lights the game's loops would add for this object, in their order:
 * every non-point light, and each point light its sphere test keeps. */
static unsigned decide(const unsigned *obj_sphere, snap_light **kept, unsigned *scene_kept)
{
    spheres_fn test = (spheres_fn)g_d.spheres_intersect;
    unsigned i, n = 0;
    for (i = 0; i < g_nscene; i++)
        if (!g_scene[i].point || test(obj_sphere, g_scene[i].sphere))
            kept[n++] = &g_scene[i];
    *scene_kept = n;
    for (i = 0; i < g_ndyn; i++)
        if (!g_dyn[i].point || test(obj_sphere, g_dyn[i].sphere))
            kept[n++] = &g_dyn[i];
    return n;
}

/* ---- LightReuse ---- */

/* Read only: the x87 control word and MXCSR are never written. */
static void read_context(unsigned c[3])
{
    unsigned short cw;
    unsigned mx;
    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    __asm__ __volatile__("stmxcsr %0" : "=m"(mx));
    c[0] = (unsigned)*g_d.threshold;
    c[1] = cw;
    c[2] = mx & ~0x3Fu; /* not the exception flags */
}

static reuse_slot *reuse_slot_of(void *robj)
{
    unsigned h = ((unsigned)(UINT_PTR)robj >> 4) * 2654435761u;
    return &g_reuse_slots[(h >> 20) & (REUSE_SLOTS - 1)];
}

static int same_key(const reuse_slot *s, const unsigned context[3], const unsigned *sphere,
                    int added_in, snap_light *const *kept, unsigned n, const unsigned *env)
{
    unsigned i;
    if (!s->valid || s->n != n || s->added_in != added_in ||
        diff_dwords(s->context, context, 3) != 3 || diff_dwords(s->sphere, sphere, 4) != 4)
        return 0;
    for (i = 0; i < n; i++)
        if (!kept[i]->version || s->kept[i].obj != kept[i]->obj ||
            s->kept[i].version != kept[i]->version)
            return 0;
    return diff_dwords(s->before, env, ENV_DWORDS) == ENV_DWORDS;
}

static int storable(snap_light *const *kept, unsigned n)
{
    unsigned i;
    if (n > MAX_KEPT)
        return 0;
    for (i = 0; i < n; i++)
        if (!kept[i]->version)
            return 0;
    return 1;
}

/* The environment after the lists, into env: from a matching build, or by
 * adding the kept lights (and keeping the build when it may be reused).
 * Returns 1 for a reused build. */
static int lists_result(unsigned char *robj, unsigned *env, const unsigned *sphere, int added_in,
                        snap_light *const *kept, unsigned n, int store)
{
    add_fn add = (add_fn)g_d.add_light;
    reuse_slot *s = 0;
    unsigned context[3], i;
    int keep = 0;

    if (reuse_applies() && n) {
        read_context(context);
        s = reuse_slot_of(robj);
        if (same_key(s, context, sphere, added_in, kept, n, env)) {
            copy_dwords(env, s->after, ENV_DWORDS);
            return 1;
        }
        keep = store && storable(kept, n);
        if (keep) {
            s->valid = 0;
            copy_dwords(s->before, env, ENV_DWORDS);
        }
    }
    for (i = 0; i < n; i++)
        add(env, kept[i]->obj);
    if (keep) {
        copy_dwords(s->context, context, 3);
        copy_dwords(s->sphere, sphere, 4);
        s->added_in = added_in;
        s->n = n;
        for (i = 0; i < n; i++) {
            s->kept[i].obj = kept[i]->obj;
            s->kept[i].version = kept[i]->version;
        }
        copy_dwords(s->after, env, ENV_DWORDS);
        s->valid = 1;
    }
    return 0;
}

/* ---- LightVerify ---- */

static int verify_due(void)
{
    DWORD now;
    if (!g_verify || pf_fps_in_extra() || pf_sp_verifying())
        return 0;
    if (g_verify == 2)
        return 1;
    now = GetTickCount();
    if (g_verify_left) {
        g_verify_left--;
        return 1;
    }
    if (!g_verify_next) {
        g_verify_next = now + VERIFY_PERIOD_MS;
        return 0;
    }
    if ((int)(now - g_verify_next) < 0)
        return 0;
    g_verify_next = now + VERIFY_PERIOD_MS;
    g_verify_left = VERIFY_PER_PERIOD - 1;
    return 1;
}

/* At the render info's set, after the game's own lists and
 * Pre_Render_Update: the same from the plugin's lists, compared. */
static void verify_check(void **slot)
{
    unsigned char *ebp = g_v.ebp;
    unsigned char *env = ebp + PF_LT_F_ENV;
    int local = slot[-3] == (void *)env;
    int game_added = *(const int *)(ebp + PF_LT_F_ADDED);
    int want_local = g_v.added || ebp[PF_LT_F_LIGHTMAP];
    unsigned at = ENV_DWORDS;
    int bad;

    g_v.ebp = 0;
    if (local && want_local) {
        const unsigned char *rinfo = *(unsigned char *const *)(ebp + PF_LT_F_RINFO);
        const unsigned char *camera = *(unsigned char *const *)rinfo;
        ((pru_fn)g_d.pre_render_update)(g_v.env, camera + PF_LT_CAMERA_TM);
        at = diff_dwords(g_v.env, (const unsigned *)env, ENV_DWORDS);
    }
    bad = local != want_local || game_added != g_v.added || at != ENV_DWORDS;
    g_take.verify_samples++;
    if (!bad)
        return;
    g_take.verify_bad++;
    fu_log_event("lights: verify %s: the game's light environment differs (%s, lights added %d/%d, "
                 "first differing byte %u)",
                 g_v.kind == V_REUSE ? "LightReuse" : "LightCull",
                 local != want_local ? "environment kind" : "contents", game_added, g_v.added,
                 at * 4);
    if (g_v.kind == V_REUSE && ++g_bad_reuse >= 2) {
        g_reuse_off = 1;
        fu_log_event("lights: LightReuse off for this session: two verify samples differ");
    } else if (g_v.kind == V_CULL && ++g_bad_cull >= 2) {
        g_cull_off = 1;
        fu_log_event("lights: LightCull and LightReuse off for this session: two verify samples "
                     "differ");
    }
}

/* ---- the sites ---- */

static void env_start(void)
{
    if (g_obj.count && !g_obj.env0)
        g_obj.env0 = tsc();
}

static void *on_lists(void **slot, void *next)
{
    unsigned char *ebp = (unsigned char *)slot[-8];
    unsigned char *scene = (unsigned char *)slot[-9];
    unsigned *env = (unsigned *)(ebp + PF_LT_F_ENV);
    const unsigned *sphere = (const unsigned *)(ebp + PF_LT_F_OBJ_SPHERE);
    int *added = (int *)(ebp + PF_LT_F_ADDED);
    snap_light *kept[2 * MAX_LIGHTS];
    unsigned n, scene_kept;
    int count = g_obj.count, b = g_obj.bucket;

    if (skip_applies()) {
        g_skipped_pass = 1;
        if (counting())
            g_take.skipped++;
        return g_d.lists_default;
    }
    if (!cull_applies() && !g_timing)
        return next;
    snapshot(scene);
    if (count) {
        g_take.scene_tested[b] += g_nscene;
        g_take.dyn_tested[b] += g_ndyn;
    }
    if (!cull_applies() || !g_snap_ok) {
        g_obj.game_lists = 1;
        return next;
    }
    n = decide(sphere, kept, &scene_kept);
    if (verify_due()) {
        /* The game's loops run; the plugin's result waits for theirs. */
        copy_dwords(g_v.env, env, ENV_DWORDS);
        g_v.kind = lists_result(*(unsigned char **)(ebp + 0xC), g_v.env, sphere, *added, kept, n,
                                1)
                       ? V_REUSE
                       : V_CULL;
        g_v.added = *added | (n != 0);
        g_v.ebp = ebp;
        g_obj.game_lists = 1;
        return next;
    }
    if (count) {
        g_take.scene_kept[b] += scene_kept;
        g_take.dyn_kept[b] += n - scene_kept;
    }
    if (counting())
        g_take.decided++;
    if (n) {
        int hit = lists_result(*(unsigned char **)(ebp + 0xC), env, sphere, *added, kept, n, 1);
        *added = 1;
        if (counting() && reuse_applies()) {
            if (hit)
                g_take.reuse_hits++;
            else
                g_take.reuse_misses++;
        } else if (g_timing && pf_fps_in_extra() && !pf_sp_verifying() && reuse_applies()) {
            if (hit)
                g_take.extra_hits++;
            else
                g_take.extra_misses++;
        }
    }
    return g_d.lists_after;
}

/* At Set_Light_Environment's entry. It writes the device's lights and
 * ambient (DX8Wrapper's light cache, applied at the next draw) only for an
 * environment that isn't NULL, isn't the cached one with its byte 0 set, and
 * not while the FX batches hold the lights: then it only stores the pointer,
 * and their unlock calls it again with the last one stored. */
static void on_lenv(void **slot)
{
    const unsigned char *env = (const unsigned char *)slot[1];
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    int on_stack;
    if (!env || (g_d.light_lock && *g_d.light_lock) ||
        (g_d.light_env && *g_d.light_env == env && env[0]))
        return;
    on_stack = (const void *)env >= tib->StackLimit && (const void *)env < tib->StackBase;
    if (in_shadow())
        /* An object's environment (in a mesh) from a skipped build; the
         * terrain's (on the stack) is built as the game builds it. */
        g_stale = g_skipped_pass && !on_stack;
    else
        g_stale = 0;
}

/* Does the coming draw light its vertices with the device's lights: lighting
 * on and no vertex shader, as the device says (yes when it can't say)? Without
 * the device, DX8Wrapper's cached D3DRS_LIGHTING (not 0) decides. */
static int fixed_function_lit(void)
{
    typedef HRESULT(__stdcall * get_state_fn)(void *dev, DWORD state, DWORD *value);
    typedef HRESULT(__stdcall * get_vs_fn)(void *dev, void **vs);
    void *dev = g_device ? *(void *const volatile *)g_device : 0, *vs = 0;
    DWORD lighting = 1;

    if (!dev)
        return g_d.lighting && *g_d.lighting;
    if (SUCCEEDED(((get_state_fn)PF_VT(dev)[PF_DEV_GET_RENDER_STATE])(dev, PF_D3DRS_LIGHTING,
                                                                      &lighting)) &&
        !lighting)
        return 0;
    if (FAILED(((get_vs_fn)PF_VT(dev)[PF_DEV_GET_VS])(dev, &vs)))
        return 1;
    if (!vs)
        return 1;
    ((pf_release_fn)PF_VT(vs)[PF_UNK_RELEASE])(vs);
    return 0;
}

void pf_lt_draw(void)
{
    if (g_stale && g_timing && !in_shadow() && fixed_function_lit())
        g_take.stale_draws++;
}

void *pf_lt_enter(unsigned k, void **slot, void *next)
{
    switch (k) {
    case PF_ROO:
        if (g_depth < DEPTH) {
            g_wrap[g_depth].slot = slot;
            g_wrap[g_depth].ret = *slot;
            *slot = pf_exit_stubs[PF_ROO];
            if (++g_depth == 1) {
                g_obj.bucket = bucket();
                g_obj.count = counting() && g_obj.bucket >= 0;
                g_obj.game_lists = 0;
                g_obj.env0 = g_obj.pre = 0;
                g_obj.t0 = g_obj.count ? tsc() : 0;
            }
        }
        return next;
    case PF_ROO_RESET:
        env_start();
        return next;
    case PF_ROO_GLIGHTS:
        return skip_applies() ? g_d.glights_skip : next;
    case PF_ROO_RESET_ND:
        if (skip_applies())
            return g_d.lists;
        env_start();
        return next;
    case PF_ROO_LISTS:
        return on_lists(slot, next);
    case PF_ROO_ADD_STATIC:
        if (g_obj.count && g_obj.game_lists)
            g_take.scene_kept[g_obj.bucket]++;
        return next;
    case PF_ROO_ADD_DYN:
        if (g_obj.count && g_obj.game_lists)
            g_take.dyn_kept[g_obj.bucket]++;
        return next;
    case PF_ROO_ENV:
        if (g_v.ebp) {
            if (g_v.ebp == (unsigned char *)slot[-8])
                verify_check(slot);
            else
                g_v.ebp = 0;
        }
        if (g_obj.count && g_obj.env0) {
            g_take.env_us[g_obj.bucket] += tsc() - g_obj.env0;
            g_obj.env0 = 0;
        }
        return next;
    case PF_ROO_RENDER:
    case PF_ROO_RENDER2:
        if (g_obj.count)
            g_obj.pre = tsc();
        return next;
    case PF_LENV_SET:
        on_lenv(slot);
        return next;
    }
    return next;
}

void *pf_lt_roo_exit(unsigned char *esp)
{
    void **want = (void **)(esp - 4 - 16);
    wrap_entry *e;
    /* Calls that never returned here (none should) are dropped. */
    while (g_depth > 1 && g_wrap[g_depth - 1].slot < want)
        g_depth--;
    e = &g_wrap[--g_depth];
    if (!g_depth && g_obj.count) {
        unsigned long long t = tsc();
        int b = g_obj.bucket;
        g_take.calls[b]++;
        g_take.us[b] += t - g_obj.t0;
        g_take.pre_us[b] += (g_obj.pre ? g_obj.pre : t) - g_obj.t0;
        g_obj.count = 0;
    }
    return e->ret;
}

/* ---- rows ---- */

static void add_counts(pf_lt_counts *t, const pf_lt_counts *c)
{
    unsigned p;
    for (p = 0; p < 3; p++) {
        t->calls[p] += c->calls[p];
        t->us[p] += c->us[p];
        t->pre_us[p] += c->pre_us[p];
        t->env_us[p] += c->env_us[p];
        t->scene_tested[p] += c->scene_tested[p];
        t->scene_kept[p] += c->scene_kept[p];
        t->dyn_tested[p] += c->dyn_tested[p];
        t->dyn_kept[p] += c->dyn_kept[p];
    }
    t->skipped += c->skipped;
    t->decided += c->decided;
    t->reuse_hits += c->reuse_hits;
    t->reuse_misses += c->reuse_misses;
    t->fallback += c->fallback;
    t->verify_samples += c->verify_samples;
    t->verify_bad += c->verify_bad;
    t->stale_draws += c->stale_draws;
    t->extra_hits += c->extra_hits;
    t->extra_misses += c->extra_misses;
}

/* The row's processor ticks per millisecond, from QueryPerformanceCounter
 * over the same span. */
static unsigned long long ticks_to_us(unsigned long long ticks, unsigned long long per_ms)
{
    return per_ms ? ticks * 1000ull / per_ms : 0;
}

void pf_lt_take(pf_lt_counts *c)
{
    LONGLONG q = qpc();
    unsigned long long t = tsc(), per_ms = 0;
    unsigned p;
    if (q > g_qpc0 && g_qpf)
        per_ms = (t - g_tsc0) * (unsigned long long)g_qpf /
                 ((unsigned long long)(q - g_qpc0) * 1000ull);
    *c = g_take;
    for (p = 0; p < 3; p++) {
        c->us[p] = ticks_to_us(c->us[p], per_ms);
        c->pre_us[p] = ticks_to_us(c->pre_us[p], per_ms);
        c->env_us[p] = ticks_to_us(c->env_us[p], per_ms);
    }
    g_qpc0 = q;
    g_tsc0 = t;
    add_counts(&g_total, c);
    g_take = (pf_lt_counts){ 0 };
}

void pf_lt_totals(pf_lt_counts *c)
{
    *c = g_total;
}

void pf_lt_minute(void)
{
    static const char *const name[3] = { "shadow", "reflection", "views" };
    const pf_lt_counts *t = &g_total;
    unsigned p;
    if (!g_timing && !g_skip && !g_cull)
        return;
    fu_log_event("lights: totals: shadow-pass builds skipped %u, objects decided %u, reuse %u hit "
                 "%u missed (extra frames %u hit %u missed), passes left to the game %u, verify "
                 "samples %u bad %u, stale lit draws %u%s",
                 t->skipped, t->decided, t->reuse_hits, t->reuse_misses, t->extra_hits,
                 t->extra_misses, t->fallback, t->verify_samples, t->verify_bad, t->stale_draws,
                 g_skip_off || g_cull_off || g_reuse_off ? " (a cut is off for this session)" : "");
    for (p = 0; p < 3; p++) {
        unsigned n = t->calls[p];
        if (!n)
            continue;
        fu_log_event("lights:   %s: %u objects, %u ns each (%u ns before Render, %u ns light "
                     "build); per object %u.%02u scene lights tested, %u.%02u kept, %u.%02u "
                     "dynamic tested, %u.%02u kept",
                     name[p], n, (unsigned)(t->us[p] * 1000 / n),
                     (unsigned)(t->pre_us[p] * 1000 / n), (unsigned)(t->env_us[p] * 1000 / n),
                     t->scene_tested[p] / n, t->scene_tested[p] * 100 / n % 100,
                     t->scene_kept[p] / n, t->scene_kept[p] * 100 / n % 100,
                     t->dyn_tested[p] / n, t->dyn_tested[p] * 100 / n % 100, t->dyn_kept[p] / n,
                     t->dyn_kept[p] * 100 / n % 100);
    }
}
