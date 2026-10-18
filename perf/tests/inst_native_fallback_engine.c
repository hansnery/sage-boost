/* Controlled stage refusal, not a production feature. Exercise the real
 * native bridge with the checked submission engine and a device proxy. */
static int fallback_reject(const unsigned char *mesh);
#define PF_NATIVE_STAGE_REJECT fallback_reject
#define bench_native_bulk_cancel fallback_real_cancel
#include "inst_recovery_fault_engine.c"
#undef bench_native_bulk_cancel
static const void *fallback_mesh;
static unsigned fallback_rejections,fallback_cancels,fallback_pending,fallback_poison;
static int fallback_reject(const unsigned char *mesh)
{
    if (mesh!=fallback_mesh) return 0;
    if (fallback_poison) {
        /* Model a gather that writes part of its next slot before refusing.
         * It does not increment the number of valid staged units. */
        for(unsigned i=0;i<5;i++)g_stage[g.count*g.stride*4+i]=0xcafebabe;
    }
    fallback_rejections++;return 1;
}
void bench_fallback_config(const void *mesh,unsigned poison)
{ fallback_mesh=mesh;fallback_poison=poison;fallback_rejections=fallback_cancels=fallback_pending=0; }
unsigned bench_fallback_capacity(void) {return g.cap;}
void bench_fallback_counts(unsigned *rejects,unsigned *cancels,unsigned *pending)
{ *rejects=fallback_rejections;*cancels=fallback_cancels;*pending=fallback_pending; }
int bench_native_bulk_cancel(void)
{
    fallback_cancels++;fallback_pending+=g.count;
    return fallback_real_cancel();
}
