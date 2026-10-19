/* Assertions inspect production accounting state; the aggregate algorithm
 * lives in timing.c and is shared with the plugin. */
#include "../src/timing.c"
int bench_scope_bulk_begin(void) { return pf_timing_bulk_begin(); }
int bench_scope_bulk_end(unsigned n) { return pf_timing_bulk_end(n); }
int bench_scope_bulk_cancel(void) { return pf_timing_bulk_cancel(); }

/* Offline exclusive handoff only. Production currently identifies frame and
 * rendering ownership with one ID; this helper is not a runtime solution. */
int bench_scope_transfer_owner(DWORD previous)
{
    if(g_main!=previous || bulk_open || g_ssp || g_sp)return 0;
    g_main=GetCurrentThreadId();
    return 1;
}

int bench_scope_owner_idle(DWORD owner)
{
    return g_main==owner && GetCurrentThreadId()==owner && !bulk_open && !g_ssp && !g_sp;
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
        if (!pf_timing_bulk_begin() || pf_timing_bulk_begin()) return 0;
        int name=bulk_name;
        unsigned calls=name<0?0:g_names[name].calls;
        unsigned pcalls=name<0||pass<0?0:g_names[name].pass_calls[pass];
        LONGLONG ticks0=name<0?0:g_names[name].ticks;
        LONGLONG pticks0=name<0||pass<0?0:g_names[name].pass_ticks[pass];
        if (pf_timing_bulk_end(0) || !pf_timing_bulk_end(count) || pf_timing_bulk_end(count)) return 0;
        if (g_ssp || g_pass!=pass || g_stats.stale_scopes!=before.stale_scopes ||
            g_stats.scopes-before.scopes!=(extra||!detail?0:count) ||
            g_fx.markers-markers!=(extra?0:count)) return 0;
        if (pass>=0 && g_meshes[pass]-meshes!=(extra?0:count)) return 0;
        if (name>=0 && (g_names[name].calls-calls!=count || g_names[name].ticks<ticks0)) return 0;
        if (name>=0 && pass>=0 &&
            (g_names[name].pass_calls[pass]-pcalls!=count ||
             g_names[name].pass_ticks[pass]-pticks0!=g_names[name].ticks-ticks0)) return 0;
        markers=g_fx.markers; meshes=pass<0?0:g_meshes[pass]; before=g_stats;
        if (!pf_timing_bulk_begin()) return 0;
        calls=name<0?0:g_names[name].calls; ticks0=name<0?0:g_names[name].ticks;
        if (!pf_timing_bulk_cancel() || pf_timing_bulk_cancel() || g_ssp ||
            g_fx.markers!=markers || g_stats.scopes!=before.scopes ||
            g_stats.stale_scopes!=before.stale_scopes ||
            (pass>=0 && g_meshes[pass]!=meshes) ||
            (name>=0 && (g_names[name].calls!=calls || g_names[name].ticks!=ticks0))) return 0;
    }
    /* A full scope stack must not pop or count an unrelated scope. */
    bench_extra_mode(0); g_detail=1; g_pass=PF_SCOPE_SHADOW; g_ssp=PF_SCOPE_STACK;
    unsigned scopes=g_stats.scopes, markers=g_fx.markers;
    if (!pf_timing_bulk_begin() || !pf_timing_bulk_end(25) ||
        g_ssp!=PF_SCOPE_STACK || g_stats.scopes!=scopes || g_fx.markers-markers!=25) return 0;
    markers=g_fx.markers;
    if (!pf_timing_bulk_begin() || !pf_timing_bulk_cancel() ||
        g_ssp!=PF_SCOPE_STACK || g_stats.scopes!=scopes || g_fx.markers!=markers) return 0;
    g_ssp=saved_depth; g_pass=saved_pass; g_detail=saved_detail;
    return 1;
}
