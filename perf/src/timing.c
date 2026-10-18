#include "timing.h"

#include "catchup.h"
#include "clock.h"
#include "csvrow.h"
#include "fps.h"
#include "lights.h"
#include "patch.h"
#include "presentgap.h"
#include "reflect.h"
#include "shadowpass.h"
#include "stubs.h"

#define PF_STACK 64
#define PF_SCOPE_STACK 32
#define PF_NAMES 64
#define PF_NAME_LEN 32
#define PF_LABEL_CACHE 128

typedef struct {
    unsigned k;
    void **slot; /* where the caller's return address was */
    void *ret;
    LONGLONG t0;
    int presented; /* End_Scene with flip set */
    int extra;     /* extra draw: excludes regular present/covered time */
    int covering;  /* counted in the frame's covered time */
} wrapped_call;

typedef struct {
    void *self; /* the marker object */
    int name;
    int covering;
    int outer_pass; /* g_pass before this marker began */
    int pass;       /* the pass it ran in (PF_SCOPE_SHADOW .. _VIEWS), or -1 */
    LONGLONG t0;
} open_scope;

typedef struct {
    char text[PF_NAME_LEN];
    int column; /* PF_SCOPE_*, or -1 */
    LONGLONG ticks;
    unsigned calls;
    LONGLONG pass_ticks[3]; /* the same inside the shadow map, reflection, views passes */
    unsigned pass_calls[3];
} scope_name;

static const char *const g_pass_suffix[3] = { " [shadow]", " [reflection]", " [views]" };

static const pf_site *g_sites;
static void *g_next[PF_SITE_COUNT];
static int g_reg_slot[PF_SITE_COUNT]; /* indirect call hooks: the slot of the saved register */
static unsigned g_have;
static HANDLE g_csv = INVALID_HANDLE_VALUE, g_scopes_csv = INVALID_HANDLE_VALUE;
static DWORD g_main;
static LONGLONG g_freq;
static int g_started;
static pf_clock g_clock;
static unsigned g_next_dump = 60, g_dump_frames;
static wrapped_call g_stack[PF_STACK];
static unsigned g_sp;
static open_scope g_scopes[PF_SCOPE_STACK];
static unsigned g_ssp;
static scope_name g_names[PF_NAMES];
static unsigned g_name_count;
static const char *g_label_key[PF_LABEL_CACHE];
static int g_label_name[PF_LABEL_CACHE];
static int g_mesh_name[2] = { -2, -2 }; /* FXShader, DX8Render; -2 = not seen yet */
static unsigned g_depth[PF_SITE_COUNT];
static unsigned g_cover_depth;
static volatile LONG g_passed;
static pf_timing_stats g_stats;
static int g_detail = 1;
static pf_reflect g_reflect, g_shadow;
static int g_shadow_stale; /* the last shadow map call was skipped */
static int g_shadow_fresh; /* an extra frame drew it after the last game frame's call */
static int g_have_matrix;
static unsigned g_matrix[16]; /* the world-to-shadow matrix of the stored map */
static int g_pass = -1;       /* PF_SCOPE_SHADOW, _REFLECT or _VIEWS while one is open */
/* The open group of the FX shader batch, for the merge count. */
typedef struct {
    const void *method, *env;
    unsigned records, markers;
    int one_method, open, pass;
} fx_group;
static fx_group g_fx;
static pf_ui_state g_ui;
static int g_shadow_scrolling; /* also hold the shadow map at full rate while scrolling */
static pf_inst_enter_fn g_inst_enter;
static pf_inst_take_fn g_inst_take;

/* The open row, in QPC ticks. */
static LONGLONG g_frame, g_frame_max, g_logic, g_render, g_skel_render, g_skel_other;
static LONGLONG g_vis, g_cust, g_flush, g_present, g_covered, g_scope[PF_SCOPE_ALL], g_pump;
static unsigned g_frames, g_skel_render_calls, g_draws, g_reflect_skipped, g_shadow_skipped;
static unsigned g_shadow_held, g_fx_groups, g_fx_merge, g_fx_merge_anylight;
static unsigned g_meshes[PF_SCOPE_VIEWS + 1], g_fx_groups_pass[PF_SCOPE_VIEWS + 1];
static unsigned g_logic_row;
static int g_sp_verify; /* ShadowPassVerify on (decides its CSV columns) */

