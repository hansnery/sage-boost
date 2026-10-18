/* Offline native FX lifecycle + dirty state, joined to the real group/mesh
 * fixture. Material input computation and asset lifetime remain controlled. */
#define PF_NATIVE_FX_METHOD
static void nx_apply_state(void);
#define PF_NATIVE_MESH_APPLY_STATE nx_apply_state
#undef PF_NATIVE_GROUP_MAIN
#define PF_NATIVE_GROUP_MAIN nx_group_main
#include "bench_native_group_gpu.c"
#include "native_fx_image.h"
extern void material_fixture_native(void **,unsigned *,unsigned *);
extern void material_fixture_sources(unsigned),material_fixture_bone_values(const void *,unsigned);
extern unsigned material_fixture_live_state(unsigned *,unsigned);
static const char *nx_manifest;
static void *nx_symbols[27];
static unsigned char nx_skip=1,nx_caps[0x2E4],nx_lights[4];
static void *nx_caps_ptr=nx_caps;
static unsigned nx_matrix_count,nx_vertex_count,nx_index_count,nx_light_count;
static unsigned char nx_vb[0x20],nx_ib[0x18];
static unsigned nx_format[]={0,0,0,40};
static void (*nx_after_setup)(void);
static unsigned nx_static_calls,nx_bone_calls,nx_dynamic_calls,nx_begins;
typedef struct {void *function;unsigned payload;} NxItem;
typedef struct {NxItem *begin,*end,*capacity;} NxRange;
typedef struct {unsigned technique;NxRange ranges[6];} NxRecord;
typedef struct {void *owner;NxRecord *begin,*end,*capacity;} NxBinding;
typedef struct {unsigned pad;void *fx;NxBinding *binding;} NxLoaded;
typedef struct {void **vt;unsigned pad[4];NxLoaded *loaded;} NxAsset;
static NxAsset nx_asset;
static NxLoaded nx_loaded;
static NxBinding nx_binding;
static NxRecord nx_record;
static NxItem nx_items[2];
static void *nx_asset_vt[12];
_Static_assert(sizeof(fake_method)==0x34 && offsetof(NxAsset,loaded)==0x14,"native FX layouts");

static unsigned nx_snapshot(unsigned *out,unsigned capacity)
{
    unsigned bytes=sizeof nm_state+sizeof nm_sas+sizeof g_method;
    REQUIRE(capacity>bytes/4+2);
    memcpy(out,nm_state,sizeof nm_state);
    memcpy((unsigned char *)out+sizeof nm_state,nm_sas,sizeof nm_sas);
    memcpy((unsigned char *)out+sizeof nm_state+sizeof nm_sas,&g_method,sizeof g_method);
    unsigned words=bytes/4;out[words++]=nm_changed;out[words++]=nx_skip;
    return words+material_fixture_live_state(out+words,capacity-words);
}

static unsigned char __attribute__((thiscall)) nx_ready(void *self)
{ REQUIRE(self==&nx_asset);return 1; }
static void nx_apply_state(void)
{ REQUIRE(nx_symbols[8]);((void (*)(void))nx_symbols[8])(); }
static void nx_stats(void) { nx_begins++; }

static void __attribute__((thiscall)) nx_source(NxItem *item,void *owner,unsigned payload)
{
    REQUIRE(owner==&nx_asset && payload<2 && item==&nx_items[payload] && item->function==nx_source);
    if(!payload) { material_fixture_sources(ng_current_group);nx_static_calls++;return; }
    nx_bone_calls++;
    /* Controlled source computation; the native walker/selector/Commit
     * sequence executes. Begin has no bound mesh context in this fixture. */
    const unsigned char *tree=*(const void **)(nm_sas+0xC8);
    const unsigned char *list=*(const void **)(nm_sas+0xCC);
    if(!tree) { REQUIRE(!list);return; }
    REQUIRE(list);
    const short *b=*(const short *const *)list,*end=*(const short *const *)(list+4);
    const unsigned char *pivots=*(const void *const *)(tree+0x14);
    unsigned bones[BONES*8];REQUIRE(end-b==BONES && pivots);
    for(unsigned j=0;j<BONES;j++) {
        REQUIRE(b[j]>=0 && b[j]<*(const int *)(tree+0x10));
        memcpy(bones+j*8,pivots+b[j]*0x58+0x30,28);bones[j*8+7]=0;
    }
    if(nm_early_draw) {
        /* The deliberately injected earlier DIP must commit its own
         * palette, as the previous control did. Otherwise its first draw
         * depends on the last case's effect constants before Dynamic's
         * native trailing CommitChanges, making the oracle history-based. */
        material_fixture_bones(bones,sizeof bones);
        ((void (*)(unsigned,unsigned,unsigned,unsigned))nm_symbols[1])(0,GRID*GRID*2,0,(GRID+1)*(GRID+1));
    } else material_fixture_bone_values(bones,sizeof bones);
}

