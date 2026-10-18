/* Native mesh/draw/cleanup with the local D3DX effect and real GPU. No
 * installed hooks. Group loop, source computation and geometry binding are
 * controlled fixture dependencies, not a complete engine integration. */
#define PF_BENCH_LEADER
#define PF_MATERIAL_MAIN material_queue_main
#include "bench_material_queue.c"
#include "native_mesh_image.h"

extern void bench_leader_native_sites(void *,void *,void *,void *volatile *,void *volatile *);
extern int bench_leader_can_bulk(void);
static const char *nm_image,*nm_manifest;
static void *nm_symbols[32],*nm_method_vt[8];
static void *volatile nm_begin_callback,*volatile nm_end_callback;
static unsigned char nm_state[0x290],nm_sas[0xE0],nm_material[0x120];
static void *nm_sas_ptr=nm_sas;
static unsigned nm_changed,nm_one=0x3F800000,nm_threshold=0x3DCCCCCD;
static unsigned nm_limit,nm_draw_count,nm_api_count;
static unsigned char nm_emissive,nm_enabled=1,nm_snapshot;
static unsigned nm_early_draw;

static void nm_unexpected(void) { REQUIRE(!"unexpected native dependency or exception"); }
static void __attribute__((thiscall)) nm_dynamic(void *self,unsigned channel)
{
    REQUIRE(self==&g_method && channel==1);
    const unsigned char *tree=*(const void **)(nm_sas+0xC8);
    const unsigned char *list=*(const void **)(nm_sas+0xCC);
    REQUIRE(tree && list);
    const short *b=*(const short *const *)list,*end=*(const short *const *)(list+4);
    const unsigned char *pivots=*(const void *const *)(tree+0x14);
    unsigned bones[BONES*8];
    REQUIRE(end-b==BONES && pivots && *(unsigned *)(nm_sas+0xD0)==g_v->joints);
    for(unsigned j=0;j<BONES;j++) {
        REQUIRE(b[j]>=0 && b[j]<*(const int *)(tree+0x10));
        memcpy(bones+j*8,pivots+b[j]*0x58+0x30,28);
        bones[j*8+7]=0;
    }
    material_fixture_bones(bones,sizeof bones);
    if(nm_early_draw)
        ((void (*)(unsigned,unsigned,unsigned,unsigned))nm_symbols[1])
            (0,GRID*GRID*2,0,(GRID+1)*(GRID+1));
}
static void __attribute__((thiscall)) nm_geometry(void *self,unsigned base)
{
    REQUIRE(self==g_geom && base==0);
    /* Shared fixed geometry is bound by frame_draw before either path. */
}
static void nm_apply_state(void) { /* Real effect owns fixture GPU state. */ }
#ifndef PF_NATIVE_MESH_APPLY_STATE
#define PF_NATIVE_MESH_APPLY_STATE nm_apply_state
#endif
static void nm_statistics(unsigned polys,unsigned vertices,void *state)
{
    REQUIRE(state==nm_state && polys==GRID*GRID*2 && vertices==(GRID+1)*(GRID+1));
    g_stock_draws++;
}
static void nm_patch(unsigned body,unsigned offset,void *target,int nop)
{
    DWORD old;
    unsigned char *p=(unsigned char *)nm_symbols[body]+offset;
    REQUIRE(VirtualProtect(nm_symbols[body],4096,PAGE_READWRITE,&old));
    *p=0xE8; *(unsigned *)(p+1)=(unsigned)target-(unsigned)(p+5);
    if(nop) p[5]=0x90;
    REQUIRE(VirtualProtect(nm_symbols[body],4096,PAGE_EXECUTE_READ,&old));
    REQUIRE(FlushInstructionCache(GetCurrentProcess(),nm_symbols[body],4096));
}
static void nm_validate(void);
static void nm_setup(void)
{
    for(unsigned i=7;i<19;i++) nm_symbols[i]=nm_unexpected;
    nm_symbols[12]=nm_geometry; nm_symbols[14]=PF_NATIVE_MESH_APPLY_STATE; nm_symbols[15]=nm_statistics;
    void *data[]={&nm_changed,nm_state,&nm_one,&nm_sas_ptr,&nm_limit,&nm_emissive,
                  &nm_enabled,&nm_snapshot,&g_dev,&nm_draw_count,&nm_api_count,&nm_threshold};
    for(unsigned i=0;i<12;i++) nm_symbols[i+19]=data[i];
    native_mesh_load(nm_symbols,nm_image,nm_manifest);
    nm_patch(2,0x185,pf_enter_stubs[PF_INST_DIP],1);
    nm_patch(1,0x18,pf_enter_stubs[PF_DRAW],0);
    nm_method_vt[0]=nm_unexpected; nm_method_vt[4]=nm_dynamic;
    g_method.vt=nm_method_vt;
    *(void **)(g_model+0x94)=nm_material;
    *(void **)(nm_material+0xB8)=g_geom;
    *(unsigned *)(g_model+0x24)=GRID*GRID*2;
    *(unsigned *)(g_model+0x28)=(GRID+1)*(GRID+1);
    pf_timing_set_next(PF_INST_MESH,nm_symbols[0]);
    pf_timing_set_next(PF_DRAW,nm_symbols[2]);
    pf_timing_set_next(PF_INST_DIP,PF_VT(g_dev)[PF_DEV_DRAW_INDEXED]);
    bench_leader_native_sites(nm_symbols[0],nm_symbols[1],nm_symbols[2],
                              &nm_begin_callback,&nm_end_callback);
    dispatch_available=bench_leader_can_bulk;
    validate_retained=nm_validate;
    comparison_name="native-mesh-leader-bulk-first";
    puts("NATIVE MESH: seven verified RAM bodies; real timing.c Draw wrapper and GPU/effect; group loop and sources remain controlled");
}

