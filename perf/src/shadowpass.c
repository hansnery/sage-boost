#include "shadowpass.h"

#include "d3d9vt.h"
#include "depthcmp.h"
#include "fps.h"
#include "log.h"

typedef unsigned char(__attribute__((thiscall)) * same_fn)(void *self, void *other);
typedef void(__attribute__((thiscall)) * update_fn)(void *self, void *scene);

static const volatile unsigned char *g_flag;
static pf_sp_data g_d;
static int g_batch, g_stencil, g_particles, g_verify;
static volatile LONG g_batch_proven;
static unsigned g_off;    /* fixes switched off for the session by the verify */
static unsigned g_bypass; /* fixes held off during a verify run */
static same_fn g_same;
static int g_batch_light;            /* the group setup and Set_Light_Environment sites */
static unsigned char *g_scan_first;  /* the scan that merged a group the game would end */
static void *g_light_from, *g_light_to; /* the setup's light environment, and the game's */
static unsigned char *const *g_pmgr;
static unsigned g_site_occludee = ~0u, g_site_occluder = ~0u, g_site_other = ~0u,
                g_site_particles = ~0u;
static pf_sp_counts g_take, g_total;
static int g_vtable_warned;

/* ShadowPassVerify */
#define VERIFY_PERIOD_MS 10000u
#define VERIFY_FIRST_MS 20000u
enum { V_BATCH, V_STENCIL, V_PARTICLES, V_ALL, V_LIGHT, V_COUNT };
static const unsigned g_variant_mask[V_COUNT] = { PF_SP_BATCH, PF_SP_STENCIL, PF_SP_PARTICLES,
                                                  PF_SP_ALL, PF_SP_LIGHT };
static const char *const g_variant_name[V_COUNT] = { "ShadowBatchIgnoreLight", "NoStencilFlushes",
                                                     "ShadowSkipParticles", "all three",
                                                     "LightSkipShadow" };
static int (*g_light_active)(void);
static void (*g_light_off)(void);
typedef struct {
    unsigned samples, bad, flip;
    unsigned long long off_us, on_us;
    unsigned covered, differ, maxd; /* last sample */
} variant_stats;
static variant_stats g_vs[V_COUNT];
static unsigned g_variant_next;
static DWORD g_verify_next;
static int g_verify_started, g_in_verify, g_capture;
static void *g_capture_rt;
static LONGLONG g_qpf;

static int on(unsigned fix)
{
    return !(g_off & fix) && !(g_bypass & fix);
}

static int counting(void)
{
    return !g_in_verify && !pf_fps_in_extra();
}

void pf_sp_init(const volatile unsigned char *shadow_flag, const pf_sp_data *d, int batch,
                int stencil, int particles, int verify)
{
    LARGE_INTEGER f;
    unsigned v;
    g_flag = shadow_flag;
    g_d = *d;
    g_batch = batch && shadow_flag;
    g_stencil = shadow_flag ? (stencil < 0 ? 0 : stencil > 2 ? 2 : stencil) : 0;
    if (g_stencil == 2 && !d->gd)
        g_stencil = 1;
    g_particles = shadow_flag && d->particle_render && d->smudge && d->smudge_count && d->gd
                      ? (particles < 0 ? 0 : particles > 2 ? 2 : particles)
                      : 0;
    g_verify = shadow_flag && d->device ? verify : 0;
    QueryPerformanceFrequency(&f);
    g_qpf = f.QuadPart;
    g_off = g_bypass = 0;
    g_take = g_total = (pf_sp_counts){ 0 };
    for (v = 0; v < V_COUNT; v++)
        g_vs[v] = (variant_stats){ 0 };
    g_variant_next = 0;
    g_verify_started = g_in_verify = g_capture = 0;
    g_batch_light = 0;
    g_scan_first = 0;
    g_light_from = g_light_to = 0;
}

void pf_sp_set_batch_light(int installed)
{
    g_batch_light = installed;
}

void pf_sp_set_sites(unsigned occludee, unsigned occluder, unsigned other, unsigned particles)
{
    g_site_occludee = occludee;
    g_site_occluder = occluder;
    g_site_other = other;
    g_site_particles = particles;
}

