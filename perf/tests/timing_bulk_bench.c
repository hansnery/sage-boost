/* Offline aggregate-accounting experiment. Never linked into the ASI.
 * Individual native debug callbacks are absent only in this fixture; a
 * runtime bridge would need to prove that before replacing their events. */
#include "../src/timing.c"

static unsigned bulk_open, bulk_depth, bulk_tracked;
static int bulk_extra, bulk_pass, bulk_name;
static char bulk_identity;

int bench_scope_bulk_begin(void)
{
    if (bulk_open || !g_main || GetCurrentThreadId()!=g_main) return 0;
    bulk_open=1;
    bulk_depth=g_ssp;
    bulk_extra=pf_fps_in_extra();
    bulk_pass=g_pass;
    bulk_name=-1;
    bulk_tracked=0;
    if (!bulk_extra) {
        scope_begin(&bulk_identity,"Rendering mesh\tFXShader\tBENCH_SYNTHETIC_UNIT_MODEL");
        bulk_tracked=g_ssp>bulk_depth;
        if (bulk_tracked) bulk_name=g_scopes[bulk_depth].name;
    }
    return 1;
}

int bench_scope_bulk_end(unsigned attempted)
{
    if (!bulk_open || !attempted || g_ssp!=bulk_depth+bulk_tracked ||
        g_pass!=bulk_pass || pf_fps_in_extra()!=bulk_extra) return 0;
    if (!bulk_extra) {
        if (bulk_tracked) scope_end(&bulk_identity);
        /* One interval measures actual aggregate work, not count times it. */
        g_fx.markers+=attempted-1;
        if (bulk_pass>=0) g_meshes[bulk_pass]+=attempted-1;
        if (bulk_tracked) {
            g_stats.scopes+=attempted-1;
            if (bulk_name>=0) {
                g_names[bulk_name].calls+=attempted-1;
                if (bulk_pass>=0 && bulk_pass<=PF_SCOPE_VIEWS)
                    g_names[bulk_name].pass_calls[bulk_pass]+=attempted-1;
            }
        }
    }
    bulk_open=0;
    return 1;
}

extern void bench_extra_mode(int);

/* Outside measured workloads. One actual interval supplies both scope and
 * per-pass duration totals. Native caller and callback proofs are separate. */
int bench_scope_bulk_check(void)
{
    int saved_pass=g_pass, saved_detail=g_detail;
    unsigned saved_depth=g_ssp;
    if (saved_depth || bulk_open) return 0;
    for (int extra=0;extra<=1;extra++) for (int detail=0;detail<=1;detail++)
    for (int pass=-1;pass<=PF_SCOPE_VIEWS;pass++) for (unsigned count=1;count<=25;count+=24) {
        unsigned markers=g_fx.markers, meshes=pass<0?0:g_meshes[pass];
        pf_timing_stats before=g_stats;
        g_pass=pass; g_detail=detail; bench_extra_mode(extra);
        if (!bench_scope_bulk_begin() || bench_scope_bulk_begin()) return 0;
        int name=bulk_name;
        unsigned calls=name<0?0:g_names[name].calls;
        unsigned pcalls=name<0||pass<0?0:g_names[name].pass_calls[pass];
        LONGLONG ticks0=name<0?0:g_names[name].ticks;
        LONGLONG pticks0=name<0||pass<0?0:g_names[name].pass_ticks[pass];
        if (bench_scope_bulk_end(0) || !bench_scope_bulk_end(count) || bench_scope_bulk_end(count)) return 0;
        if (g_ssp || g_pass!=pass || g_stats.stale_scopes!=before.stale_scopes ||
            g_stats.scopes-before.scopes!=(extra||!detail?0:count) ||
            g_fx.markers-markers!=(extra?0:count)) return 0;
        if (pass>=0 && g_meshes[pass]-meshes!=(extra?0:count)) return 0;
        if (name>=0 && (g_names[name].calls-calls!=count || g_names[name].ticks<ticks0)) return 0;
        if (name>=0 && pass>=0 &&
            (g_names[name].pass_calls[pass]-pcalls!=count ||
             g_names[name].pass_ticks[pass]-pticks0!=g_names[name].ticks-ticks0)) return 0;
    }
    /* A full scope stack must not pop or count an unrelated scope. */
    bench_extra_mode(0); g_detail=1; g_pass=PF_SCOPE_SHADOW; g_ssp=PF_SCOPE_STACK;
    unsigned scopes=g_stats.scopes, markers=g_fx.markers;
    if (!bench_scope_bulk_begin() || !bench_scope_bulk_end(25) ||
        g_ssp!=PF_SCOPE_STACK || g_stats.scopes!=scopes || g_fx.markers-markers!=25) return 0;
    g_ssp=saved_depth; g_pass=saved_pass; g_detail=saved_detail;
    return 1;
}