static int __attribute__((thiscall)) nx_begin(void *self,int *passes,int flags)
{
    ng_state();REQUIRE(self==&g_method && g_method.refs>=2 && flags==0xFFFF);
    ng_begins++;
    unsigned tech,block;
    material_fixture_native(&nx_loaded.fx,&tech,&block);
    g_method.native_fields[0]=(unsigned)&nx_asset;
    g_method.native_fields[6]=ng_begin_fail?0:tech;
    g_method.native_fields[7]=block;nx_record.technique=tech;
    /* The scene supplies buffer objects; actual dirty-bit application is
     * native. Every group dirties bindings so the tested path is exercised. */
    nm_changed|=0x30000;
    unsigned char ok=((unsigned char (__attribute__((thiscall)) *)(void *,int *,unsigned))nx_symbols[1])(self,passes,flags);
    if(!ok) ng_failed_begins++;
    else REQUIRE(*passes==1 && !(nm_changed&0x30000));
    return ok;
}
static void __attribute__((thiscall)) nx_pass(void *self,unsigned pass)
{
    ng_state();REQUIRE(self==&g_method && pass==0);ng_passes++;
    ((void (__attribute__((thiscall)) *)(void *,unsigned))nx_symbols[2])(self,pass);
}
static void __attribute__((thiscall)) nx_dynamic(void *self,unsigned channel)
{
    REQUIRE(self==&g_method && channel==1);nx_dynamic_calls++;
    ((void (__attribute__((thiscall)) *)(void *,unsigned))nx_symbols[3])(self,channel);
}
static void __attribute__((thiscall)) nx_end_pass(void *self)
{
    ng_state();REQUIRE(self==&g_method);ng_pass_ends++;
    ((void (__attribute__((thiscall)) *)(void *))nx_symbols[4])(self);
}
static void __attribute__((thiscall)) nx_end(void *self)
{
    ng_state();REQUIRE(self==&g_method);ng_ends++;
    ((void (__attribute__((thiscall)) *)(void *))nx_symbols[5])(self);
}
static void nx_frame_mode(int optimized)
{
    nx_static_calls=nx_bone_calls=nx_dynamic_calls=nx_begins=0;
    ng_frame_mode(optimized);
}
static void nx_frame_check(int extra)
{
    ng_frame_check(extra);
    REQUIRE(nx_static_calls==GROUPS && nx_bone_calls==GROUPS*2 && nx_dynamic_calls==GROUPS && nx_begins==GROUPS);
}
static void nx_state_controls(void)
{
    IDirect3DStateBlock9 *saved=NULL;
    HR(IDirect3DDevice9_CreateStateBlock(g_dev,D3DSBT_ALL,&saved));
    for(unsigned skip=0;skip<2;skip++) {
        nx_skip=(unsigned char)skip;
        HR(IDirect3DDevice9_SetStreamSource(g_dev,0,NULL,0,0));
        HR(IDirect3DDevice9_SetStreamSource(g_dev,1,g_vb,0,40));
        HR(IDirect3DDevice9_SetIndices(g_dev,NULL));
        unsigned vertices=nx_vertex_count,indices=nx_index_count,api=nm_api_count;
        nm_changed=0xF0000;nx_apply_state();
        REQUIRE(nm_changed==0xC0000 && nx_vertex_count-vertices==2 && nx_index_count-indices==1 && nm_api_count-api==3);
        IDirect3DVertexBuffer9 *vb=NULL;UINT offset,stride;
        HR(IDirect3DDevice9_GetStreamSource(g_dev,0,&vb,&offset,&stride));
        REQUIRE(vb==g_vb && !offset && stride==40);IDirect3DVertexBuffer9_Release(vb);
        HR(IDirect3DDevice9_GetStreamSource(g_dev,1,&vb,&offset,&stride));REQUIRE(!vb);
        IDirect3DIndexBuffer9 *ib=NULL;HR(IDirect3DDevice9_GetIndices(g_dev,&ib));
        REQUIRE(ib==g_ib);IDirect3DIndexBuffer9_Release(ib);
        api=nm_api_count;nm_changed=0;nx_apply_state();REQUIRE(nm_api_count==api);
    }
    nx_skip=1;nm_changed=0;HR(IDirect3DStateBlock9_Apply(saved));IDirect3DStateBlock9_Release(saved);
    puts("NATIVE STATE: pending vertex/index bindings restored from wrong real device bindings in both skip modes; exact dirty mask, counters and clean no-op pass");
}
static void nx_setup(void)
{
    for(unsigned i=9;i<16;i++)nx_symbols[i]=nm_unexpected;
    nx_symbols[10]=nx_stats;nx_symbols[11]=nx_source;
    void *data[]={&nm_changed,&nx_skip,nm_state,&nx_caps_ptr,&g_dev,nx_lights,&nm_api_count,
                  &nx_matrix_count,&nx_vertex_count,&nx_index_count,&nx_light_count};
    for(unsigned i=0;i<11;i++)nx_symbols[16+i]=data[i];
    native_fx_load(nx_symbols,nm_image,nx_manifest);
    nx_asset_vt[10]=nx_ready;nx_asset_vt[11]=nm_unexpected;
    nx_asset.vt=nx_asset_vt;nx_asset.loaded=&nx_loaded;nx_loaded.binding=&nx_binding;
    nx_binding.owner=&nx_asset;nx_binding.begin=&nx_record;nx_binding.end=nx_binding.capacity=&nx_record+1;
    for(unsigned i=0;i<2;i++) {
        nx_items[i].function=nx_source;nx_items[i].payload=i;
        nx_record.ranges[i].begin=&nx_items[i];nx_record.ranges[i].end=nx_record.ranges[i].capacity=&nx_items[i]+1;
    }
    *(void **)(nx_vb+0x14)=nx_format;nx_vb[0x18]=1;*(void **)(nx_vb+0x1C)=g_vb;
    *(void **)(nx_ib+0x14)=g_ib;*(void **)(nm_state+0x280)=nx_vb;*(void **)(nm_state+0x288)=nx_ib;
    nm_method_vt[2]=nx_begin;nm_method_vt[3]=nx_pass;nm_method_vt[4]=nx_dynamic;
    nm_method_vt[5]=nx_end_pass;nm_method_vt[6]=nx_end;
    frame_mode=nx_frame_mode;frame_check=nx_frame_check;
    comparison_name="native-fx-state-recovery";
    nx_state_controls();
    puts("NATIVE FX: nine verified lifecycle/state bodies added; live effect receives native Begin/Pass/Dynamic/End, source selection and dirty-state application; sources and asset lifetimes controlled");
    if(nx_after_setup)nx_after_setup();
}
#ifndef PF_NATIVE_FX_MAIN
#define PF_NATIVE_FX_MAIN main
#endif
int PF_NATIVE_FX_MAIN(int argc,char **argv)
{
    REQUIRE(argc==11);nx_manifest=argv[10];
    nx_after_setup=ng_after_setup;ng_after_setup=nx_setup;
    int result=nx_group_main(argc-1,argv);
    for(unsigned i=0;i<9;i++)REQUIRE(VirtualFree(nx_symbols[i],0,MEM_RELEASE));
    return result;
}