void pf_sp_set_same_batch(void *original)
{
    g_same = (same_fn)original;
    if (!original)
        g_batch = 0;
}

void pf_sp_set_batch_proven(int proven)
{
    InterlockedExchange(&g_batch_proven, proven != 0);
}

void pf_sp_set_particle_manager(unsigned char *const *mgr)
{
    g_pmgr = mgr;
    if (!mgr)
        g_particles = 0;
}

void pf_sp_set_light_cut(int (*active)(void), void (*off)(void))
{
    g_light_active = active;
    g_light_off = off;
}

int pf_sp_holding(unsigned fix)
{
    return (g_bypass & fix) != 0;
}

int pf_sp_verifying(void)
{
    return g_in_verify;
}

int pf_sp_batch_on(void)
{
    return g_batch && g_batch_light && g_batch_proven && !(g_off & PF_SP_BATCH);
}

int pf_sp_stencil_mode(void)
{
    return g_off & PF_SP_STENCIL ? 0 : g_stencil;
}

int pf_sp_particles_mode(void)
{
    return g_off & PF_SP_PARTICLES ? 0 : g_particles;
}

static int in_shadow_pass(void)
{
    return g_flag && *g_flag;
}

/* ---- ShadowBatchIgnoreLight ---- */

static int batch_applies(void)
{
    return g_batch && g_batch_light && g_batch_proven && on(PF_SP_BATCH) && in_shadow_pass();
}

unsigned char __attribute__((thiscall)) pf_sp_same_batch(unsigned char *self, unsigned char *other)
{
    void *env;
    unsigned fog[3], i;
    unsigned char r;
    int count;

    if (!batch_applies())
        return g_same(self, other);
    r = g_same(self, other);
    count = counting();
    if (count)
        g_take.batch_tests++;
    if (r)
        return r;
    if (count)
        g_take.batch_split_game++;
    /* A group has light environments in all its meshes or in none, so the
     * setup of one whose first mesh has none has none to set. */
    if (!*(void **)(self + PF_MESH_LIGHT_ENV) != !*(void **)(other + PF_MESH_LIGHT_ENV)) {
        if (count)
            g_take.batch_split_now++;
        return 0;
    }
    /* The same test with other's light environment and fog colour taken from
     * self (no shadow-map technique reads either), then put back. */
    env = *(void **)(other + PF_MESH_LIGHT_ENV);
    for (i = 0; i < 3; i++)
        fog[i] = ((unsigned *)(other + PF_MESH_FOG_COLOR))[i];
    *(void **)(other + PF_MESH_LIGHT_ENV) = *(void **)(self + PF_MESH_LIGHT_ENV);
    for (i = 0; i < 3; i++)
        ((unsigned *)(other + PF_MESH_FOG_COLOR))[i] = ((unsigned *)(self + PF_MESH_FOG_COLOR))[i];
    r = g_same(self, other);
    *(void **)(other + PF_MESH_LIGHT_ENV) = env;
    for (i = 0; i < 3; i++)
        ((unsigned *)(other + PF_MESH_FOG_COLOR))[i] = fog[i];
    if (count && !r)
        g_take.batch_split_now++;
    if (r)
        g_scan_first = self;
    return r;
}

/* At the group setup's call (slot: the stub's pushal; ECX the first mesh,
 * EBX its record, [EBP+8] the record past the group). The FX batches run
 * holding the lights: each setup's Set_Light_Environment only stores its
 * environment, and their unlock writes the device's lights from the last one
 * stored. The game would have stored, for this run of records, the first
 * mesh's of the last group its own test makes; that is found by running the
 * test as the game's scan would, and handed to the setup's call instead. */