static pf_inst_counts nm_one_group(int extra,int optimized,int split,int early,
                                   unsigned *pixels)
{
    pf_inst_counts c;
    unsigned refs=g_method.refs,bulk=0;
    unsigned char other_model[sizeof g_model];
    memcpy(other_model,g_model,sizeof other_model);
    for(unsigned i=0;i<GROUP_SIZE;i++)
        *(void **)(many_meshes[i].b+PF_MESH_MODEL)=split && i>=13?other_model:g_model;
    pose(extra);
    bench_extra_mode(extra); bench_leader_mode(optimized,0); pf_inst_take(&c);
    g_stock_draws=0; nm_early_draw=early;
    HR(IDirect3DDevice9_SetVertexShader(g_dev,g_ea));
    HR(IDirect3DDevice9_SetVertexDeclaration(g_dev,g_decl));
    HR(IDirect3DDevice9_SetStreamSource(g_dev,0,g_vb,0,40));
    HR(IDirect3DDevice9_SetIndices(g_dev,g_ib));
    HR(IDirect3DDevice9_BeginScene(g_dev));
    HR(IDirect3DDevice9_Clear(g_dev,0,NULL,D3DCLEAR_TARGET |
        (shadow_mode?D3DCLEAR_ZBUFFER:0),shadow_mode?0xffffffffu:0,1,0));
    material_begin(0);
    current_records=records[0];
    *(void **)(frame_ptr+PF_FRAME_GROUP_END)=current_records+sizeof records[0];
    for(unsigned i=0;i<GROUP_SIZE;) {
        if(optimized && i==1 && bench_leader_can_bulk()) {
            REQUIRE(bench_scope_bulk_begin());
            unsigned n=bench_inst_bulk(current_records+i*PF_RECORD_SIZE,
                                       current_records+sizeof records[0],&g_method);
            REQUIRE(n && bench_scope_bulk_end(n));
            bulk+=n; i+=n;
        } else one_member(i++);
    }
    material_end();
    HR(IDirect3DDevice9_EndScene(g_dev));
    drain(); read_pixels(pixels); pf_inst_take(&c);
    REQUIRE(g_method.refs==refs && pf_inst_active());
    REQUIRE(!*(unsigned *)(nm_sas+0xC8) && !*(unsigned *)(nm_sas+0xCC) &&
            !*(unsigned *)(nm_sas+0xD0));
    if(nm_begin_callback || nm_end_callback || early || c.verify) {
        REQUIRE(!bench_leader_suppressed() && !bulk);
    } else {
        REQUIRE(bench_leader_suppressed()==(optimized?(split?2u:1u):0u));
        REQUIRE(g_stock_draws==(split?2u:1u));
        if(!extra) REQUIRE(c.meshes==GROUP_SIZE-(optimized?0u:(split?2u:1u)));
        if(optimized) REQUIRE(bulk==(split?12u:24u));
    }
    if(early) REQUIRE(g_stock_draws==GROUP_SIZE*2);
    for(unsigned i=0;i<GROUP_SIZE;i++) *(void **)(many_meshes[i].b+PF_MESH_MODEL)=g_model;
    nm_early_draw=0; bench_extra_mode(0);
    return c;
}

static void nm_compare(const unsigned *a,const unsigned *b)
{
    for(unsigned i=0;i<RT*RT;i++)
        REQUIRE(shadow_mode?pf_depth_diff(a[i],b[i])<=PF_DEPTH_TOLERANCE:a[i]==b[i]);
}

static void nm_validate(void)
{
    validate_leaders();
    static unsigned a[RT*RT],b[RT*RT];
    for(int extra=0;extra<2;extra++) {
        nm_one_group(extra,0,0,0,a);
        for(unsigned callback=0;callback<2;callback++) {
            nm_begin_callback=callback?NULL:nm_unexpected;
            nm_end_callback=callback?nm_unexpected:NULL;
            nm_one_group(extra,1,0,0,b); nm_compare(a,b);
        }
        nm_begin_callback=nm_end_callback=NULL;
        nm_one_group(extra,0,1,0,a);
        nm_one_group(extra,1,1,0,b); nm_compare(a,b);
        nm_one_group(extra,0,0,1,a);
        pf_inst_counts c=nm_one_group(extra,1,0,1,b); nm_compare(a,b);
        if(!extra) REQUIRE(c.mismatch==GROUP_SIZE && !c.meshes);
    }
    pf_inst_init(0); nm_one_group(0,0,0,0,a);
    pf_inst_init(2);
    pf_inst_counts c=nm_one_group(0,1,0,0,b); nm_compare(a,b);
    REQUIRE(c.verify && c.verify_samples && !c.verify_bad);
    pf_inst_init(0);
    /* Control helpers leave the last group bound, just as the frame loop. */
    puts("NATIVE CONTROLS: callback-slot fallback, split at member 13, partial bulk, earlier source DIP and real verification window pass; output, cleanup and references match");
}

#ifndef PF_NATIVE_MESH_MAIN
#define PF_NATIVE_MESH_MAIN main
#endif
int PF_NATIVE_MESH_MAIN(int argc,char **argv)
{
    REQUIRE(argc==9 && !strcmp(argv[6],"leader-bulk-first"));
    nm_image=argv[7]; nm_manifest=argv[8]; setup_complete=nm_setup;
    int result=material_queue_main(7,argv);
    for(unsigned i=0;i<7;i++) REQUIRE(VirtualFree(nm_symbols[i],0,MEM_RELEASE));
    return result;
}
