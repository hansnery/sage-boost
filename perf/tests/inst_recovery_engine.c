/* Native/GPU fixture adapter: all batching and leader gates are production
 * inst.c. Link-time observers surround the shared submission object only. */
#define pf_inst_enter bench_original_inst_enter
#define bench_inst_bulk bench_legacy_inst_bulk
#ifdef PF_NATIVE_STAGE_REJECT
#define PF_BENCH_INST_SOURCE "../build/inst_recovery_base.c"
#endif
#include "inst_retained_engine.c"
#undef pf_inst_enter
#undef bench_inst_bulk

static pf_inst_bulk_sites fixture_bulk_sites;
static unsigned fixture_omit;
static void (*recovery_begin)(void), (*recovery_end)(void);

#ifdef PF_NATIVE_STAGE_REJECT
static int stage(const unsigned char *mesh)
{
    if (g.mode==M_INST && PF_NATIVE_STAGE_REJECT(mesh)) return 0;
    return unchecked_stage(mesh);
}
#endif

void bench_leader_native_sites(void *mesh,void *triangles,void *draw,
                              void *volatile *begin,void *volatile *end)
{
    fixture_bulk_sites=(pf_inst_bulk_sites){mesh,triangles,draw,begin,end};
}
void bench_leader_mode(unsigned enabled,unsigned omit)
{
    fixture_omit=omit;
    if(!pf_inst_bulk_configure(&fixture_bulk_sites,enabled)) ExitProcess(4);
    pf_inst_bulk_context(enabled);
}
unsigned bench_leader_suppressed(void) { return pf_inst_bulk_leaders(); }
/* Controlled verification state for the offline native/GPU fixture. Mode 2
 * represents a still-open window with this category's two samples spent. */
void bench_verify_window(unsigned mode)
{
    if(mode!=2)ExitProcess(4);
    g_verify=g_started=g_window_open=1;
    g_verify_close=GetTickCount()+VERIFY_WINDOW_MS;
    g_window_samples=4;g_window_shadow=2;
    for(unsigned i=0;i<PF_INST_CATS;i++)g_cat_window[i]=g_bl_window[i]=VERIFY_PER_CATEGORY;
}
int bench_leader_can_bulk(void) { return pf_inst_bulk_ready(); }
int bench_native_bulk_matches(const unsigned char *rec,const unsigned char *end,void *method)
{ return pf_inst_bulk_matches(rec,end,method); }
unsigned bench_inst_bulk(const unsigned char *rec,const unsigned char *end,void *method)
{
    pf_submit_result result;
    unsigned count=pf_inst_bulk_advance(rec,end,method,&result);
    if(!result.coherent || !result.complete) ExitProcess(4);
    return count;
}
int bench_native_bulk_cancel(void)
{
    pf_submit_result result=pf_inst_bulk_cancel();
    return result.coherent && result.complete;
}

void bench_recovery_watch(void (*begin)(void),void (*end)(void))
{ recovery_begin=begin;recovery_end=end; }
unsigned bench_recovery_operation(void) { return g_submit.operation; }
unsigned bench_recovery_open(void) { return g_submit.open; }
extern pf_submit_result __real_pf_submit_batch(pf_submit *,unsigned *,unsigned);
extern pf_submit_result __real_pf_submit_pending(pf_submit *,unsigned *,unsigned);
pf_submit_result __wrap_pf_submit_batch(pf_submit *s,unsigned *data,unsigned count)
{
    if(recovery_begin)recovery_begin();
    pf_submit_result result=__real_pf_submit_batch(s,data,count);
    if(recovery_end)recovery_end();
    return result;
}
pf_submit_result __wrap_pf_submit_pending(pf_submit *s,unsigned *data,unsigned count)
{
    if(recovery_begin)recovery_begin();
    pf_submit_result result=__real_pf_submit_pending(s,data,count);
    if(recovery_end)recovery_end();
    return result;
}

#ifndef PF_NATIVE_INST_ENTER
#define PF_NATIVE_INST_ENTER pf_inst_enter
#endif
void *PF_NATIVE_INST_ENTER(unsigned k,void **slot,void *next)
{
    unsigned before=g_bulk_leaders;
    void *to=bench_original_inst_enter(k,slot,next);
    /* Missing-leader negative control belongs to this fixture only. */
    if(fixture_omit && g_bulk_leaders!=before && g.count==1)g.count=0;
    return to;
}
