/* Fault-audit accessors only. Never linked into the ASI. */
#define PF_NATIVE_INST_ENTER fault_native_enter
#ifndef PF_FAULT_ENGINE_SOURCE
#define PF_FAULT_ENGINE_SOURCE "inst_native_leader_engine.c"
#endif
#include PF_FAULT_ENGINE_SOURCE
static void *fault_device;
void bench_fault_device(void *device) { fault_device=device; }
void bench_fault_stock(unsigned enabled) { g_failed=enabled!=0; }

void *pf_inst_enter(unsigned k,void **slot,void *next)
{
    void *original=k==g_dip_site?slot[1]:NULL;
    if(original && fault_device) slot[1]=fault_device;
    void *to=fault_native_enter(k,slot,next);
    if(original) slot[1]=original;
    return to;
}

int bench_fault_batch_get(unsigned first,unsigned count)
{
    return g.mode==M_INST && !g.count && g.decl && !g_failed &&
           g.variant>=0 && (unsigned)g.variant<PF_INST_VARIANTS &&
           first==pf_inst_variants[g.variant].ir_reg && count &&
           count<=pf_inst_variants[g.variant].ir_count;
}

void bench_fault_reset(void)
{
    /* Each case is isolated; production deliberately never resets this
     * session-failed flag through pf_inst_init(). */
    g_failed=0; g.mode=M_IDLE; g.count=0; g.armed=0;
    pf_inst_init(0);
}
