/* Offline native-mesh/GPU bridge only; never linked into the ASI. The
 * enclosing group, source callbacks and geometry binding remain controlled. */
#define pf_inst_enter bench_original_inst_enter
#define bench_inst_bulk bench_original_inst_bulk
#include "inst_retained_engine.c"
#undef pf_inst_enter
#undef bench_inst_bulk

static const unsigned char *leader_mesh, *leader_geometry;
static void **leader_slot;
static const unsigned char *native_mesh, *native_triangles, *native_draw;
static void *volatile *begin_callback, *volatile *end_callback;
static unsigned leader_enabled, leader_omit, leader_suppressed;

void bench_leader_native_sites(void *mesh, void *triangles, void *draw,
                              void *volatile *begin, void *volatile *end)
{
    native_mesh=mesh; native_triangles=triangles; native_draw=draw;
    begin_callback=begin; end_callback=end;
}

void bench_leader_mode(unsigned enabled, unsigned omit)
{
    leader_enabled=enabled; leader_omit=omit; leader_suppressed=0;
    leader_mesh=leader_geometry=0; leader_slot=0;
}

unsigned bench_leader_suppressed(void) { return leader_suppressed; }

static int eligible(void)
{
    /* Preserve the original verification path for the whole open window.
     * Native group callbacks may observe a draw before the next member. */
#ifndef PF_NATIVE_LEADER_EXTRA_GATE
#define PF_NATIVE_LEADER_EXTRA_GATE 1
#endif
    return PF_NATIVE_LEADER_EXTRA_GATE && native_mesh && begin_callback && end_callback &&
           !*begin_callback && !*end_callback &&
           (g_extra || !g_verify || !g_window_open);
}

int bench_leader_can_bulk(void)
{
    return eligible() && !g_failed &&
           (g.mode==M_INST || (g.mode==M_LEADER && g.dips==1 && !g.capture_reason));
}

int bench_native_bulk_matches(const unsigned char *rec,const unsigned char *end,void *method)
{
    if(!bench_leader_can_bulk() || !rec || rec>=end || end!=g.end || method!=g.method ||
       !method || *refs_of(method)<1) return 0;
    const unsigned char *mesh=*(const void *const *)rec;
    return mesh && *(const void *const *)(mesh+PF_MESH_MODEL)==g.model &&
           *(const unsigned *)(mesh+PF_MESH_BASE_VERTEX)==g.base_vertex;
}

unsigned bench_inst_bulk(const unsigned char *rec, const unsigned char *end, void *method)
{
    if (!bench_leader_can_bulk()) return 0;
    if (g.mode==M_LEADER) evaluate();
    return bench_original_inst_bulk(rec,end,method);
}

static HRESULT WINAPI leader_skip(void *dev, unsigned type, int base,
                                  unsigned first, unsigned vertices,
                                  unsigned start, unsigned primitives)
{
    (void)dev; (void)type; (void)base; (void)first; (void)vertices;
    (void)start; (void)primitives;
    return S_OK;
}

#ifndef PF_NATIVE_INST_ENTER
#define PF_NATIVE_INST_ENTER pf_inst_enter
#endif
void *PF_NATIVE_INST_ENTER(unsigned k, void **slot, void *next)
{
#ifdef PF_NATIVE_LEADER_CAPTURE_BEGIN
    if (k==g_dip_site && g.mode==M_LEADER && g.armed && !g.dips)
        PF_NATIVE_LEADER_CAPTURE_BEGIN();
#endif
    if (k==g_mesh_site) {
        leader_slot=slot; leader_mesh=slot[-4];
        const unsigned char *model=*(const void *const *)(leader_mesh+PF_MESH_MODEL);
        leader_geometry=model?*(const void *const *)(model+PF_MODEL_GEOMETRY):0;
    }
    void *ret=bench_original_inst_enter(k,slot,next);
    if (!leader_enabled || k!=g_dip_site || !g.armed || g_failed ||
        g.mode!=M_LEADER || g.dips!=1 || g.capture_reason || g.count || g.cap<2)
        return ret;
    /* Frame identity MUST precede deeper stack reads. Return addresses
     * alone also match a recursive invocation of the very same mesh. */
    if ((UINT_PTR)slot+392!=(UINT_PTR)leader_slot ||
        slot[0]!=native_draw+0x18A || slot[22]!=native_mesh+0x92F ||
        (slot[15]!=native_triangles+0x1D && slot[15]!=pf_exit_stubs[PF_DRAW]) ||
        !eligible() || !leader_mesh || *refs_of(g.method)<2 ||
        *(const void *const *)(leader_mesh+PF_MESH_MODEL)!=g.model ||
        *(const unsigned *)(leader_mesh+PF_MESH_BASE_VERTEX)!=g.base_vertex ||
        *(const void *const *)(g.model+PF_MODEL_GEOMETRY)!=leader_geometry ||
        !leader_geometry || leader_geometry[0] ||
        *(const void *const *)(leader_mesh+0x310))
        return ret;
    if (g.skinned &&
        (*(const void *const *)(leader_geometry+PF_GEOM_BONES_BEGIN)!=g.bonemap ||
         *(const short *const *)(leader_geometry+PF_GEOM_BONES_END)!=g.bonemap+g.nbones ||
         *(const unsigned *)(leader_geometry+PF_GEOM_JOINTS)!=g.joints))
        return ret;
    if (!leader_omit && !stage(leader_mesh)) return ret;
    leader_suppressed++;
    return leader_skip;
}
