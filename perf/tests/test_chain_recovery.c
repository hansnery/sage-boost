/* Native group/mesh/FX recovery with controlled owning material chains.
 * Child lifecycle changes real GPU state; it is not a native subclass. */
static void cq_base_controls(void);
static void cq_case_begin(unsigned,unsigned,unsigned);
static void cq_case_end(unsigned,unsigned,unsigned,const unsigned *);
static unsigned cq_snapshot(unsigned *,unsigned);
#define PF_FAULT_BASE_CONTROLS cq_base_controls
#define PF_FAULT_CASE_BEGIN cq_case_begin
#define PF_FAULT_CASE_END cq_case_end
#define PF_FAULT_EXTRA_STATE cq_snapshot
#define PF_NATIVE_FAULT_MAIN cq_inherited_main
#include "test_native_group_fallback.c"
#include "native_chain_fixture.h"

static unsigned cq_phase[7],cq_saved[7],cq_calls[5],cq_requested=6,cq_first=1;
static unsigned cq_broken;
static unsigned cq_material[7][7];
extern void material_fixture_child_state(unsigned *,int);
extern void material_fixture_child_apply(unsigned,unsigned);
#define CQ_CHAIN_WORDS (29+49+7)
static unsigned cq_record_hash,cq_reference[4096],cq_reference_words;

static unsigned cq_snapshot(unsigned *out,unsigned capacity)
{
    unsigned n=nx_snapshot(out,capacity);REQUIRE(capacity>=n+CQ_CHAIN_WORDS);
    for(unsigned i=0;i<7;i++) {
        out[n++]=(unsigned)chain_links[i];out[n++]=cq_phase[i];
        out[n++]=i?chain_children[i-1].refs:g_method.refs;
        out[n++]=cq_saved[i];
    }
    DWORD mask;HR(IDirect3DDevice9_GetRenderState(g_dev,D3DRS_COLORWRITEENABLE,&mask));
    out[n++]=mask;
    memcpy(out+n,cq_material,sizeof cq_material);n+=49;
    material_fixture_child_state(out+n,0);n+=7;
    return n;
}

static unsigned cq_indices(unsigned *indices)
{
    fake_method *m=chain_links[0];unsigned n=0;
    while(m) { REQUIRE(n<5);unsigned index=chain_index(m);indices[n++]=index;m=chain_links[index]; }
    REQUIRE(n==chain_count-1);return n;
}
static unsigned cq_mask(unsigned index) { return 9u|((index&1)?2u:4u); }

