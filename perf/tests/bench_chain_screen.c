/* Offline gate screen: native chain walkers with controlled owning child
 * methods. No production dispatch or shader change is enabled by this file. */
#define PF_NATIVE_FX_MAIN chain_fx_main
#include "bench_native_fx_gpu.c"

static fake_method chain_children[6];
static fake_method *chain_links[7];
static unsigned chain_count=6,chain_allow,chain_broken,chain_begins,chain_sets;

int bench_chain_gate(unsigned count)
{
    return count==1 || (chain_allow && chain_broken!=3 && count>=2 && count<=6);
}

static unsigned chain_index(fake_method *method)
{
    if(method==&g_method)return 0;
    for(unsigned i=0;i<6;i++)if(method==chain_children+i)return i+1;
    REQUIRE(0);return 0;
}

static void __attribute__((thiscall)) chain_set_next(fake_method *self,fake_method *next)
{
    unsigned index=chain_index(self);
    REQUIRE(self->refs>=5);
    if(next) { chain_index(next);REQUIRE(next->refs>=6); }
    /* The native walker transfers an already AddRef'd by-value argument.
     * Keep that reference as the link, releasing the previous owned link. */
    if(chain_broken==2 && !next && chain_links[index])return;
    if(chain_links[index]) { REQUIRE(chain_links[index]->refs>5);chain_links[index]->refs--; }
    chain_links[index]=next;chain_sets++;
}

static void chain_refs(void)
{
    REQUIRE(g_method.refs==5);
    for(unsigned i=0;i<6;i++)REQUIRE(chain_children[i].refs==5);
    for(unsigned i=0;i<7;i++)REQUIRE(!chain_links[i]);
}

static int __attribute__((thiscall)) chain_begin(void *self,int *passes,int flags)
{
    fake_method *method=self;
    unsigned length=0;
    while(method) {
        unsigned index=chain_index(method);
        REQUIRE(++length<=6 && method->refs>5);
        method=chain_links[index];
    }
    REQUIRE(length==chain_count);chain_begins++;
    return nx_begin(self,passes,flags);
}

static void chain_records(unsigned count)
{
    REQUIRE(count>=1 && count<=6);chain_count=count;
    for(unsigned group=0;group<GROUPS;group++)for(unsigned i=0;i<GROUP_SIZE;i++) {
        unsigned *record=(unsigned *)(records[group]+i*PF_RECORD_SIZE);
        record[1]=count;record[2]=(unsigned)&g_method;
        for(unsigned j=1;j<6;j++)record[2+j]=j<count?(unsigned)&chain_children[j-1]:0;
    }
}

static void chain_frame(int candidate)
{
    chain_refs();chain_allow=candidate;chain_begins=chain_sets=0;
    nx_frame_mode(1); /* Both paths already have the installed bulk hooks. */
}

static void chain_check(int extra)
{
    pf_inst_counts counts;pf_inst_take(&counts);
    unsigned bulk=chain_allow || chain_count==1;
    REQUIRE(bench_leader_suppressed()==GROUPS);
    REQUIRE(bench_native_group_consumed()==(bulk?GROUPS*(GROUP_SIZE-1):0));
    if(!extra)REQUIRE(counts.meshes==GROUPS*GROUP_SIZE && counts.draws==fixture_batches(GROUP_SIZE)*GROUPS);
    REQUIRE(nx_static_calls==GROUPS && nx_bone_calls==GROUPS*2 && nx_dynamic_calls==GROUPS && nx_begins==GROUPS);
    REQUIRE(chain_begins==GROUPS && chain_sets==2*chain_count*GROUPS);
    chain_refs();
}

static void chain_render(unsigned group)
{
    ng_current_group=group;
    unsigned hash=fnv(records[group],sizeof records[group]);
    ng_group_call(ng_target(1),ng_renderer,records[group],records[group]+sizeof records[group]);
    REQUIRE(!ng_group_abi_errors && hash==fnv(records[group],sizeof records[group]));
    chain_refs();
}