void pf_sp_group(void **slot)
{
    unsigned char *first = (unsigned char *)slot[-4];
    unsigned char *rec = (unsigned char *)slot[-6];
    unsigned char *frame = (unsigned char *)slot[-8];
    unsigned char *end, *start, *r;
    int merged = g_scan_first && g_scan_first == first;

    g_scan_first = 0;
    g_light_from = g_light_to = 0;
    if (!merged || !batch_applies() || !rec || !frame || *(unsigned char **)rec != first)
        return;
    end = *(unsigned char **)(frame + 8);
    if (end <= rec || (unsigned)(end - rec) % PF_SP_RECORD)
        return;
    start = first;
    for (r = rec + PF_SP_RECORD; r < end; r += PF_SP_RECORD)
        if (!g_same(start, *(unsigned char **)r))
            start = *(unsigned char **)r;
    if (*(void **)(start + PF_MESH_LIGHT_ENV) != *(void **)(first + PF_MESH_LIGHT_ENV)) {
        g_light_from = *(void **)(first + PF_MESH_LIGHT_ENV);
        g_light_to = *(void **)(start + PF_MESH_LIGHT_ENV);
    }
}

/* At Set_Light_Environment's entry (slot[1] its argument): the setup's call
 * comes first after pf_sp_group. */
void pf_sp_light_env(void **slot)
{
    if (!g_light_from)
        return;
    if (slot[1] == g_light_from) {
        slot[1] = g_light_to;
        if (counting())
            g_take.batch_light_fixed++;
    } else if (counting()) {
        g_take.batch_light_missed++;
    }
    g_light_from = g_light_to = 0;
}

/* ---- NoStencilFlushes ---- */

static int stencil_applies(void)
{
    const unsigned char *gd;
    if (!g_stencil || !on(PF_SP_STENCIL))
        return 0;
    if (in_shadow_pass())
        return 1;
    if (g_stencil < 2 || !g_d.gd || !(gd = *(const unsigned char *const volatile *)g_d.gd))
        return 0;
    return gd[PF_GD_SHADOW_MAP] && !gd[PF_GD_SHADOW_VOLUMES];
}

/* Each site sits on the instruction that keeps the template's NON_SELF bits:
 * `and ecx, 0x380` (occludees), `shr eax, 3; and eax, 0x70` (occluders) and
 * `shr ebx, 3; and ebx, 0x70` (the rest). The register is zeroed before the
 * stolen instruction runs, so the game takes its own path for an object
 * without those bits. pushal put EAX at slot[-3], ECX [-4], EBX [-6]. */
static void stencil_site(unsigned k, void **slot)
{
    int reg = k == g_site_occludee ? -4 : k == g_site_occluder ? -3 : -6;
    unsigned v = (unsigned)(UINT_PTR)slot[reg];
    unsigned bits = k == g_site_occludee ? v & 0x380u : (v >> 3) & 0x70u;
    if (!bits || !stencil_applies())
        return;
    slot[reg] = 0;
    if (counting()) {
        if (in_shadow_pass())
            g_take.stencil_shadow++;
        else
            g_take.stencil_other++;
    }
}

/* ---- ShadowSkipParticles ---- */

static void *particle_site(void *next, void *skip)
{
    unsigned char *mgr, *smudge;
    const unsigned char *gd;
    int smudges_possible;

    if (g_capture && in_shadow_pass() && !g_capture_rt) {
        void *dev = *(void *const volatile *)g_d.device;
        if (dev)
            ((pf_get_rt_fn)PF_VT(dev)[PF_DEV_GET_RENDER_TARGET])(dev, 0, &g_capture_rt);
    }
    if (!g_particles || !on(PF_SP_PARTICLES) || !in_shadow_pass() || !skip)
        return next;
    mgr = g_pmgr ? *(unsigned char *const volatile *)g_pmgr : 0;
    if (!mgr)
        return next;
    if (PF_VT(mgr)[PF_PMGR_RENDER_SLOT / 4] != (void *)g_d.particle_render) {
        if (!g_vtable_warned) {
            g_vtable_warned = 1;
            fu_log_event("shadowpass: the particle manager's render isn't the checked function: "
                         "its walk is kept");
        }
        return next;
    }
    if (!mgr[PF_PMGR_RENDER_PENDING]) {
        /* The render would return at once. */
        if (counting())
            g_take.particles_skipped++;
        return skip;
    }
    smudge = *(unsigned char *const volatile *)g_d.smudge;
    gd = *(const unsigned char *const volatile *)g_d.gd;
    smudges_possible = smudge && *(const unsigned *)(smudge + PF_SMUDGE_STATE) != 1 && gd &&
                       gd[PF_GD_SMUDGES];
    if (smudges_possible && g_particles < 2) {
        if (counting())
            g_take.particles_kept++;
        return next;
    }
    /* What the walk leaves behind when no smudge is added (its draw modules
     * draw nothing in this pass): the pending byte cleared, no smudges. */
    mgr[PF_PMGR_RENDER_PENDING] = 0;
    *g_d.smudge_count = 0;
    if (smudge)
        *(unsigned *)(smudge + PF_SMUDGE_COUNT) = 0;
    if (counting())
        g_take.particles_skipped++;
    return skip;
}

