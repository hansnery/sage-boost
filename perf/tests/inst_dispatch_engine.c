/* Offline optimistic prototype. This translation unit is NEVER linked into
 * the ASI. Compile with the production flags so staging has its real cost. */
#ifndef PF_BENCH_INST_SOURCE
#define PF_BENCH_INST_SOURCE "../src/inst.c"
#endif
#include PF_BENCH_INST_SOURCE

/* Exclusive offline handoff after all fixture draws have completed. This
 * must not be used to transfer live native renderer state. */
int bench_inst_transfer_owner(DWORD previous)
{
    if(g_thread!=previous || g.mode!=M_IDLE || g.armed || g.count || g_bulk_context)return 0;
    g_thread=GetCurrentThreadId();
    return 1;
}

/* Offline reset boundary only, after the queue and GPU have drained. This
 * explicitly releases the fixture caches; it is not a live reset hook. */
int bench_inst_release_resources(void)
{
    if(g_thread!=GetCurrentThreadId() || g.mode!=M_IDLE || g.armed ||
       g.count || g_bulk_context)return 0;
    release_stream_owner();
    pf_decl_cache_clear(&g_decl);
    release(g_ivb);g_ivb=0;
    for(unsigned i=0;i<PF_INST_VARIANTS;i++) {
        release(g_ours[i]);g_ours[i]=0;g_ours_failed[i]=0;
    }
    for(unsigned i=0;i<g_vs_n;i++){release(g_vs[i].vs);g_vs[i].vs=0;}
    g_vs_n=0;g_dev=0;
    return 1;
}

/* After the regular path accepted the second member. Remaining records
 * borrow the group's method: no by-value temporary was constructed. The
 * benchmark deliberately omits marker accounting to establish whether even
 * an optimistic shortcut merits implementing a runtime hook. */
unsigned bench_inst_bulk(const unsigned char *rec, const unsigned char *end,
                         void *method)
{
    unsigned n = 0;
    if (g.mode != M_INST || g_failed || end != g.end || method != g.method ||
        !method || *refs_of(method) < 1)
        return 0;
    while (rec < end && !g_failed) {
        const unsigned char *mesh = *(const unsigned char *const *)rec;
        if (!mesh || *(const void *const *)(mesh + PF_MESH_MODEL) != g.model ||
            *(const unsigned *)(mesh + PF_MESH_BASE_VERTEX) != g.base_vertex ||
            !stage(mesh))
            break;
        n++;
        rec += PF_RECORD_SIZE;
        if (g.count == g.cap)
            draw_batch(1);
    }
    if (rec == end) {
        draw_batch(1);
        g.mode = M_IDLE;
        g.armed = 0;
        release_stream_owner();
    }
    return n;
}
