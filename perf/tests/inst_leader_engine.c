/* Optimistic offline screen ONLY. The modeled material callee issues one
 * DIP and no state writes afterward. This is NOT established for the native
 * engine, so this adapter must never be linked into an installed plugin. */
#define pf_inst_enter bench_original_inst_enter
#define bench_inst_bulk bench_original_inst_bulk
#include "inst_retained_engine.c"
#undef pf_inst_enter
#undef bench_inst_bulk

static const unsigned char *leader_mesh;
static unsigned leader_enabled, leader_omit, leader_suppressed;

void bench_leader_mode(unsigned enabled, unsigned omit)
{
    leader_enabled = enabled;
    leader_omit = omit; /* Deliberately broken missing-leader image control. */
    leader_suppressed = 0;
    leader_mesh = NULL;
}

unsigned bench_leader_suppressed(void) { return leader_suppressed; }

unsigned bench_inst_bulk(const unsigned char *rec, const unsigned char *end,
                         void *method)
{
    /* The single-DIP fixture callee has returned. Accept the captured state
     * here so the second mesh can also use aggregate dispatch. A native
     * implementation would need a new contract for this earlier boundary. */
    if (leader_enabled && g.mode == M_LEADER)
        evaluate();
    return bench_original_inst_bulk(rec, end, method);
}

static HRESULT WINAPI leader_skip(void *dev, unsigned type, int base,
                                  unsigned first, unsigned vertices,
                                  unsigned start, unsigned primitives)
{
    (void)dev; (void)type; (void)base; (void)first; (void)vertices;
    (void)start; (void)primitives;
    return S_OK;
}

void *pf_inst_enter(unsigned k, void **slot, void *next)
{
    if (k == g_mesh_site)
        leader_mesh = slot[-4];
    void *ret = bench_original_inst_enter(k, slot, next);
    if (!leader_enabled || k != g_dip_site || !g.armed)
        return ret;
    /* Fail the fixture instead of pretending this is a production fallback:
     * a deferred draw could already have been lost or reordered by then. */
    if (g.dips != 1 || g.mode != M_LEADER || g.capture_reason ||
        g_failed || g_verify || !leader_mesh || g.count || g.cap < 2 ||
        (!leader_omit && !stage(leader_mesh)))
        ExitProcess(3);
    leader_suppressed++;
    return leader_skip;
}
