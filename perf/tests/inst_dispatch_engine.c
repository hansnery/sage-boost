/* Offline optimistic prototype. This translation unit is NEVER linked into
 * the ASI. Compile with the production flags so staging has its real cost. */
#ifndef PF_BENCH_INST_SOURCE
#define PF_BENCH_INST_SOURCE "../src/inst.c"
#endif
#include PF_BENCH_INST_SOURCE

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