static LONGLONG now(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

static unsigned long long to_us(LONGLONG ticks)
{
    return (unsigned long long)ticks * 1000000ull / (unsigned long long)g_freq;
}

/* Both skeleton hooks nest in one another, so they share a depth. */
static unsigned nest_of(unsigned k)
{
    return k == PF_SKEL_ANIM ? PF_SKEL_HLOD : k;
}

static unsigned length(const char *s)
{
    unsigned n = 0;
    while (s[n])
        n++;
    return n;
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void write_text(HANDLE h, const char *s, unsigned n)
{
    DWORD put;
    if (h != INVALID_HANDLE_VALUE && n)
        WriteFile(h, s, n, &put, NULL);
}

/* The entry for a label's scope name (added on first sight), or -1 if full. */
static int add_name(const char *label)
{
    char text[PF_NAME_LEN];
    scope_name *n;
    unsigned i, k;

    pf_scope_name(label, text, sizeof text);
    for (k = 0; k < g_name_count && !same(g_names[k].text, text); k++)
        ;
    if (k < g_name_count)
        return (int)k;
    if (g_name_count == PF_NAMES)
        return -1;
    n = &g_names[g_name_count++];
    for (i = 0; text[i]; i++)
        n->text[i] = text[i];
    n->text[i] = 0;
    n->column = pf_scope_column(label);
    return (int)k;
}

/* Per-mesh labels live in reused buffers, so they go by kind, never by
 * pointer; every other label is a literal and is cached by its address. */
static int name_of(const char *label)
{
    unsigned h = (unsigned)((UINT_PTR)label >> 2) & (PF_LABEL_CACHE - 1);
    int k;

    if (pf_is_mesh_label(label)) {
        int kind = pf_scope_column(label) == PF_SCOPE_FX_MESH   ? 0
                   : pf_scope_column(label) == PF_SCOPE_DX8_MESH ? 1
                                                                 : -1;
        if (kind < 0)
            return add_name(label);
        if (g_mesh_name[kind] == -2)
            g_mesh_name[kind] = add_name(label);
        return g_mesh_name[kind];
    }
    if (g_label_key[h] == label)
        return g_label_name[h];
    k = add_name(label);
    g_label_key[h] = label;
    g_label_name[h] = k;
    return k;
}

static void write_row(unsigned time_s)
{
    DWORD err = GetLastError();
    LONGLONG rest = g_frame - g_covered;
    char buf[PF_CSV_ROW_CAP];
    unsigned i;
    pf_row r;

    r.time_s = time_s;
    r.frames = g_frames;
    r.frame_us = to_us(g_frame);
    r.frame_max_us = to_us(g_frame_max);
    r.logic_us = to_us(g_logic);
    r.render_us = to_us(g_render);
    r.skel_render_us = to_us(g_skel_render);
    r.skel_other_us = to_us(g_skel_other);
    r.skel_render_calls = g_skel_render_calls;
    r.to_workers = r.main_thread = 0;
    r.vis_us = to_us(g_vis);
    r.cust_us = to_us(g_cust);
    r.flush_us = to_us(g_flush);
    r.present_us = to_us(g_present);
    for (i = 0; i < PF_SCOPE_ALL; i++) {
        r.scope_us[i] = to_us(g_scope[i]);
        g_scope[i] = 0;
    }
    r.draws = g_draws;
    r.rest_us = to_us(rest > 0 ? rest : 0);
    r.reflect_skipped = g_reflect_skipped;
    r.shadow_skipped = g_shadow_skipped;
    r.shadow_meshes = g_meshes[PF_SCOPE_SHADOW];
    r.reflect_meshes = g_meshes[PF_SCOPE_REFLECT];
    r.view_meshes = g_meshes[PF_SCOPE_VIEWS];
    r.shadow_full_rate = g_shadow_held;
    r.fx_groups = g_fx_groups;
    r.fx_merge = g_fx_merge;
    r.fx_merge_anylight = g_fx_merge_anylight;
    for (i = 0; i <= PF_SCOPE_VIEWS; i++) {
        r.fx_groups_pass[i] = g_fx_groups_pass[i];
        g_fx_groups_pass[i] = 0;
    }
    pf_sp_take(&r.sp);
    pf_lt_take(&r.lt);
    {
        pf_fps_row fr;
        pf_fps_take_row(&fr);
        r.idle_us = fr.idle_us;
        r.waits = fr.waits;
        r.extra_frames = fr.extra;
        r.extra_us = fr.extra_us;
        r.extra_profile = fr.profile;
        r.extra_late = fr.late;
        r.extra_units = fr.units;
        r.wait_us = fr.wait_us;
        r.outside_us = fr.outside_us;
        r.sleeps = fr.sleeps;
        r.budget_us = fr.budget_us;
        {
            unsigned i;
            for (i = 0; i < 4; i++) r.fps_blocked[i] = fr.blocked[i];
            for (i = 0; i < 3; i++) r.fps_probes[i] = fr.probes[i];
            r.fps_learned_us = fr.learned_us;
        }
        r.extra_cost_us = fr.cost_us;
        r.extra_ref_us = fr.ref_us;
        r.extra_max_us = fr.extra_max_us;
        r.late_ms = fr.late_ms;
        r.held = fr.held;
        r.extra_shadows = fr.shadows;
        r.extra_reflections = fr.reflections;
        r.gap_us = fr.gap_us;
        r.gaps = fr.gaps;
        r.gap_max_us = fr.gap_max_us;
        r.disp_err_us = fr.disp_err_us;
        r.rate_changes = fr.rate_changes;
        r.unit_steps = fr.unit_steps;
        r.unit_uneven = fr.unit_uneven;
        r.extra_clocks = fr.clocks;
        r.clock_requests = fr.clock_requests;
        r.clocks_noted = fr.clocks_noted;
        r.clock_unseen = fr.clock_unseen;
    }
    r.pump_us = to_us(g_pump);
    g_pump = 0;
    r.logic_calls = g_logic_row;
    g_logic_row = 0;
    pf_cu_take_row(&r.cu);
    r.have = g_have | (g_sp_verify ? PF_HAVE_SPVERIFY : 0);
    pf_present_take(&r.presentation);
    if (g_inst_take) {
        g_inst_take(&r.inst);
        r.have |= PF_HAVE_INST;
    }
    write_text(g_csv, buf, pf_format_row(&r, buf, sizeof buf));
    g_stats.rows++;
    g_frame = g_frame_max = g_logic = g_render = g_skel_render = g_skel_other = 0;
    g_vis = g_cust = g_flush = g_present = g_covered = 0;
    g_frames = g_skel_render_calls = g_draws = g_reflect_skipped = g_shadow_skipped = 0;
    g_shadow_held = g_fx_groups = g_fx_merge = g_fx_merge_anylight = 0;
    for (i = 0; i <= PF_SCOPE_VIEWS; i++)
        g_meshes[i] = 0;
    SetLastError(err);
}

static void write_scopes(unsigned time_s)
{
    DWORD err = GetLastError();
    char buf[128];
    unsigned k;

    for (k = 0; k < g_name_count; k++) {
        scope_name *n = &g_names[k];
        unsigned p;
        if (n->calls)
            write_text(g_scopes_csv, buf,
                       pf_format_scope_row(time_s, n->text, n->calls, g_dump_frames,
                                           to_us(n->ticks), buf, sizeof buf));
        /* The same label by the pass it ran in. */
        for (p = 0; p < 3; p++) {
            char label[PF_NAME_LEN + 16];
            unsigned i = 0, j;
            if (n->pass_calls[p]) {
                for (j = 0; n->text[j]; j++)
                    label[i++] = n->text[j];
                for (j = 0; g_pass_suffix[p][j]; j++)
                    label[i++] = g_pass_suffix[p][j];
                label[i] = 0;
                write_text(g_scopes_csv, buf,
                           pf_format_scope_row(time_s, label, n->pass_calls[p], g_dump_frames,
                                               to_us(n->pass_ticks[p]), buf, sizeof buf));
            }
            n->pass_ticks[p] = 0;
            n->pass_calls[p] = 0;
        }
        n->ticks = 0;
        n->calls = 0;
    }
    g_dump_frames = 0;
    pf_sp_minute();
    pf_lt_minute();
    SetLastError(err);
}

static HANDLE create(const WCHAR *path, const char *header)
{
    HANDLE h;
    if (!path)
        return INVALID_HANDLE_VALUE;
    h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    write_text(h, header, length(header));
    return h;
}

int pf_timing_open(const pf_site *sites, const WCHAR *csv_path, const WCHAR *scopes_path)
{
    LARGE_INTEGER f;
    unsigned i;

    g_sites = sites;
    /* pushal saves eax (reg 0) highest, at slot[-3], down to edi at slot[-10]. */
    for (i = 0; i < PF_SITE_COUNT; i++) {
        int reg = pf_call_reg(&sites[i]);
        g_reg_slot[i] = reg < 0 ? -5 : -3 - reg;
    }
    QueryPerformanceFrequency(&f);
    g_freq = f.QuadPart;
    pf_present_init();
    pf_reflect_init(&g_reflect, 1, g_freq / 4);
    pf_reflect_init(&g_shadow, 1, g_freq / 4);
    if (!csv_path)
        return 1;
    g_csv = create(csv_path, pf_csv_header);
    if (g_csv == INVALID_HANDLE_VALUE)
        return 0;
    g_scopes_csv = create(scopes_path, pf_scope_header);
    return 1;
}

void pf_timing_set_detail(int detail)
{
    g_detail = detail;
}

void pf_timing_set_reflect(unsigned interval)
{
    pf_reflect_init(&g_reflect, interval, g_freq / 4);
}

void pf_timing_set_shadow(unsigned interval)
{
    pf_reflect_init(&g_shadow, interval, g_freq / 4);
}

void pf_timing_set_ui(const pf_ui_state *ui, int while_scrolling)
{
    g_ui = *ui;
    g_shadow_scrolling = while_scrolling;
}

void pf_timing_set_inst(pf_inst_enter_fn enter, pf_inst_take_fn take)
{
    g_inst_enter = enter;
    g_inst_take = take;
}

void pf_timing_set_next(unsigned k, void *trampoline)
{
    if (k < PF_SITE_COUNT)
        g_next[k] = trampoline;
}

static int all(pf_site_mask mask, pf_site_mask bits)
{
    return (mask & bits) == bits;
}

void pf_timing_set_installed(pf_site_mask mask)
{
    g_have = (all(mask, PF_BIT(PF_LOGIC)) ? PF_HAVE_LOGIC : 0) |
             (all(mask, PF_BIT(PF_RENDER)) ? PF_HAVE_RENDER : 0) |
             (all(mask, PF_BIT(PF_SKEL_HLOD) | PF_BIT(PF_SKEL_ANIM)) ? PF_HAVE_SKEL : 0) |
             (all(mask, PF_BIT(PF_VIS) | PF_BIT(PF_CUST) | PF_BIT(PF_FLUSH)) ? PF_HAVE_SCENE : 0) |
             (all(mask, PF_BIT(PF_END_SCENE)) ? PF_HAVE_PRESENT : 0) |
             (all(mask, PF_BIT(PF_EVENT_BEGIN) | PF_BIT(PF_EVENT_END)) ? PF_HAVE_SCOPES : 0) |
             (all(mask, PF_BIT(PF_DRAW)) ? PF_HAVE_DRAWS : 0) |
             (all(mask, PF_BIT(PF_REFLECT)) ? PF_HAVE_REFLECT : 0) |
             (all(mask, PF_BIT(PF_SHADOW) | PF_BIT(PF_SHADOW_INFO)) ? PF_HAVE_SHADOW : 0) |
             (all(mask, PF_BIT(PF_FX_GROUP) | PF_BIT(PF_EVENT_BEGIN) | PF_BIT(PF_EVENT_END))
                  ? PF_HAVE_BATCH
                  : 0) |
             (all(mask, PF_BIT(PF_FPS_WAIT)) ? PF_HAVE_FPS : 0) |
             (all(mask, PF_BIT(PF_PUMP)) ? PF_HAVE_PUMP : 0) |
             (all(mask, PF_BIT(PF_SAME_BATCH)) ? PF_HAVE_SBATCH : 0) |
             (all(mask, PF_BIT(PF_STENCIL_OCCLUDEE) | PF_BIT(PF_STENCIL_OCCLUDER) |
                            PF_BIT(PF_STENCIL_OTHER))
                  ? PF_HAVE_STENCIL
                  : 0) |
             (all(mask, PF_BIT(PF_SHADOW_PARTICLES)) ? PF_HAVE_SPART : 0) |
             (all(mask, PF_BIT(PF_ROO_GLIGHTS) | PF_BIT(PF_ROO_RESET_ND) | PF_BIT(PF_ROO_LISTS) |
                            PF_BIT(PF_ROO_ENV))
                  ? PF_HAVE_LIGHTS
                  : 0) |
             (all(mask, PF_BIT(PF_ROO) | PF_BIT(PF_ROO_RESET) | PF_BIT(PF_ROO_ADD_STATIC) |
                            PF_BIT(PF_ROO_ADD_DYN) | PF_BIT(PF_ROO_RENDER) |
                            PF_BIT(PF_ROO_RENDER2) | PF_BIT(PF_LENV_SET) |
                            PF_BIT(PF_ROO_LISTS) | PF_BIT(PF_ROO_ENV))
                  ? PF_HAVE_LTIME
                  : 0) |
             (all(mask, PF_BIT(PF_CU_RENDER)) && pf_cu_on() ? PF_HAVE_CATCHUP : 0);
}

int pf_timing_pass(void)
{
    return g_pass;
}

/* A game frame ran without its render (catch-up): the shadow map and the
 * reflection count it as a frame that reused them, the next render doesn't
 * take an extra frame's map from before it as fresh, and the lights the
 * skip's update walk changed are snapshotted again. */
static void render_skipped(void)
{
    pf_reflect_frame_skipped(&g_shadow);
    pf_reflect_frame_skipped(&g_reflect);
    g_shadow_fresh = 0;
    pf_fps_render_skipped();
    pf_lt_render_skipped();
}

void pf_timing_set_sp_verify(int on)
{
    g_sp_verify = on;
}

void pf_timing_discard(const WCHAR *csv_path, const WCHAR *scopes_path)
{
    if (g_csv != INVALID_HANDLE_VALUE)
        CloseHandle(g_csv);
    if (g_scopes_csv != INVALID_HANDLE_VALUE)
        CloseHandle(g_scopes_csv);
    g_csv = g_scopes_csv = INVALID_HANDLE_VALUE;
    if (csv_path)
        DeleteFileW(csv_path);
    if (scopes_path)
        DeleteFileW(scopes_path);
}

void pf_timing_flush(void)
{
    write_row(g_clock.row);
    write_scopes(g_clock.row);
}

void pf_timing_get_stats(pf_timing_stats *s)
{
    *s = g_stats;
    s->passed_through = (unsigned)g_passed;
}

static void scope_begin(void *self, const char *label)
{
    open_scope *o;
    int name, column;

    if (!label)
        return;
    if (pf_is_mesh_label(label)) {
        if (g_pass >= 0)
            g_meshes[g_pass]++;
        g_fx.markers++;
        if (!g_detail)
            return;
    }
    if (g_ssp == PF_SCOPE_STACK)
        return;
    name = name_of(label);
    column = name >= 0 ? g_names[name].column : -1;
    if (!pf_is_mesh_label(label) && column != PF_SCOPE_FX_BATCH)
        g_fx.open = 0;
    o = &g_scopes[g_ssp++];
    o->self = self;
    o->name = name;
    o->covering = PF_SCOPE_IS_FRAME_LEVEL(column) && !g_cover_depth;
    if (o->covering)
        g_cover_depth++;
    o->outer_pass = g_pass;
    if (column >= 0 && column <= PF_SCOPE_VIEWS)
        g_pass = column;
    o->pass = g_pass;
    o->t0 = now();
}

static void scope_end(void *self)
{
    LONGLONG t, dt;
    open_scope *o;
    unsigned i;

    for (i = g_ssp; i > 0 && g_scopes[i - 1].self != self; i--)
        ;
    if (!i)
        return;
    t = now();
    while (g_ssp > i) {
        o = &g_scopes[--g_ssp];
        if (o->covering)
            g_cover_depth--;
        g_pass = o->outer_pass;
        g_stats.stale_scopes++;
    }
    o = &g_scopes[--g_ssp];
    g_pass = o->outer_pass;
    dt = t - o->t0;
    if (o->name >= 0) {
        scope_name *n = &g_names[o->name];
        n->ticks += dt;
        n->calls++;
        if (o->pass >= 0 && o->pass <= PF_SCOPE_VIEWS) {
            n->pass_ticks[o->pass] += dt;
            n->pass_calls[o->pass]++;
        }
        if (n->column >= 0)
            g_scope[n->column] += dt;
    }
    if (o->covering) {
        g_covered += dt;
        g_cover_depth--;
    }
    g_stats.scopes++;
}

/* At the FX batch's per-group setup call. The stub saved the registers:
 * slot[-4] ECX = the group's first mesh, slot[-6] EBX = its first record,
 * slot[-8] EBP (the batch's frame, [EBP+8] = the group's end), slot[-9]
 * ESI = its first rendering method. Records are 32 bytes:
 * { mesh, method count, methods[] }. */
static void fx_group_begin(void **slot)
{
    const unsigned char *mesh = (const unsigned char *)slot[-4];
    const unsigned char *rec = (const unsigned char *)slot[-6];
    const unsigned char *frame = (const unsigned char *)slot[-8];
    const unsigned char *end = *(const unsigned char *const *)(frame + 8);
    fx_group next;

    next.method = slot[-9];
    next.env = mesh ? *(const void *const *)(mesh + 0xC8) : 0;
    next.records = end > rec ? (unsigned)(end - rec) / 32 : 1;
    next.markers = 0;
    next.one_method = *(const unsigned *)(rec + 4) == 1;
    next.open = 1;
    next.pass = g_pass;
    if (g_pass >= 0 && g_pass <= PF_SCOPE_VIEWS)
        g_fx_groups_pass[g_pass]++;
    g_fx_groups++;
    g_stats.fx_groups++;
    /* Same single method as a single-pass previous group: its EndPass/End
     * and this group's Begin/BeginPass are the ones a merge would drop. */
    if (g_fx.open && g_fx.pass == next.pass && g_fx.one_method && next.one_method &&
        g_fx.method == next.method && g_fx.markers == g_fx.records) {
        g_fx_merge_anylight++;
        g_stats.fx_merge_anylight++;
        if (g_fx.env == next.env) {
            g_fx_merge++;
            g_stats.fx_merge++;
        }
    }
    g_fx = next;
}

static void probe(unsigned k, void **slot)
{
    if (k == PF_DRAW) {
        g_draws++;
        g_stats.draws++;
        pf_lt_draw();
    } else if (k == PF_EVENT_BEGIN) {
        /* slot[-4] is ECX (`this`), saved by the entry stub's pushal. */
        scope_begin(slot[-4], (const char *)slot[1]);
    } else if (k == PF_EVENT_END) {
        scope_end(slot[-4]);
    } else if (k == PF_FX_GROUP) {
        fx_group_begin(slot);
    }
}

static void **g_verify_slot; /* the shadow call's stack, for ShadowPassVerify */

/* Where a reflection or shadow map call continues: its callee, or a `ret n`
 * on a frame the schedule skips. Only the main thread's calls are skipped. */
static void *interval_gate(unsigned k, DWORD tid)
{
    int shadow = k == PF_SHADOW;
    void *skip = pf_skip_thunk(g_sites[k].arg_bytes);
    int run;

    if (tid != g_main) {
        run = PF_REFLECT_RUN;
    } else if (shadow && (pf_ui_placing(&g_ui) || (g_shadow_scrolling && pf_ui_scrolling(&g_ui)))) {
        /* The ghost building follows the cursor every frame; a stored map
         * would leave its shadow behind. */
        pf_reflect_hold(&g_shadow, now());
        g_shadow_held++;
        g_stats.shadow_held++;
        run = PF_REFLECT_RUN;
    } else if (shadow && g_shadow_fresh) {
        /* An extra frame since the last game frame drew it (a few ms ago, a
         * quarter step behind at most): reused, which leaves that much more
         * time for extra frames. Counts as a redraw for the interval. */
        pf_reflect_hold(&g_shadow, now());
        g_stats.shadow_fresh++;
        run = PF_REFLECT_SKIP;
    } else {
        run = pf_reflect_next(shadow ? &g_shadow : &g_reflect, now());
    }
    if (shadow && tid == g_main)
        g_shadow_fresh = 0;

    if (run == PF_REFLECT_SKIP && skip) {
        if (shadow) {
            g_shadow_stale = 1;
            g_shadow_skipped++;
            g_stats.shadow_skipped++;
        } else {
            g_reflect_skipped++;
            g_stats.reflect_skipped++;
        }
        return skip;
    }
    if (shadow) {
        g_shadow_stale = 0;
        g_stats.shadow_forced += run == PF_REFLECT_FORCED;
        g_stats.shadow_runs++;
        /* ShadowPassVerify draws this redraw twice itself (a fix off, then as
         * configured), then the game's call is skipped. */
        if (tid == g_main && skip && pf_sp_verify_due()) {
            pf_sp_verify(g_verify_slot, g_next[k]);
            return skip;
        }
    } else {
        g_stats.reflect_forced += run == PF_REFLECT_FORCED;
        g_stats.reflect_runs++;
    }
    return g_next[k];
}

/* At SetShadowMapInfo(0, &matrix, texture): the matrix is rebuilt from the
 * light camera every frame, so after a skipped redraw the one that matches
 * the stored map goes in its place. Copied as dwords, never as floats. */
static void keep_shadow_matrix(void **slot)
{
    unsigned *m = (unsigned *)slot[2];
    unsigned i;

    if (!m)
        return;
    if (g_shadow_stale && g_have_matrix) {
        for (i = 0; i < 16; i++)
            m[i] = g_matrix[i];
        g_stats.shadow_restored++;
    } else {
        for (i = 0; i < 16; i++)
            g_matrix[i] = m[i];
        g_have_matrix = 1;
    }
}

/* Where a hook continues: the trampoline or callee, or for `call [reg+disp32]`
 * the slot the call reads, with the register it had (saved by the stub). */
static void *next_of(unsigned k, void **slot)
{
    if (g_sites[k].call_hook != 2)
        return g_next[k];
    return *(void **)((unsigned char *)slot[g_reg_slot[k]] + (unsigned)(UINT_PTR)g_next[k]);
}

void *__attribute__((used)) pf_enter(unsigned k, void **slot)
{
    DWORD tid;
    const pf_site *s = &g_sites[k];
    unsigned nest;
    wrapped_call *c;

    /* Instancing runs in extra frames too: it draws what the frame's render
     * pass built (see the design doc); fps.c tells it when a frame is extra. */
    if (k == PF_INST_MESH || k == PF_INST_DIP) {
        void *next = next_of(k, slot);
        return g_inst_enter ? g_inst_enter(k, slot, next) : next;
    }
    tid = GetCurrentThreadId();
    /* The shadow-pass fixes, in extra frames too (their output is the game's). */
    if (k == PF_STENCIL_OCCLUDEE || k == PF_STENCIL_OCCLUDER || k == PF_STENCIL_OTHER ||
        k == PF_SHADOW_PARTICLES) {
        if (g_main && tid != g_main)
            return g_next[k];
        return pf_sp_enter(k, slot, g_next[k], pf_skip_thunk(s->arg_bytes));
    }
    if (k == PF_SCENE_UPDATE)
        return pf_fps_scene_update(g_next[k]);
    /* Catch-up: the render's start in a game frame, and the notice at every
     * presented frame (extra ones too, so it doesn't flicker). */
    if (k == PF_CU_RENDER) {
        void *to;
        if (pf_fps_in_extra())
            return g_next[k];
        to = pf_cu_render(slot, g_next[k]);
        if (to != g_next[k])
            render_skipped();
        return to;
    }
    if (k == PF_END_SCENE && *(const unsigned char *)(slot + 1))
        pf_cu_present();
    /* ShadowBatchIgnoreLight's group setup, in extra frames too; the timers'
     * count of the group follows below. */
    if (k == PF_FX_GROUP && (!g_main || tid == g_main))
        pf_sp_group(slot);
    /* The light environment's sites and renderOneObject's own wrap, in extra
     * frames too (the cuts' output is the game's). */
    if (k >= PF_LIGHT_FIRST && k <= PF_LIGHT_LAST) {
        if (g_main && tid != g_main)
            return g_next[k];
        if (k == PF_LENV_SET)
            pf_sp_light_env(slot);
        return pf_lt_enter(k, slot, g_next[k]);
    }
    if (k == PF_CUST && (!g_main || tid == g_main))
        pf_lt_pass_begin();
    nest = nest_of(k);
    if ((pf_fps_in_extra() && k != PF_END_SCENE) || k == PF_FPS_WAIT || k == PF_FPS_AVG || k == PF_HLOD_SET ||
        k == PF_HLOD_DTOR || k == PF_VIEWS || k == PF_PARTICLES || k == PF_BUTTON_DRAW ||
        k == PF_CLOCK_SET || k == PF_CLOCK_SET_LEFT) {
        void *next = next_of(k, slot), *to;
        if (k == PF_SHADOW_INFO && tid == g_main)
            keep_shadow_matrix(slot);
        to = pf_fps_enter(k, slot, next, s->arg_bytes);
        /* Behind the schedule, the wait ends when the frame reaches it. */
        if (k == PF_FPS_WAIT)
            pf_cu_wait(slot);
        /* An extra frame that redraws the shadow map (ExtraFrameShadows):
         * the matrix built next describes the new map, so it is stored, as
         * after a game frame's redraw. The interval schedule stays the game
         * frames'. */
        if (k == PF_SHADOW && to == next && tid == g_main) {
            g_shadow_stale = 0;
            g_shadow_fresh = 1;
            g_stats.shadow_extra++;
        }
        return to;
    }
    if (!g_main && (k == PF_FRAME || k == PF_REFLECT || k == PF_SHADOW || k == PF_DISPLAY_DRAW))
        g_main = tid;
    if (k == PF_REFLECT || k == PF_SHADOW) {
        g_verify_slot = slot;
        return interval_gate(k, tid);
    }
    if (k == PF_SHADOW_INFO) {
        if (tid == g_main)
            keep_shadow_matrix(slot);
        return g_next[k];
    }
    if (s->probe) {
        if (tid == g_main)
            probe(k, slot);
        return g_next[k];
    }
    if (tid != g_main || g_depth[nest] || g_sp == PF_STACK) {
        InterlockedIncrement(&g_passed);
        return g_next[k];
    }
    c = &g_stack[g_sp++];
    c->k = k;
    c->slot = slot;
    c->ret = *slot;
    *slot = pf_exit_stubs[k];
    c->presented = k == PF_END_SCENE && *(const unsigned char *)(slot + 1) != 0;
    c->extra = pf_fps_in_extra();
    c->covering = !c->extra && (k == PF_LOGIC || c->presented) && !g_cover_depth;
    if (c->covering)
        g_cover_depth++;
    g_depth[nest]++;
    c->t0 = now();
    if (k == PF_FRAME) {
        pf_fps_frame_edge(1);
        pf_cu_frame_edge(1);
    } else if (k == PF_DISPLAY_DRAW) {
        pf_fps_draw_edge(1);
        pf_cu_draw_edge(1);
    }
    if (!g_started) {
        pf_clock_start(&g_clock, c->t0, g_freq);
        g_started = 1;
    }
    return g_next[k];
}

void *__attribute__((used)) pf_exit(unsigned k, unsigned char *esp)
{
    void **want = (void **)(esp - 4 - g_sites[k].arg_bytes);
    LONGLONG t, dt;
    unsigned closed;
    wrapped_call *c;

    if (k == PF_ROO)
        return pf_lt_roo_exit(esp);
    t = now();
    while (g_sp > 1 && g_stack[g_sp - 1].slot < want) {
        c = &g_stack[--g_sp];
        g_depth[nest_of(c->k)]--;
        if (c->covering)
            g_cover_depth--;
        if (c->k == PF_DISPLAY_DRAW) {
            pf_fps_draw_edge(0);
            pf_cu_draw_edge(0);
        }
        g_stats.stale++;
    }
    c = &g_stack[--g_sp];
    dt = t - c->t0;
    g_depth[nest_of(k)]--;
    if (c->covering) {
        g_covered += dt;
        g_cover_depth--;
    }
    switch (k) {
    case PF_FRAME:
        pf_fps_frame_edge(0);
        pf_cu_frame_edge(0);
        g_frame += dt;
        if (dt > g_frame_max)
            g_frame_max = dt;
        g_frames++;
        g_dump_frames++;
        g_stats.frames++;
        if (pf_clock_tick(&g_clock, t, &closed)) {
            write_row(closed);
            if (g_clock.row >= g_next_dump) {
                write_scopes(g_next_dump - 60);
                g_next_dump = (g_clock.row / 60 + 1) * 60;
            }
        }
        break;
    case PF_LOGIC:
        g_logic += dt;
        g_stats.logic_calls++;
        g_logic_row++;
        pf_cu_logic();
        break;
    case PF_RENDER:
        g_render += dt;
        g_stats.render_calls++;
        break;
    case PF_VIS:
        g_vis += dt;
        break;
    case PF_CUST:
        g_cust += dt;
        break;
    case PF_FLUSH:
        g_flush += dt;
        break;
    case PF_PUMP:
        g_pump += dt;
        break;
    case PF_DISPLAY_DRAW:
        pf_fps_draw_edge(0);
        pf_cu_draw_edge(0);
        break;
    case PF_END_SCENE:
        if (c->presented) {
            /* Divide first so an absolute QPC uptime cannot overflow the
             * multiplication used for short duration conversion. */
            unsigned long long ticks = (unsigned long long)t;
            unsigned long long freq = (unsigned long long)g_freq;
            pf_present_hit((ticks / freq) * 1000000ull +
                           (ticks % freq) * 1000000ull / freq);
            if (!c->extra) {
                g_present += dt;
                g_stats.presents++;
            }
        }
        break;
    default:
        if (g_depth[PF_RENDER]) {
            g_skel_render += dt;
            g_skel_render_calls++;
            g_stats.skel_render_calls++;
        } else {
            g_skel_other += dt;
            g_stats.skel_other_calls++;
        }
    }
    return c->ret;
}
