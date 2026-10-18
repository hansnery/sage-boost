/* Force unsupported staging before any follower and after partial batches.
 * Reference every case to 25 ordinary native mesh draws at distinct poses. */
static void fallback_controls(void);
#define PF_RECOVERY_CANDIDATE
#define PF_NATIVE_FX_MAIN group_gpu_main
#define PF_NATIVE_GPU_GROUP_SOURCE "bench_native_fx_gpu.c"
#define PF_FAULT_ARGUMENTS 11
#ifndef PF_FAULT_EXTRA_STATE
#define PF_FAULT_EXTRA_STATE nx_snapshot
#endif
#define PF_FAULT_STOCK_REFERENCE
#define PF_FAULT_ADDITIONAL_CONTROLS fallback_controls
#include "test_native_gpu_faults.c"
extern void bench_fallback_config(const void *,unsigned);
extern unsigned bench_fallback_capacity(void);
extern void bench_fallback_counts(unsigned *,unsigned *,unsigned *);

static void fallback_controls(void)
{
    static unsigned reference[RT*RT],out[RT*RT];
    unsigned cases=0;
    puts("fallback_extra,groups,partial_write,member,capacity,stage_refusals,cancellations,pending_replayed,instanced_units,ordinary_units,pixel_errors,state_errors");
    for(unsigned extra=0;extra<2;extra++)for(unsigned groups=1;groups<=2;groups++) {
        ff_groups=groups;bench_fallback_config(NULL,0);ff_allow_fallback=groups>1;
        ff_case(extra,0,0,0,NULL,reference);
        ff_case(extra,1,0,0,reference,out);
        unsigned capacity=bench_fallback_capacity();REQUIRE(capacity>=2 && capacity<GROUP_SIZE);
        unsigned positions[]={1,2,capacity-1,capacity,capacity+1,GROUP_SIZE-1};
        for(unsigned poison=0;poison<2;poison++)for(unsigned p=0;p<sizeof positions/sizeof positions[0];p++) {
            unsigned member=positions[p];
            bench_fallback_config(many_meshes+member,poison);ff_allow_fallback=1;
            FaultResult r=ff_case(extra,1,0,0,reference,out);
            unsigned rejects,cancels,pending;
            bench_fallback_counts(&rejects,&cancels,&pending);
            REQUIRE(rejects==(member==1?1u:2u) && cancels==1 && pending==member%capacity);
            REQUIRE(r.instances==(member/capacity)*capacity+(groups-1)*GROUP_SIZE && r.native==groups*GROUP_SIZE-r.instances);
            REQUIRE(r.suppressed==groups && r.active && !r.pixels && !ff_state_bad);
            printf("FALLBACK,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",extra,groups,poison,member,capacity,
                    rejects,cancels,pending,r.instances,r.native,r.pixels,ff_state_bad);
            cases++;
        }
    }
    ff_groups=1;ff_allow_fallback=0;bench_fallback_config(NULL,0);
    printf("FALLBACK CONTROLS: %u zero/partial cases preserve 25/50 units, following groups, native/effect/device state, references, scopes and ABI\n",cases);
}
