/* Verified native group/events/method-chain -> mesh/draw/cleanup -> real
 * local effect and GPU. Source and geometry/state helpers remain controlled. */
#define PF_NATIVE_MESH_MAIN mesh_only_main
#include "bench_native_leader.c"
#include "native_group_image.h"
extern void material_fixture_prepare(unsigned),material_fixture_pass_begin(unsigned);
extern void material_fixture_pass_end(void);
extern void bench_native_group_mode(unsigned);
extern unsigned bench_native_group_consumed(void),ng_group_abi_errors;
extern void *ng_group_resume,*ng_group_fallback;
extern void ng_group_bridge(void),ng_group_call(void *,void *,void *,void *);
#ifdef PF_RUNTIME_BULK
#include "bulk.h"
static void runtime_group_mode(unsigned on) { REQUIRE(pf_bulk_enable(on)); }
#define bench_native_group_mode runtime_group_mode
#define bench_native_group_consumed pf_bulk_consumed
#define ng_group_bridge pf_bulk_bridge
#endif
static const char *ng_manifest;
static void *ng_symbols[34],*ng_hooked;
static void *ng_target(unsigned optimized)
{
#ifdef PF_RUNTIME_BULK
    return optimized ? (void *)pf_bulk_entry : ng_symbols[0];
#else
    return optimized ? ng_hooked : ng_symbols[0];
#endif
}
static unsigned char ng_renderer[0x40];
static FARPROC ng_sprintf,ng_strncpy;
static const char ng_prefix[]="SceneAnalyst";
static const char *ng_labels[]={"RenderFXShaderBatch","Rendering mesh\tFXShader\t",
    "(unnamed)","MeshFXShader","Rendering %d meshes\tFXShader\t%s"};
static struct {unsigned pad[3];const char *name;} ng_name={{0},"BENCH_SYNTHETIC_UNIT_MODEL"};
static unsigned ng_current_group,ng_callbacks_begin,ng_callbacks_end,ng_callbacks_hash;
static unsigned ng_prepares,ng_finishes,ng_begins,ng_ends,ng_passes,ng_pass_ends;
static unsigned ng_begin_fail,ng_failed_begins,ng_force_identical;
static void (*ng_after_setup)(void);

static unsigned *ng_frame(void)
{
    unsigned p; __asm__ volatile("movl %%fs:0,%0":"=r"(p)); return (unsigned *)(p+12);
}
static void ng_state(void)
{
    unsigned *f=ng_frame();
    REQUIRE(f[-1]==1 && f[-2]==(unsigned)ng_symbols[19]);
}
static int __attribute__((thiscall)) ng_compare(fake_mesh *a,fake_mesh *b)
{
    return ng_force_identical || *(void **)(a->b+PF_MESH_MODEL)==*(void **)(b->b+PF_MESH_MODEL);
}
static void __attribute__((thiscall)) ng_prepare(fake_mesh *mesh)
{
    ng_state(); REQUIRE(mesh); ng_prepares++;
}
static void __attribute__((thiscall)) ng_finish(fake_mesh *mesh)
{
    ng_state(); REQUIRE(mesh); ng_finishes++;
}
static int __attribute__((thiscall)) ng_begin(void *self,int *passes,int flags)
{
    ng_state(); REQUIRE(self==&g_method && g_method.refs>=2 && flags==0xFFFF);
    ng_begins++; *passes=1;
    if(ng_begin_fail) { ng_failed_begins++; return 0; }
    material_fixture_prepare(ng_current_group); material_fixture_open(); return 1;
}
static void __attribute__((thiscall)) ng_begin_pass(void *self,unsigned pass)
{
    ng_state(); REQUIRE(self==&g_method && pass==0); ng_passes++;
    material_fixture_pass_begin(pass);
}
static void __attribute__((thiscall)) ng_end_pass(void *self)
{
    ng_state(); REQUIRE(self==&g_method); ng_pass_ends++; material_fixture_pass_end();
}
static void __attribute__((thiscall)) ng_end(void *self)
{
    ng_state(); REQUIRE(self==&g_method); ng_ends++; material_fixture_close();
}
static void __attribute__((thiscall)) ng_set_next(void *self,void *next)
{
    REQUIRE(self==&g_method && !next);
}
static void WINAPI ng_on_begin(unsigned color,const unsigned short *label)
{
    REQUIRE(!color); ng_callbacks_begin++;
    ng_callbacks_hash=(ng_callbacks_hash^g_stock_draws)*16777619u;
    for(unsigned i=0;label[i];i++) {
        REQUIRE(i<255); ng_callbacks_hash=(ng_callbacks_hash^label[i])*16777619u;
    }
}
static int ng_on_end(void)
{
    ng_callbacks_end++; ng_callbacks_hash=(ng_callbacks_hash^g_stock_draws)*16777619u;
    return 0;
}