static int __attribute__((thiscall)) cq_begin(void *self,int *passes,int flags)
{
    int ok=chain_begin(self,passes,flags);
    if(ok) {
        unsigned indices[5],n=cq_indices(indices);
        for(unsigned i=0;i<n;i++) { REQUIRE(!cq_phase[indices[i]]);cq_phase[indices[i]]=1; }
        cq_calls[0]++;
    }
    return ok;
}
static void __attribute__((thiscall)) cq_pass(void *self,unsigned pass)
{
    nx_pass(self,pass);
    unsigned indices[5],n=cq_indices(indices);
    for(unsigned i=0;i<n;i++) {
        unsigned j=indices[i];REQUIRE(cq_phase[j]==1);cq_phase[j]=2;
        material_fixture_child_state(cq_material[j],0);
        material_fixture_child_apply(j,0);
        HR(IDirect3DDevice9_GetRenderState(g_dev,D3DRS_COLORWRITEENABLE,(DWORD *)&cq_saved[j]));
        HR(IDirect3DDevice9_SetRenderState(g_dev,D3DRS_COLORWRITEENABLE,cq_mask(j)));
    }
    cq_calls[1]++;
}
static void __attribute__((thiscall)) cq_dynamic(void *self,unsigned channel)
{
    unsigned indices[5],n=cq_indices(indices);
    for(unsigned i=0;i<n;i++) {
        unsigned j=indices[i];REQUIRE(cq_phase[j]==2);
        material_fixture_child_apply(j,cq_broken==1 && chain_allow);
        HR(IDirect3DDevice9_SetRenderState(g_dev,D3DRS_COLORWRITEENABLE,cq_mask(j)));
    }
    nx_dynamic(self,channel); /* Native CommitChanges applies child inputs. */
    cq_calls[2]++;
}
static void __attribute__((thiscall)) cq_end_pass(void *self)
{
    unsigned indices[5],n=cq_indices(indices);
    while(n) {
        unsigned j=indices[--n];REQUIRE(cq_phase[j]==2);cq_phase[j]=1;
        if(cq_broken!=2 || !chain_allow)
            HR(IDirect3DDevice9_SetRenderState(g_dev,D3DRS_COLORWRITEENABLE,cq_saved[j]));
        if(cq_broken!=2 || !chain_allow)material_fixture_child_state(cq_material[j],1);
        memset(cq_material[j],0,sizeof cq_material[j]);
        cq_saved[j]=0;
    }
    nx_end_pass(self);cq_calls[3]++;
}
static void __attribute__((thiscall)) cq_end(void *self)
{
    nx_end(self);
    unsigned indices[5],n=cq_indices(indices);
    for(unsigned i=0;i<n;i++) { REQUIRE(cq_phase[indices[i]]==1);cq_phase[indices[i]]=0; }
    cq_calls[4]++;
}

static void cq_install(void)
{
    for(unsigned i=0;i<6;i++) { chain_children[i].vt=nm_method_vt;chain_children[i].refs=5; }
    chain_records(cq_requested);chain_allow=1;
    nm_method_vt[2]=cq_begin;nm_method_vt[3]=cq_pass;nm_method_vt[4]=cq_dynamic;
    nm_method_vt[5]=cq_end_pass;nm_method_vt[6]=cq_end;nm_method_vt[7]=chain_set_next;
}

static void cq_case_begin(unsigned extra,unsigned optimized,unsigned step)
{
    (void)extra;(void)step;
    chain_refs();chain_allow=optimized;memset(cq_calls,0,sizeof cq_calls);
    cq_record_hash=fnv(records[0],ff_groups*sizeof records[0]);
}
static void cq_case_end(unsigned extra,unsigned optimized,unsigned step,const unsigned *pixels)
{
    (void)extra;chain_refs();
    unsigned covered=0;
    for(unsigned i=0;i<RT*RT;i++)covered+=pixels[i]!=(shadow_mode?PF_DEPTH_CLEAR:0);
    if(covered<=100)fprintf(stderr,"chain coverage=%u optimized=%u step=%u length=%u\n",covered,optimized,step,chain_count);
    REQUIRE(covered>100); /* Matching empty images are not a useful oracle. */
    REQUIRE(cq_record_hash==fnv(records[0],ff_groups*sizeof records[0]));
    REQUIRE(cq_calls[0]==ff_groups && cq_calls[1]==ff_groups && cq_calls[3]==ff_groups && cq_calls[4]==ff_groups);
    REQUIRE(cq_calls[2]>=ff_groups && cq_calls[2]<=GROUP_SIZE*ff_groups);
    for(unsigned i=0;i<7;i++)REQUIRE(!cq_phase[i] && !cq_saved[i]);
    unsigned all[4096],n=cq_snapshot(all,4096);
    /* Ordinary rendering leaves the last unit's palette in the effect;
     * established instancing leaves the leader's. Compare chain state here.
     * ff_snapshot still compares the entire native/effect palette around
     * every batch and replay, where exact restoration is required. */
    unsigned *state=all+n-CQ_CHAIN_WORDS;n=CQ_CHAIN_WORDS;
    if(!optimized) { memcpy(cq_reference,state,n*4);cq_reference_words=n; }
    else {
        REQUIRE(n==cq_reference_words);
        for(unsigned i=0;i<n;i++)if(state[i]!=cq_reference[i])
            fprintf(stderr,"chain final difference word=%u native=%08x candidate=%08x\n",i,cq_reference[i],state[i]);
        REQUIRE(!memcmp(state,cq_reference,n*4));
    }
    if(optimized && !step && !ff_allow_fallback)REQUIRE(bench_native_group_consumed()==ff_groups*(GROUP_SIZE-1));
}