void *pf_sp_enter(unsigned k, void **slot, void *next, void *skip_thunk)
{
    if (k == g_site_particles)
        return particle_site(next, skip_thunk);
    if (k == g_site_occludee || k == g_site_occluder || k == g_site_other)
        stencil_site(k, slot);
    return next;
}

/* ---- ShadowPassVerify ---- */

static LONGLONG qpc(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

static unsigned long long ticks_us(LONGLONG t)
{
    return g_qpf ? (unsigned long long)t * 1000000ull / (unsigned long long)g_qpf : 0;
}

static unsigned enabled_mask(void)
{
    return (pf_sp_batch_on() ? PF_SP_BATCH : 0) | (pf_sp_stencil_mode() ? PF_SP_STENCIL : 0) |
           (pf_sp_particles_mode() ? PF_SP_PARTICLES : 0) |
           (g_light_active && g_light_active() ? PF_SP_LIGHT : 0);
}

int pf_sp_verify_due(void)
{
    DWORD now;
    if (!g_verify || pf_fps_in_extra() || !enabled_mask())
        return 0;
    if (g_verify == 2)
        return 1; /* tests: every redraw */
    now = GetTickCount();
    if (!g_verify_started) {
        g_verify_started = 1;
        g_verify_next = now + VERIFY_FIRST_MS;
        return 0;
    }
    return (int)(now - g_verify_next) >= 0;
}

typedef struct {
    unsigned pending, smudges, smudge_mgr_count, drawn; /* drawn: manager +0x5C's change */
} particle_state;

static void particle_snapshot(particle_state *s, unsigned drawn_before)
{
    unsigned char *mgr = g_pmgr ? *(unsigned char *const volatile *)g_pmgr : 0;
    unsigned char *smudge = g_d.smudge ? *(unsigned char *const volatile *)g_d.smudge : 0;
    s->pending = mgr ? mgr[PF_PMGR_RENDER_PENDING] : 0;
    s->drawn = mgr ? *(const unsigned *)(mgr + 0x5C) - drawn_before : 0;
    s->smudges = g_d.smudge_count ? *g_d.smudge_count : 0;
    s->smudge_mgr_count = smudge ? *(const unsigned *)(smudge + PF_SMUDGE_COUNT) : 0;
}

static unsigned drawn_now(void)
{
    unsigned char *mgr = g_pmgr ? *(unsigned char *const volatile *)g_pmgr : 0;
    return mgr ? *(const unsigned *)(mgr + 0x5C) : 0;
}

/* The captured shadow map into a new system-memory surface (NULL on failure). */
static void *read_back(void *dev, void *rt, pf_surface_desc *desc)
{
    void *sys = 0;
    if (!rt || FAILED(((HRESULT(__stdcall *)(void *, pf_surface_desc *))PF_VT(rt)[PF_SURF_GET_DESC])(
                   rt, desc)))
        return 0;
    if (FAILED(((pf_create_plain_fn)PF_VT(dev)[PF_DEV_CREATE_OFFSCREEN_PLAIN])(
            dev, desc->width, desc->height, desc->format, PF_D3DPOOL_SYSTEMMEM, &sys, 0)))
        return 0;
    if (FAILED(((pf_two_ptr_fn)PF_VT(dev)[PF_DEV_GET_RENDER_TARGET_DATA])(dev, rt, sys))) {
        ((pf_release_fn)PF_VT(sys)[PF_UNK_RELEASE])(sys);
        return 0;
    }
    return sys;
}

static void release(void *obj)
{
    if (obj)
        ((pf_release_fn)PF_VT(obj)[PF_UNK_RELEASE])(obj);
}

/* covered: texels either map drew (not the clear depth); differ: not
 * bit-identical; bad: over PF_DEPTH_TOLERANCE; maxd in 2^-24 steps. */
static int compare_maps(void *a, void *b, const pf_surface_desc *d, unsigned *covered,
                        unsigned *differ, unsigned *bad, unsigned *maxd)
{
    pf_locked_rect la, lb;
    pf_lock_rect_fn lock_a = (pf_lock_rect_fn)PF_VT(a)[PF_SURF_LOCK_RECT];
    pf_lock_rect_fn lock_b = (pf_lock_rect_fn)PF_VT(b)[PF_SURF_LOCK_RECT];
    unsigned x, y;

    *covered = *differ = *bad = *maxd = 0;
    if (FAILED(lock_a(a, &la, 0, PF_D3DLOCK_READONLY)))
        return 0;
    if (FAILED(lock_b(b, &lb, 0, PF_D3DLOCK_READONLY))) {
        ((pf_unlock_fn)PF_VT(a)[PF_SURF_UNLOCK_RECT])(a);
        return 0;
    }
    for (y = 0; y < d->height; y++) {
        const unsigned *pa = (const unsigned *)((const unsigned char *)la.bits + y * la.pitch);
        const unsigned *pb = (const unsigned *)((const unsigned char *)lb.bits + y * lb.pitch);
        for (x = 0; x < d->width; x++) {
            unsigned diff;
            if (pa[x] == PF_DEPTH_CLEAR && pb[x] == PF_DEPTH_CLEAR)
                continue;
            (*covered)++;
            if (pa[x] == pb[x])
                continue;
            (*differ)++;
            diff = pf_depth_diff(pa[x], pb[x]);
            if (diff > *maxd)
                *maxd = diff;
            if (diff > PF_DEPTH_TOLERANCE)
                (*bad)++;
        }
    }
    ((pf_unlock_fn)PF_VT(b)[PF_SURF_UNLOCK_RECT])(b);
    ((pf_unlock_fn)PF_VT(a)[PF_SURF_UNLOCK_RECT])(a);
    return 1;
}

static int next_variant(unsigned mask)
{
    unsigned i;
    for (i = 0; i < V_COUNT; i++) {
        unsigned v = (g_variant_next + i) % V_COUNT;
        unsigned want = g_variant_mask[v] & mask;
        /* "all three" only when it differs from a single fix. */
        if (want && (v != V_ALL || (want & (want - 1)))) {
            g_variant_next = v + 1;
            return (int)v;
        }
    }
    return -1;
}

void pf_sp_verify(void **slot, void *callee)
{
    void *self = slot[-4], *scene = slot[1], *dev = *(void *const volatile *)g_d.device;
    void *map[2] = { 0, 0 };
    pf_surface_desc desc[2];
    particle_state ps[2];
    LONGLONG dt[2];
    unsigned mask = enabled_mask(), covered = 0, differ = 0, bad = 0, maxd = 0, run;
    int v = next_variant(mask), off_first, state_ok, compared = 0;
    variant_stats *vs;

    g_verify_next = GetTickCount() + VERIFY_PERIOD_MS;
    if (v < 0 || !dev || !callee) {
        ((update_fn)callee)(self, scene);
        return;
    }
    vs = &g_vs[v];
    off_first = !(vs->flip++ & 1);
    g_in_verify = 1;
    for (run = 0; run < 2; run++) {
        int off = off_first ? run == 0 : run == 1;
        unsigned before = drawn_now();
        LONGLONG t0;
        g_bypass = off ? g_variant_mask[v] & mask : 0;
        g_capture = 1;
        g_capture_rt = 0;
        t0 = qpc();
        ((update_fn)callee)(self, scene);
        dt[off] = qpc() - t0;
        g_capture = 0;
        particle_snapshot(&ps[off], before);
        map[off] = read_back(dev, g_capture_rt, &desc[off]);
        release(g_capture_rt);
        g_capture_rt = 0;
    }
    g_bypass = 0;
    g_in_verify = 0;
    state_ok = ps[0].drawn == ps[1].drawn && ps[0].smudges == ps[1].smudges &&
               ps[0].smudge_mgr_count == ps[1].smudge_mgr_count && ps[0].pending == ps[1].pending;
    if (map[0] && map[1] && desc[0].format == PF_D3DFMT_R32F && desc[0].width == desc[1].width &&
        desc[0].height == desc[1].height && desc[0].format == desc[1].format)
        compared = compare_maps(map[1], map[0], &desc[0], &covered, &differ, &bad, &maxd);
    release(map[0]);
    release(map[1]);
    if (!compared) {
        fu_log_event("shadowpass: verify %s: no shadow map to compare (captured %d/%d)",
                     g_variant_name[v], map[0] != 0, map[1] != 0);
        return;
    }
    vs->samples++;
    vs->off_us += ticks_us(dt[1]);
    vs->on_us += ticks_us(dt[0]);
    vs->covered = covered;
    vs->differ = differ;
    vs->maxd = maxd;
    g_take.verify_samples++;
    if (bad || !state_ok) {
        vs->bad++;
        g_take.verify_bad++;
    }
    if (vs->samples <= 3 || bad || !state_ok)
        fu_log_event("shadowpass: verify %s (%s first): %u texels drawn, %u not bit-identical, %u "
                     "over the tolerance (largest %u/2^24); particle bookkeeping %s (drawn %u/%u, "
                     "smudges %u/%u); shadow pass %u us with it off, %u us on",
                     g_variant_name[v], off_first ? "off" : "on", covered, differ, bad, maxd,
                     state_ok ? "equal" : "DIFFERENT", ps[1].drawn, ps[0].drawn, ps[1].smudges,
                     ps[0].smudges, (unsigned)ticks_us(dt[1]), (unsigned)ticks_us(dt[0]));
    if ((bad || !state_ok) && vs->bad >= 2 && v != V_ALL) {
        g_off |= g_variant_mask[v];
        if (v == V_LIGHT && g_light_off)
            g_light_off();
        fu_log_event("shadowpass: %s off for this session: two verify samples differ",
                     g_variant_name[v]);
    }
}

/* ---- counts ---- */

static void add(pf_sp_counts *t, const pf_sp_counts *c)
{
    t->batch_tests += c->batch_tests;
    t->batch_split_game += c->batch_split_game;
    t->batch_split_now += c->batch_split_now;
    t->stencil_shadow += c->stencil_shadow;
    t->stencil_other += c->stencil_other;
    t->particles_skipped += c->particles_skipped;
    t->particles_kept += c->particles_kept;
    t->verify_samples += c->verify_samples;
    t->verify_bad += c->verify_bad;
    t->batch_light_fixed += c->batch_light_fixed;
    t->batch_light_missed += c->batch_light_missed;
}

void pf_sp_take(pf_sp_counts *c)
{
    *c = g_take;
    add(&g_total, &g_take);
    g_take = (pf_sp_counts){ 0 };
}

void pf_sp_totals(pf_sp_counts *c)
{
    *c = g_total;
    add(c, &g_take);
}

void pf_sp_minute(void)
{
    pf_sp_counts t;
    unsigned v;
    if (!g_batch && !g_stencil && !g_particles)
        return;
    pf_sp_totals(&t);
    fu_log_event("shadowpass: totals: batch tests %u, group ends %u -> %u (%u groups merged, "
                 "%u setups given the game's light environment, %u missed); objects without "
                 "stencil flushes %u shadow pass, %u other passes; particle walks skipped %u, "
                 "kept %u; verify samples %u, bad %u",
                 t.batch_tests, t.batch_split_game, t.batch_split_now,
                 t.batch_split_game - t.batch_split_now, t.batch_light_fixed,
                 t.batch_light_missed, t.stencil_shadow, t.stencil_other, t.particles_skipped,
                 t.particles_kept, t.verify_samples, t.verify_bad);
    for (v = 0; v < V_COUNT; v++) {
        const variant_stats *s = &g_vs[v];
        if (!s->samples)
            continue;
        fu_log_event("shadowpass:   verify %s: %u samples, %u bad; shadow pass %u us off, %u us "
                     "on (average); last: %u texels, %u not bit-identical, largest %u/2^24",
                     g_variant_name[v], s->samples, s->bad, (unsigned)(s->off_us / s->samples),
                     (unsigned)(s->on_us / s->samples), s->covered, s->differ, s->maxd);
    }
}