static void ng_frame_mode(int optimized)
{
    leader_frame(optimized); bench_native_group_mode(optimized);
}
static void ng_render(unsigned group)
{
    ng_current_group=group;
    unsigned refs=g_method.refs;
    ng_group_call(ng_target(pair_optimized),ng_renderer,
                  records[group],records[group]+sizeof records[0]);
    REQUIRE(!ng_group_abi_errors && g_method.refs==refs);
}
static void ng_frame_check(int extra)
{
    leader_check(extra);
    REQUIRE(bench_native_group_consumed()==(pair_optimized?GROUPS*(GROUP_SIZE-1):0));
}
static void ng_patch_call(void *body,unsigned offset,void *target)
{
    unsigned char *p=(unsigned char *)body+offset; DWORD old;
    REQUIRE(VirtualProtect(body,4096,PAGE_READWRITE,&old));
    REQUIRE(*p==0xE8); *(unsigned *)(p+1)=(unsigned)target-(unsigned)(p+5);
    REQUIRE(VirtualProtect(body,4096,PAGE_EXECUTE_READ,&old));
    REQUIRE(FlushInstructionCache(GetCurrentProcess(),body,4096));
}
static void ng_validate(void);
static void ng_setup(void)
{
    nm_setup();
    for(unsigned i=9;i<17;i++) ng_symbols[i]=nm_unexpected;
    ng_symbols[9]=ng_compare; ng_symbols[10]=ng_prepare;
    ng_symbols[11]=pf_enter_stubs[PF_INST_MESH]; ng_symbols[12]=ng_finish;
    HMODULE crt=GetModuleHandleA("msvcr71.dll"); REQUIRE(crt);
    ng_symbols[17]=GetProcAddress(crt,"_mbscpy"); ng_symbols[18]=GetProcAddress(crt,"_mbscat");
    ng_sprintf=GetProcAddress(crt,"sprintf"); ng_strncpy=GetProcAddress(crt,"strncpy");
    REQUIRE(ng_symbols[17] && ng_symbols[18] && ng_sprintf && ng_strncpy);
    ng_symbols[19]=nm_unexpected;
    for(unsigned i=0;i<5;i++) ng_symbols[20+i]=(void *)ng_labels[i];
    ng_symbols[25]=&ng_sprintf; ng_symbols[26]=&nm_sas_ptr; ng_symbols[27]=&ng_strncpy;
    for(unsigned i=0;i<4;i++) ng_symbols[28+i]=(void *)(ng_prefix+4*i);
    ng_symbols[32]=(void *)&nm_begin_callback; ng_symbols[33]=(void *)&nm_end_callback;
    native_group_load(ng_symbols,nm_image,ng_manifest,ng_group_bridge,
                      &ng_hooked,&ng_group_resume,&ng_group_fallback);
#ifdef PF_RUNTIME_BULK
    REQUIRE(pf_bulk_bind(ng_hooked,ng_group_resume,ng_group_fallback));
#endif
    const unsigned begin_offsets[]={0xC2,0x1AA,0x22D},end_offsets[]={0x1D1,0x2AF,0x2FF};
    for(unsigned i=0;i<3;i++) for(unsigned j=0;j<2;j++) {
        void *body=j?ng_hooked:ng_symbols[0];
        ng_patch_call(body,begin_offsets[i],pf_enter_stubs[PF_EVENT_BEGIN]);
        ng_patch_call(body,end_offsets[i],pf_enter_stubs[PF_EVENT_END]);
    }
    pf_timing_set_next(PF_EVENT_BEGIN,ng_symbols[5]);
    pf_timing_set_next(PF_EVENT_END,ng_symbols[6]);
    nm_method_vt[2]=ng_begin; nm_method_vt[3]=ng_begin_pass;
    nm_method_vt[5]=ng_end_pass; nm_method_vt[6]=ng_end; nm_method_vt[7]=ng_set_next;
    *(void **)(g_model+0x10)=&ng_name; *(unsigned *)(g_geom+0x24)=1;
    for(unsigned g=0;g<GROUPS;g++) for(unsigned i=0;i<GROUP_SIZE;i++) {
        *(unsigned *)(records[g]+i*PF_RECORD_SIZE+4)=1;
        *(void **)(records[g]+i*PF_RECORD_SIZE+8)=&g_method;
    }
    group_render=ng_render; frame_mode=ng_frame_mode; frame_check=ng_frame_check;
    scopes_per_group=GROUP_SIZE+1; validate_retained=ng_validate;
    comparison_name="native-group-mesh-bulk-first";
    puts("NATIVE GROUP: nine verified bodies joined to native mesh and GPU; actual events, owned methods, loop and cleanup; source/state helpers remain controlled");
    if(ng_after_setup) ng_after_setup();
}

