/* Owned queue -> native sort -> native group/mesh/FX -> real GPU. Offline
 * only: collection order, model/source computation and lifetimes controlled. */
#include "native_shared_arena.h"
#define PF_NATIVE_FX_MAIN queue_fx_main
#include "bench_native_fx_gpu.c"

extern void bench_sort_load(const char *,const char *);
extern void bench_sort_run(void *,void *,unsigned);
extern void bench_sort_copy(void *,const void *),bench_sort_destroy(void *);
extern void bench_sort_corrupt(unsigned);
#define QUEUE_RECORDS (GROUPS*GROUP_SIZE)
typedef struct {unsigned words[8];} QueueRecord;
static struct {unsigned before[8];QueueRecord records[QUEUE_RECORDS];unsigned after[8];} q_buffer;
static QueueRecord q_expected[QUEUE_RECORDS];
static unsigned q_order[QUEUE_RECORDS];
static unsigned char q_models[GROUPS][sizeof g_model];
static unsigned q_bounds[]={0,116*GROUP_SIZE,172*GROUP_SIZE,QUEUE_RECORDS};
static unsigned q_compare_mode,q_pattern,q_render_on,q_sort_on,q_oracle=1,q_expected_ready;
static unsigned q_owned,q_sort_calls,q_group_calls,q_order_hash,q_broken;
static void (*q_previous_validate)(void);
static const char *q_sort_manifest;

