/* Stronger multi-extra screen: real local effects, checked keys, per-group
 * state restoration and a bulk-dispatch baseline. Never linked into ASI. */
#define PF_MATERIAL_MAIN unused_material_main
#include "bench_material_queue.c"
#define PF_MULTI_EMBED
#include "bench_retained_multi.c"

static unsigned recapture_regular(void)
{
    /* Charge every group capture each ordinary frame, including exact keys.
     * Extra frames still validate keys and fall back on changed inputs. */
    memset(material_sizes, 0, sizeof material_sizes);
    return persistent_regular();
}

static void material_multi_validate(void)
{
    check_bulk_accounting();
    validate_materials();
    cached_regular = recapture_regular;
}

int main(int argc, char **argv)
{
    const char *crt=argc>2?argv[2]:"C:/Jogos/AgeoftheRing/rotwk/msvcr71.dll";
    const char *slash=strrchr(crt,'/'),*backslash=strrchr(crt,'\\');
    if(backslash && (!slash || backslash>slash)) slash=backslash;
    REQUIRE(slash && (unsigned)(slash-crt)+1+sizeof "d3dx9_27.dll"<=sizeof material_runtime);
    memcpy(material_runtime,crt,(size_t)(slash-crt)+1);
    strcpy(material_runtime+(slash-crt)+1,"d3dx9_27.dll");
    material_setup=effect_setup;
    material_begin=material_fixture_begin;
    material_end=material_fixture_end;
    pose_adjust=spread;
    cached_regular=cached_extra=persistent_regular;
    preserve_lifecycle=restore_group_state=advancing_clock=validate_markers=1;
    persistent_churn=100;
    prepare_sample=sample_epoch;
    baseline_bulk_enabled=1;
    dispatch_scope_begin=bench_scope_bulk_begin;
    dispatch_scope_end=bench_scope_bulk_end;
    bench_retained_bulk_markers(bench_scope_bulk_begin,bench_scope_bulk_end);
    bench_retained_boundary_config(1,material_fixture_state);
    multi_validate=material_multi_validate;
    puts("MATERIAL MULTI: charge recapture every ordinary frame; validate all extra keys; real Begin/End and per-group restoration; bulk baseline; advancing cycle clock");
    int result=multi_benchmark(argc,argv);
    bench_filter_clear();
    return result;
}