typedef struct {
    unsigned begin,end,hash,stock,bulk,suppressed,scopes;
    pf_inst_counts counts;
} NgCheck;

static NgCheck ng_one_group(unsigned extra,unsigned optimized,unsigned count,unsigned split,
                            unsigned callbacks,unsigned early,unsigned fail,unsigned verify,
                            unsigned *pixels)
{
    NgCheck result={0};
    pf_timing_stats before,after;
    unsigned char other_model[sizeof g_model];
    memcpy(other_model,g_model,sizeof other_model);
    for(unsigned i=0;i<GROUP_SIZE;i++)
        *(void **)(many_meshes[i].b+PF_MESH_MODEL)=split && i>=13?other_model:g_model;
    ng_force_identical=split==2; ng_begin_fail=fail; nm_early_draw=early;
    nm_begin_callback=callbacks&1?ng_on_begin:NULL;
    nm_end_callback=callbacks&2?ng_on_end:NULL;
    ng_callbacks_begin=ng_callbacks_end=0; ng_callbacks_hash=2166136261u;
    pair_optimized=optimized; ng_current_group=0;
    pose(extra); pf_inst_init(verify?2:0); bench_extra_mode(extra);
    ng_frame_mode(optimized); g_stock_draws=0;
    HR(IDirect3DDevice9_SetVertexShader(g_dev,g_ea));
    HR(IDirect3DDevice9_SetVertexDeclaration(g_dev,g_decl));
    HR(IDirect3DDevice9_SetStreamSource(g_dev,0,g_vb,0,40));
    HR(IDirect3DDevice9_SetIndices(g_dev,g_ib));
    HR(IDirect3DDevice9_BeginScene(g_dev));
    HR(IDirect3DDevice9_Clear(g_dev,0,NULL,D3DCLEAR_TARGET |
        (shadow_mode?D3DCLEAR_ZBUFFER:0),shadow_mode?0xffffffffu:0,1,0));
    unsigned refs=g_method.refs;
    pf_timing_get_stats(&before);
    ng_group_call(ng_target(optimized),ng_renderer,
                  records[0],records[0]+count*PF_RECORD_SIZE);
    pf_timing_get_stats(&after);
    HR(IDirect3DDevice9_EndScene(g_dev)); drain(); read_pixels(pixels);
    REQUIRE(!ng_group_abi_errors && g_method.refs==refs && pf_inst_active());
    REQUIRE(after.stale_scopes==before.stale_scopes);
    REQUIRE(!*(unsigned *)(nm_sas+0xC8) && !*(unsigned *)(nm_sas+0xCC) &&
            !*(unsigned *)(nm_sas+0xD0));
    result.begin=ng_callbacks_begin; result.end=ng_callbacks_end; result.hash=ng_callbacks_hash;
    result.stock=g_stock_draws; result.bulk=bench_native_group_consumed();
    result.suppressed=bench_leader_suppressed(); result.scopes=after.scopes-before.scopes;
    pf_inst_take(&result.counts);
    unsigned batches=count?(split==1?2u:1u):0u;
    unsigned events=fail?batches:count+batches;
    REQUIRE(result.begin==((callbacks&1)?events:0) && result.end==((callbacks&2)?events:0));
    REQUIRE(result.scopes==(extra?0:events));
    if(callbacks || early || verify || fail || count<3) {
        REQUIRE(!result.suppressed && !result.bulk);
    } else {
        unsigned leaders=split?2:1;
        REQUIRE(result.stock==leaders && result.suppressed==(optimized?leaders:0));
        REQUIRE(result.bulk==(optimized?count-leaders:0));
        if(!extra) REQUIRE(result.counts.meshes==count-(optimized?0:leaders));
    }
    if(fail) REQUIRE(!result.stock && !result.counts.meshes);
    if(early) REQUIRE(result.stock==2*count);
    for(unsigned i=0;i<GROUP_SIZE;i++) *(void **)(many_meshes[i].b+PF_MESH_MODEL)=g_model;
    ng_force_identical=ng_begin_fail=nm_early_draw=0;
    nm_begin_callback=nm_end_callback=NULL; bench_extra_mode(0); pf_inst_init(0);
    return result;
}