static unsigned q_random(unsigned *state)
{ *state=*state*1664525u+1013904223u;return *state; }
static void q_make_order(unsigned pattern,unsigned seed)
{
    for(unsigned i=0;i<QUEUE_RECORDS;i++)q_order[i]=i;
    for(unsigned queue=0;queue<3;queue++) {
        unsigned first=q_bounds[queue],last=q_bounds[queue+1],count=last-first;
        if(pattern==1) {
            for(unsigned i=0;i<count;i++)q_order[first+i]=last-1-i;
        } else if(pattern==2) {
            for(unsigned i=count-1;i;i--) {
                unsigned other=q_random(&seed)%(i+1),saved=q_order[first+i];
                q_order[first+i]=q_order[first+other];q_order[first+other]=saved;
            }
        } else if(pattern==3) {
            for(unsigned i=0;i<count;i++)q_order[first+i]=first+(i%(count/GROUP_SIZE))*GROUP_SIZE+i/(count/GROUP_SIZE);
        }
    }
    q_expected_ready=0;
}
static void q_restore_models(void)
{
    for(unsigned group=0;group<GROUPS;group++)memcpy(q_models[group],g_model,sizeof g_model);
    for(unsigned i=0;i<QUEUE_RECORDS;i++)*(void **)(many_meshes[i].b+PF_MESH_MODEL)=q_models[i/GROUP_SIZE];
}
static void q_refs(unsigned expected)
{
    REQUIRE(g_method.refs==5+expected*QUEUE_RECORDS);
    for(unsigned i=0;i<QUEUE_RECORDS;i++)REQUIRE(*(unsigned *)(many_meshes[i].b+4)==1+expected);
}
static void __attribute__((thiscall)) q_prepare(fake_mesh *mesh)
{
    ng_prepare(mesh);
    unsigned id=(unsigned)(mesh-many_meshes);REQUIRE(id<QUEUE_RECORDS);
    ng_current_group=id/GROUP_SIZE;
    REQUIRE(*(void **)(mesh->b+PF_MESH_MODEL)==q_models[ng_current_group]);
    q_group_calls++;
    q_order_hash=(q_order_hash^ng_current_group)*16777619u;
}
static void q_collect(void)
{
    REQUIRE(!q_owned);q_owned=1;
    memset(q_buffer.before,0xa5,sizeof q_buffer.before);memset(q_buffer.after,0xa5,sizeof q_buffer.after);
    for(unsigned i=0;i<QUEUE_RECORDS;i++) {
        unsigned index=q_order[i];
        bench_sort_copy(q_buffer.records+i,records[index/GROUP_SIZE]+(index%GROUP_SIZE)*PF_RECORD_SIZE);
    }
    if(q_oracle)q_refs(1);
}
static void q_release(void)
{
    REQUIRE(q_owned);
    for(unsigned i=0;i<QUEUE_RECORDS;i++)bench_sort_destroy(q_buffer.records+i);
    q_owned=0;
    if(q_oracle)q_refs(0);
    REQUIRE(g_method.refs==5);
}
static void q_workload(unsigned group)
{
    if(group)return;
    q_collect();
    for(unsigned queue=0;queue<3;queue++) {
        unsigned first=q_bounds[queue],last=q_bounds[queue+1];
        bench_sort_run(q_buffer.records+first,q_buffer.records+last,q_sort_on);q_sort_calls++;
    }
    if(q_oracle) {
        if(!q_expected_ready) {
            REQUIRE(!q_sort_on);memcpy(q_expected,q_buffer.records,sizeof q_expected);q_expected_ready=1;
        } else REQUIRE(!memcmp(q_expected,q_buffer.records,sizeof q_expected));
        for(unsigned i=0;i<8;i++)REQUIRE(q_buffer.before[i]==0xa5a5a5a5 && q_buffer.after[i]==0xa5a5a5a5);
        q_refs(1);
    }
    if(q_broken==2 && q_render_on)q_buffer.records[0]=q_buffer.records[1];
    for(unsigned queue=0;queue<3;queue++) {
        unsigned refs=g_method.refs;
        ng_group_call(q_render_on?ng_hooked:ng_symbols[0],ng_renderer,
                      q_buffer.records+q_bounds[queue],q_buffer.records+q_bounds[queue+1]);
        REQUIRE(!ng_group_abi_errors && g_method.refs==refs);
    }
    if(q_oracle)REQUIRE(!memcmp(q_expected,q_buffer.records,sizeof q_expected));
    q_release();
}
static void q_frame_mode(int optimized)
{
    q_sort_on=optimized && q_compare_mode!=2;
    q_render_on=optimized && q_compare_mode!=1;
    q_sort_calls=q_group_calls=0;q_order_hash=2166136261u;
    nx_frame_mode(q_render_on);
}
static void q_frame_check(int extra)
{
    int saved=pair_optimized;pair_optimized=q_render_on;nx_frame_check(extra);pair_optimized=saved;
    if(q_owned || q_sort_calls!=3 || q_group_calls!=GROUPS || g_method.refs!=5)
        fprintf(stderr,"queue frame: owned=%u sorts=%u groups=%u refs=%u extra=%d\n",q_owned,q_sort_calls,q_group_calls,g_method.refs,extra);
    REQUIRE(!q_owned && q_sort_calls==3 && q_group_calls==GROUPS && g_method.refs==5);
    unsigned expected=2166136261u;
    for(unsigned i=0;i<GROUPS;i++)expected=(expected^i)*16777619u;
    REQUIRE(q_order_hash==expected);
}
static void q_sample(unsigned round,unsigned repetition)
{
    q_oracle=0;sample_epoch(round,repetition);
    q_make_order(q_pattern,0x813a9037u+round*FRAMES_PER_SAMPLE+repetition);
}
static unsigned q_object_hash(void)
{
    return fnv((const unsigned char *)many_meshes,sizeof many_meshes)^
           fnv((const unsigned char *)many_containers,sizeof many_containers)^
           fnv((const unsigned char *)many_trees,sizeof many_trees)^
           fnv((const unsigned char *)q_models,sizeof q_models);
}
static void q_validate(void)
{
    /* Preserve the existing native group callback/fallback/verification
     * controls before checking whole queues and both frame images. */
    void *old_prepare=ng_symbols[10];
    ng_patch_call(ng_symbols[0],0xCD,ng_prepare);ng_patch_call(ng_hooked,0xCD,ng_prepare);
    group_render=ng_render;frame_mode=nx_frame_mode;frame_check=nx_frame_check;
    q_previous_validate();
    group_render=q_workload;frame_mode=q_frame_mode;frame_check=q_frame_check;
    ng_patch_call(ng_symbols[0],0xCD,old_prepare);ng_patch_call(ng_hooked,0xCD,old_prepare);
    q_restore_models();q_refs(0);
    static unsigned a[RT*RT],b[RT*RT];
    unsigned before_state[4096],after_state[4096];
    for(unsigned pattern=0;pattern<4;pattern++) {
        q_make_order(pattern,0x25381abc);q_oracle=1;
        for(unsigned extra=0;extra<2;extra++) {
            pose(extra);pair_optimized=0;frame_draw(extra,0,a);
            unsigned hash=q_object_hash(),words=nx_snapshot(before_state,4096);
            pair_optimized=1;frame_draw(extra,0,b);nm_compare(a,b);
            REQUIRE(hash==q_object_hash());
            REQUIRE(nx_snapshot(after_state,4096)==words && !memcmp(before_state,after_state,words*4));
        }
    }
    q_make_order(q_pattern,0x813a9037u);q_oracle=1;
    /* The caller may draw an optimized stale-pose control first later. */
    q_collect();
    for(unsigned queue=0;queue<3;queue++)bench_sort_run(q_buffer.records+q_bounds[queue],q_buffer.records+q_bounds[queue+1],0);
    memcpy(q_expected,q_buffer.records,sizeof q_expected);q_expected_ready=1;q_release();
    puts("QUEUE CONTROLS: four input orders, three owned ranges, exact sorted records/ties, 288 ordered groups, regular/extra output, object/engine/effect state, mesh/method references, guards and native ABI pass");
}
static void q_setup(void)
{
    bench_sort_load(nm_image,q_sort_manifest);bench_sort_corrupt(q_broken==1);
    q_previous_validate=validate_retained;
    q_restore_models();
    for(unsigned i=0;i<QUEUE_RECORDS;i++)*(unsigned *)(many_meshes[i].b+4)=1;
    q_refs(0);q_make_order(q_pattern,0x813a9037u);
    ng_symbols[10]=q_prepare;
    ng_patch_call(ng_symbols[0],0xCD,q_prepare);ng_patch_call(ng_hooked,0xCD,q_prepare);
    group_render=q_workload;frame_mode=q_frame_mode;frame_check=q_frame_check;
    prepare_sample=q_sample;validate_retained=q_validate;
    comparison_name=q_compare_mode==0?"native-queue-sort-render":q_compare_mode==1?"native-queue-sort-only":"native-queue-render-only";
    printf("QUEUE: 7200 owned records, ranges 2900/1400/2900, 288 distinct model identities, comparison=%u order=%u; native render bodies retain relative layout; no gameplay claim\n",q_compare_mode,q_pattern);
}
int main(int argc,char **argv)
{
    REQUIRE(argc==14 || argc==15);
    q_sort_manifest=argv[11];q_compare_mode=(unsigned)atoi(argv[12]);q_pattern=(unsigned)atoi(argv[13]);
    if(argc==15) {
        unsigned option=(unsigned)atoi(argv[14]);REQUIRE(option<=3);
        q_broken=option<3?option:0;benchmark_controls_only=option!=0;
    }
    REQUIRE(q_compare_mode<=2 && q_pattern<=3 && q_broken<=2);
    ng_after_setup=q_setup;
    int result=queue_fx_main(11,argv);
    REQUIRE(VirtualFree(na_base,0,MEM_RELEASE));
    return result;
}