static void chain_boundary(unsigned extra,unsigned candidate,unsigned *pixels)
{
    pose(extra);pf_inst_init(0);bench_extra_mode(extra);chain_frame(candidate);g_stock_draws=0;
    ng_current_group=0;
    /* Same model/first method, distinct final method: native must keep two
     * groups. This does not authorize merging on the first method alone. */
    for(unsigned i=0;i<GROUP_SIZE;i++) {
        unsigned *record=(unsigned *)(records[1]+i*PF_RECORD_SIZE);
        record[1+chain_count]=(unsigned)(chain_broken==1?&chain_children[chain_count-2]:&chain_children[5]);
    }
    unsigned hash=fnv(records[0],2*sizeof records[0]);
    HR(IDirect3DDevice9_SetVertexShader(g_dev,g_ea));
    HR(IDirect3DDevice9_SetVertexDeclaration(g_dev,g_decl));
    HR(IDirect3DDevice9_SetStreamSource(g_dev,0,g_vb,0,40));
    HR(IDirect3DDevice9_SetIndices(g_dev,g_ib));
    HR(IDirect3DDevice9_BeginScene(g_dev));
    HR(IDirect3DDevice9_Clear(g_dev,0,NULL,D3DCLEAR_TARGET | (shadow_mode?D3DCLEAR_ZBUFFER:0),
                             shadow_mode?0xffffffffu:0,1,0));
    ng_group_call(ng_target(1),ng_renderer,records[0],records[2]);
    HR(IDirect3DDevice9_EndScene(g_dev));drain();read_pixels(pixels);
    REQUIRE(!ng_group_abi_errors && chain_begins==2 && g_stock_draws==2);
    REQUIRE(bench_leader_suppressed()==2 && bench_native_group_consumed()==(candidate?2*(GROUP_SIZE-1):0));
    REQUIRE(hash==fnv(records[0],2*sizeof records[0]));chain_refs();
    chain_records(chain_count);bench_extra_mode(0);
}

static void chain_validate(void)
{
    static unsigned old[RT*RT],candidate[RT*RT];
    unsigned requested=chain_count,cases=0;
    /* The outer state block exists only after setup returns. Run the
     * inherited single-method controls here, then restore this workload. */
    chain_refs();chain_records(1);
    nm_method_vt[7]=ng_set_next;nm_method_vt[2]=nx_begin;
    group_render=ng_render;frame_mode=nx_frame_mode;frame_check=nx_frame_check;
    ng_validate();
    nm_method_vt[7]=chain_set_next;nm_method_vt[2]=chain_begin;
    group_render=chain_render;frame_mode=chain_frame;frame_check=chain_check;
    for(unsigned count=1;count<=6;count++) {
        chain_records(count);
        for(unsigned extra=0;extra<2;extra++) {
            unsigned a[4096],b[4096],na,nb;
            pose(extra);pair_optimized=0;frame_draw(extra,0,old);na=nx_snapshot(a,4096);
            pose(extra);pair_optimized=1;frame_draw(extra,0,candidate);nb=nx_snapshot(b,4096);
            nm_compare(old,candidate);REQUIRE(na==nb && !memcmp(a,b,na*4));cases++;
            if(count>1) {
                chain_boundary(extra,0,old);chain_boundary(extra,1,candidate);nm_compare(old,candidate);cases++;
            }
        }
    }
    chain_records(requested);pf_inst_init(0);
    printf("CHAIN CONTROLS: %u regular/extra pairs, lengths 1..6, owning links/clear, full-method boundaries, record/state/reference/ABI and pixels pass; child behavior controlled\n",cases);
}

static void chain_setup(void)
{
    for(unsigned i=0;i<6;i++) { chain_children[i].vt=nm_method_vt;chain_children[i].refs=5; }
    nm_method_vt[7]=chain_set_next;nm_method_vt[2]=chain_begin;
    chain_records(chain_count);
    group_render=chain_render;frame_mode=chain_frame;frame_check=chain_check;
    validate_retained=chain_validate;comparison_name="multi-method-bulk-gate";
    puts("CHAIN SCREEN: same installed hooks/leader/FX on both paths; only the method-count gate changes; no native child-subclass or live-coverage qualification");
}

int main(int argc,char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    REQUIRE(argc==12);
    chain_count=(unsigned)strtoul(argv[7],NULL,10);REQUIRE(chain_count>=1 && chain_count<=6);
    const char *broken=getenv("PF_CHAIN_SCREEN_BROKEN");chain_broken=broken?(unsigned)strtoul(broken,NULL,10):0;
    REQUIRE(chain_broken<=3);
    benchmark_controls_only=getenv("PF_CHAIN_SCREEN_CONTROLS")!=NULL;
    for(int i=7;i<argc;i++)argv[i]=argv[i+1];
    ng_after_setup=chain_setup;
    return chain_fx_main(argc-1,argv);
}