static void cq_failed_begin(void)
{
    unsigned native[4096],candidate[4096],a=0,b=0;
    for(unsigned optimized=0;optimized<2;optimized++) {
        bench_fault_reset();bench_extra_mode(0);ng_frame_mode(optimized);chain_allow=optimized;
        if(!optimized)bench_fault_stock(1);
        ng_begin_fail=1;ng_current_group=0;
        unsigned palette[BONES*8]={0};
        material_fixture_sources(0);
        material_fixture_bone_values(palette,sizeof palette);
        HR(IDirect3DStateBlock9_Apply(outer));
        HR(IDirect3DDevice9_BeginScene(g_dev));
        ng_group_call(ng_target(optimized),ng_renderer,records[0],records[0]+sizeof records[0]);
        HR(IDirect3DDevice9_EndScene(g_dev));drain();
        REQUIRE(!ng_group_abi_errors && !bench_native_group_consumed());
        /* Native failed Begin retains owning links. Capture that exact state,
         * then let a successful native group replace and clear those links. */
        REQUIRE(chain_links[0] && g_method.refs==5);
        if(optimized)b=cq_snapshot(candidate,4096);else a=cq_snapshot(native,4096);
        ng_begin_fail=0;
        HR(IDirect3DDevice9_BeginScene(g_dev));
        ng_group_call(ng_target(optimized),ng_renderer,records[0],records[0]+sizeof records[0]);
        HR(IDirect3DDevice9_EndScene(g_dev));drain();chain_refs();
    }
    REQUIRE(a==b);
    for(unsigned i=0;i<a;i++)if(native[i]!=candidate[i])
        fprintf(stderr,"chain Begin failure state word=%u native=%08x candidate=%08x\n",i,native[i],candidate[i]);
    REQUIRE(!memcmp(native,candidate,a*4));
    puts("CHAIN BEGIN FAILURE: native retained links, state and references agree");
}

static void cq_base_controls(void)
{
    if(cq_first) {
        /* Negative cases target this chain fixture, not the independent
         * 136 single-method controls already required by every normal run. */
        if(!ff_corrupt_mode && !chain_broken && !cq_broken)ng_validate();
        cq_first=0;
    }
    cq_install();cq_failed_begin();
}
static void cq_validate(void)
{
    for(cq_requested=2;cq_requested<=6;cq_requested++) {
        ff_fault_cases=0;ff_groups=1;ff_allow_fallback=0;
        ff_validate();
        REQUIRE(VirtualFree(ff_forwarders,0,MEM_RELEASE));ff_forwarders=NULL;
        printf("CHAIN RECOVERY PASS: length=%u, GPU faults, partial fallback and following group\n",cq_requested);
    }
}
static void cq_setup(void)
{
    validate_retained=cq_validate;benchmark_controls_only=1;
    comparison_name="material-chain-recovery";
}
int main(int argc,char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    if(argc==12) { ff_corrupt_mode=(unsigned)atoi(argv[11]);ff_negative=ff_corrupt_mode==1;argc--; }
    const char *broken=getenv("PF_CHAIN_SCREEN_BROKEN");chain_broken=broken?(unsigned)atoi(broken):0;
    const char *child=getenv("PF_CHAIN_RECOVERY_BROKEN");cq_broken=child?(unsigned)atoi(child):0;
    REQUIRE(chain_broken<=3 && cq_broken<=2);ng_after_setup=cq_setup;
    return group_gpu_main(argc,argv);
}
