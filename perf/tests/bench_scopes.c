/* Offline CPU screen for render-marker overhead. No game process, scene or
 * presentation is run. GPU fixture setup is outside all measurements. */
#define PF_DISPATCH_EMBED
#include "bench_dispatch.c"
#include "bench_guard.h"

extern void bench_extra_mode(int);
static unsigned scope_mode;
static const char *modes[]={"regular_detail1","regular_detail0","extra","unhooked_copies","label_only"};

static void scope_workload(void)
{
    for(unsigned i=0;i<GROUPS*GROUP_SIZE;i++) {
        char label[256],object[320];
        local_mbcopy((unsigned char*)label,(const unsigned char*)"Rendering mesh\tFXShader\t");
        local_mbcat((unsigned char*)label,(const unsigned char*)"BENCH_SYNTHETIC_UNIT_MODEL");
        if(scope_mode==4) continue;
        if(scope_mode==3) {
            event_begin(object,label,"MeshFXShader",0);
            event_end(object);
        } else {
            ((event_begin_fn)pf_enter_stubs[PF_EVENT_BEGIN])(object,label,"MeshFXShader",0);
            ((event_end_fn)pf_enter_stubs[PF_EVENT_END])(object);
        }
    }
}

int main(int argc,char **argv)
{
    double values[5][ROUNDS];
    REQUIRE(bench_no_game());
    setup_dispatch(argc,argv);
    pf_timing_set_next(PF_DISPLAY_DRAW,scope_workload);
    puts("CPU scopes only: 7200 markers, real plugin hooks, local CRT; no frame/FPS claim");
    puts("round,mode,markers,ms");
    for(unsigned round=0;round<ROUNDS+WARMUP;round++) {
        REQUIRE(bench_no_game());
        for(unsigned index=0;index<5;index++) {
            unsigned mode=round&1?4-index:index;
            double elapsed=0;
            scope_mode=mode;
            pf_timing_set_detail(mode!=1);
            bench_extra_mode(mode==2);
            for(unsigned repeat=0;repeat<FRAMES_PER_SAMPLE;repeat++) {
                pf_timing_stats before,after;
                pf_timing_get_stats(&before);
                long long start=ticks();
                ((void(*)(void))pf_enter_stubs[PF_DISPLAY_DRAW])();
                elapsed+=(ticks()-start)*1000.0/qpf/FRAMES_PER_SAMPLE;
                pf_timing_get_stats(&after);
                REQUIRE(after.scopes-before.scopes==(mode==0?GROUPS*GROUP_SIZE:0));
                REQUIRE(after.stale_scopes==before.stale_scopes);
            }
            bench_extra_mode(0);
            if(round>=WARMUP) {
                values[mode][round-WARMUP]=elapsed;
                printf("%u,%s,%u,%.6f\n",round-WARMUP,modes[mode],GROUPS*GROUP_SIZE,elapsed);
            }
        }
        REQUIRE(bench_no_game());
    }
    for(unsigned mode=0;mode<5;mode++) {
        qsort(values[mode],ROUNDS,sizeof(double),compare_double);
        printf("SUMMARY %s: median %.6f ms, p10 %.6f, p90 %.6f\n",modes[mode],values[mode][ROUNDS/2],values[mode][2],values[mode][18]);
    }
    pf_timing_set_detail(1);
    puts("Scope counts and no-stale-scope checks passed for all modes");
    return 0;
}