static void ng_validate(void)
{
    validate_leaders();
    static unsigned a[RT*RT],b[RT*RT];
    unsigned cases=0;
    const unsigned sizes[]={0,1,2,3,25};
    for(unsigned extra=0;extra<2;extra++) for(unsigned callbacks=0;callbacks<4;callbacks++)
    for(unsigned ci=0;ci<5;ci++) for(unsigned scenario=0;scenario<5;scenario++) {
        unsigned count=sizes[ci],split=scenario==1?1:scenario==2?2:0;
        if(split && count!=25) continue;
        unsigned early=scenario==3,fail=scenario==4;
        NgCheck stock=ng_one_group(extra,0,count,split,callbacks,early,fail,0,a);
        NgCheck fast=ng_one_group(extra,1,count,split,callbacks,early,fail,0,b);
        for(unsigned i=0;i<RT*RT;i++) if(shadow_mode?pf_depth_diff(a[i],b[i])>PF_DEPTH_TOLERANCE:a[i]!=b[i]) {
            fprintf(stderr,"group image mismatch: extra=%u callbacks=%u count=%u scenario=%u pixel=%u\n",
                    extra,callbacks,count,scenario,i);break;
        }
        nm_compare(a,b);
        REQUIRE(stock.begin==fast.begin && stock.end==fast.end && stock.hash==fast.hash &&
                stock.stock==fast.stock && stock.scopes==fast.scopes);
        cases++;
    }
    ng_one_group(0,0,25,0,0,0,0,0,a);
    NgCheck verified=ng_one_group(0,1,25,0,0,0,0,1,b); nm_compare(a,b);
    REQUIRE(verified.counts.verify && verified.counts.verify_samples && !verified.counts.verify_bad);
    REQUIRE(ng_prepares==ng_finishes+ng_failed_begins && ng_begins==ng_ends+ng_failed_begins &&
            ng_passes==ng_pass_ends);
    printf("NATIVE GROUP CONTROLS: %u original/candidate pairs, actual callback sequence/draw observations, empty/small groups, native and partial model splits, earlier DIP, Begin failure, verification and ABI pass\n",cases);
}

#ifndef PF_NATIVE_GROUP_MAIN
#define PF_NATIVE_GROUP_MAIN main
#endif
int PF_NATIVE_GROUP_MAIN(int argc,char **argv)
{
    REQUIRE(argc==10 && !strcmp(argv[6],"leader-bulk-first"));
    nm_image=argv[7]; nm_manifest=argv[8]; ng_manifest=argv[9]; setup_complete=ng_setup;
    int result=material_queue_main(7,argv);
    REQUIRE(ng_prepares==ng_finishes+ng_failed_begins && ng_begins==ng_ends+ng_failed_begins &&
            ng_passes==ng_pass_ends);
    for(unsigned i=0;i<7;i++) REQUIRE(PF_NATIVE_BODY_FREE(nm_symbols[i]));
    for(unsigned i=0;i<9;i++) REQUIRE(PF_NATIVE_BODY_FREE(ng_symbols[i]));
    REQUIRE(VirtualFree(ng_hooked,0,MEM_RELEASE));
    return result;
}
