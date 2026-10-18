/* Full-effect retained queue screen, never linked into the plugin. */
#define PF_RETAINED_EMBED
#include "bench_retained.c"

extern void material_fixture_init(IDirect3DDevice9 *, const void *, unsigned, int, const char *);
extern void material_fixture_begin(unsigned);
extern void material_fixture_end(void);
extern void material_fixture_bones(const void *, unsigned);
extern void material_fixture_epoch(unsigned);
extern void material_fixture_churn(unsigned);
extern unsigned material_fixture_key(unsigned,unsigned char *,unsigned);
extern void material_fixture_start(void);
extern void material_fixture_open(void);
extern void material_fixture_close(void);
extern void material_fixture_verify_end(unsigned);
extern void material_fixture_end_counts(unsigned *,unsigned *);
extern void material_fixture_clock_probe(void);
extern void material_fixture_clock(unsigned);
extern void material_fixture_filter(unsigned);
extern void material_fixture_filter_check(void);
extern void bench_filter_counts(unsigned long long *,unsigned long long *);
extern void bench_filter_clear(void);
extern HRESULT material_fixture_mask(IDirect3DDevice9 *);
extern unsigned material_fixture_state(IDirect3DDevice9 *,unsigned *,unsigned);
extern void bench_retained_material_mask(HRESULT (*)(IDirect3DDevice9 *));
extern void bench_retained_markers(void (*)(void),void (*)(void));
extern void bench_retained_bulk_markers(int (*)(void),int (*)(unsigned));
extern int bench_scope_bulk_begin(void),bench_scope_bulk_end(unsigned),bench_scope_bulk_check(void);
extern int bench_retained_boundary_begin(unsigned),bench_retained_boundary_end(void);
extern int bench_retained_boundary_negative(unsigned);
extern int bench_retained_boundary_bindings(unsigned);
extern void bench_retained_boundary_config(unsigned,unsigned (*)(IDirect3DDevice9 *,unsigned *,unsigned));
extern void bench_retained_boundary_counts(unsigned *,unsigned *);
static unsigned char material_keys[GROUPS][8192];
static unsigned material_sizes[GROUPS];
static unsigned persistent_churn;
static unsigned persistent_hits,persistent_misses;
static unsigned preserve_lifecycle;
static unsigned restore_group_state;
static unsigned advancing_clock;
static unsigned state_filter_mode;
static void filter_frame(int optimized)
{
    /* A frame boundary performs unobserved native state writes/restoration.
     * No mirror entries survive it, including when returning to baseline. */
    material_fixture_filter(optimized?state_filter_mode:0);
}
static void accounting_init(void) {}
static void check_bulk_accounting(void)
{
    /* Establish the main render thread through its normal entry hook. */
    pf_timing_set_next(PF_DISPLAY_DRAW,accounting_init);
    ((void (*)(void))pf_enter_stubs[PF_DISPLAY_DRAW])();
    pf_timing_set_next(PF_DISPLAY_DRAW,retained_workload);
    REQUIRE(bench_scope_bulk_check());
    puts("BULK ACCOUNTING: logical counts, pass totals, detail/extra modes, stack capacity and interval checks passed");
}
static char material_runtime[MAX_PATH];
static char replay_event[320];

static void replay_marker_begin(void)
{
    char label[256];
    local_mbcopy((unsigned char *)label,(const unsigned char *)"Rendering mesh\tFXShader\t");
    local_mbcat((unsigned char *)label,(const unsigned char *)"BENCH_SYNTHETIC_UNIT_MODEL");
    ((event_begin_fn)pf_enter_stubs[PF_EVENT_BEGIN])(replay_event,label,"MeshFXShader",0);
}
static void replay_marker_end(void)
{
    ((event_end_fn)pf_enter_stubs[PF_EVENT_END])(replay_event);
}

static void __attribute__((thiscall)) effect_render(fake_mesh *mesh, fake_method *method, int count)
{
    unsigned bones[BONES*8];
    fake_container *c=*(fake_container **)(mesh->b+PF_MESH_CONTAINER);
    const unsigned char *piv=*(unsigned char **)((unsigned char *)c->htree+0x14);
    (void)count;
    for(unsigned j=0;j<BONES;j++) {
        memcpy(bones+8*j,piv+g_bonemap[j]*0x58+0x30,28);
        bones[8*j+7]=0;
    }
    material_fixture_bones(bones,sizeof bones);
    site_draw(GRID*GRID*2);
    method->refs--;
}

static void effect_setup(void)
{
    unsigned char code[32768];
    unsigned n=sizeof code, hash=2166136261u, found=0;
    IDirect3DVertexShader9 *vs=NULL;
    material_fixture_init(g_dev,g_fx,g_fx_size,shadow_mode,material_runtime);
    material_fixture_begin(0);
    HR(IDirect3DDevice9_GetVertexShader(g_dev,&vs)); REQUIRE(vs);
    HR(IDirect3DVertexShader9_GetFunction(vs,NULL,&n)); REQUIRE(n<=sizeof code);
    HR(IDirect3DVertexShader9_GetFunction(vs,code,&n));
    for(unsigned i=0;i<n;i++) hash=(hash^code[i])*16777619u;
    for(unsigned i=0;i<PF_INST_VARIANTS;i++)
        if(pf_inst_variants[i].ea_hash==hash && pf_inst_variants[i].ea_size==n) {
            g_v=&pf_inst_variants[i]; found++;
        }
    REQUIRE(found==1);
    g_ea=vs;
    material_fixture_end();
    printf("Bound effect shader: %08X, variant %u\n",hash,g_v->ea_index);
    pf_timing_set_next(PF_INST_MESH,effect_render);
    bench_retained_material_mask(material_fixture_mask);
    material_fixture_verify_end(preserve_lifecycle);
}

static void spread(float *pivot,unsigned mesh)
{
    unsigned group=mesh/GROUP_SIZE;
    pivot[4]+=3.f*(group%18);
    pivot[5]+=3.f*(group/18);
}

static void validate_materials(void)
{
    static unsigned old[RT*RT], stock[RT*RT], fresh[RT*RT], stale[RT*RT];
    double regular,extra,total;
    unsigned changed=0,wrong=0,covered=0;
    material_fixture_clock_probe();
    pair(1,&regular,&extra,&total,old);
    if(restore_group_state) {
        REQUIRE(bench_retained_boundary_negative(0));
        REQUIRE(bench_retained_boundary_bindings(0));
    }
    material_fixture_epoch(1);
    pair(0,&regular,&extra,&total,stock);
    /* Negative control: replay the previous material generation. */
    {
        unsigned (*saved_extra)(void)=cached_extra;
        cached_extra=NULL; /* Deliberately bypass invalidation for this control. */
        pose(1); frame_draw(1,1,stale);
        cached_extra=saved_extra;
    }
    pair(1,&regular,&extra,&total,fresh);
    for(unsigned i=0;i<RT*RT;i++) {
        covered+=stock[i]!=(shadow_mode?PF_DEPTH_CLEAR:0);
        changed+=stock[i]!=old[i]; wrong+=stock[i]!=stale[i];
        REQUIRE(shadow_mode?pf_depth_diff(stock[i],fresh[i])<=PF_DEPTH_TOLERANCE:stock[i]==fresh[i]);
        if(shadow_mode) REQUIRE(stock[i]<=PF_DEPTH_CLEAR && fresh[i]<=PF_DEPTH_CLEAR);
    }
    REQUIRE(covered>1000 && changed>100 && wrong>100);
    printf("MATERIAL INVALIDATION: covered=%u changed=%u stale=%u; recapture matches\n",covered,changed,wrong);
    if(cached_regular) {
        static const unsigned changes[]={0,10,100};
        for(unsigned trial=0;trial<3;trial++) {
            material_fixture_churn(changes[trial]);
            for(unsigned phase=0;phase<3;phase++) {
                unsigned h=persistent_hits,m=persistent_misses;
                material_fixture_epoch(phase&1);
                pair(0,&regular,&extra,&total,stock);
                pair(1,&regular,&extra,&total,fresh);
                for(unsigned i=0;i<RT*RT;i++)
                    REQUIRE(shadow_mode?pf_depth_diff(stock[i],fresh[i])<=PF_DEPTH_TOLERANCE:stock[i]==fresh[i]);
                /* Also compare regular images, so a valid extra cannot
                 * hide a bad mixed hit/miss regular frame. */
                pose(0); retain_now=0; frame_draw(0,0,stock);
                retain_now=1; frame_draw(0,0,fresh); retain_now=0;
                for(unsigned i=0;i<RT*RT;i++)
                    REQUIRE(shadow_mode?pf_depth_diff(stock[i],fresh[i])<=PF_DEPTH_TOLERANCE:stock[i]==fresh[i]);
                REQUIRE(persistent_hits-h+persistent_misses-m==(2+preserve_lifecycle)*GROUPS);
                if(phase) {
                    unsigned expected_misses=changes[trial]==100?GROUPS:changes[trial]==10?30:0;
                    REQUIRE(persistent_misses-m==expected_misses);
                }
            }
        }
        puts("PERSISTENT OUTPUT: regular and extra match at zero, partial and complete invalidation; hit/miss counts match");
        material_fixture_churn(100);
    }
    {
        unsigned before[512],after[512],n=material_fixture_state(g_dev,before,512);
        pose(1); frame_draw(1,1,NULL);
        REQUIRE(material_fixture_state(g_dev,after,512)==n && !memcmp(before,after,n*4));
        if(cached_regular) {
            pose(0); retain_now=1; frame_draw(0,0,NULL); retain_now=0;
            REQUIRE(material_fixture_state(g_dev,after,512)==n && !memcmp(before,after,n*4));
        }
        puts("MATERIAL STATE: all recorded render/sampler/texture states restored");
    }
    material_fixture_epoch(0);
    if(preserve_lifecycle) {
        unsigned ends,changed;
        unsigned refs=g_method.refs;
        pair(1,&regular,&extra,&total,fresh);
        unsigned hits=persistent_hits,misses=persistent_misses;
        material_fixture_epoch(1);
        pose(1); frame_draw(1,0,stock); frame_draw(1,1,fresh);
        for(unsigned i=0;i<RT*RT;i++)
            REQUIRE(shadow_mode?pf_depth_diff(stock[i],fresh[i])<=PF_DEPTH_TOLERANCE:stock[i]==fresh[i]);
        REQUIRE(persistent_misses-misses==GROUPS && persistent_hits==hits);
        frame_draw(1,1,fresh);
        REQUIRE(persistent_misses-misses==GROUPS && persistent_hits-hits==GROUPS);
        for(unsigned i=0;i<RT*RT;i++)
            REQUIRE(shadow_mode?pf_depth_diff(stock[i],fresh[i])<=PF_DEPTH_TOLERANCE:stock[i]==fresh[i]);
        REQUIRE(g_method.refs==refs);
        puts("EXTRA INVALIDATION: changed inputs fall back before drawing, then warm hits match");
        material_fixture_epoch(0);
        material_fixture_end_counts(&ends,&changed);
        REQUIRE(ends>1000 && changed>1000);
        printf("EFFECT END: %u group boundaries restore entry render states; %u changed states exercised\n",ends,changed);
    }
    if(advancing_clock) {
        material_fixture_clock(1000);
        pair(1,&regular,&extra,&total,fresh);
        unsigned hits=persistent_hits,misses=persistent_misses;
        material_fixture_clock(1033);
        pair(0,&regular,&extra,&total,stock);
        pair(1,&regular,&extra,&total,fresh);
        REQUIRE(persistent_misses-misses==GROUPS && persistent_hits-hits==GROUPS);
        for(unsigned i=0;i<RT*RT;i++)
            REQUIRE(shadow_mode?pf_depth_diff(stock[i],fresh[i])<=PF_DEPTH_TOLERANCE:stock[i]==fresh[i]);
        puts("ADVANCING CLOCK: all regular groups invalidate; same-time extra hits preserve output");
    }
    if(restore_group_state) {
        unsigned checks,changed;
        bench_retained_boundary_counts(&checks,&changed);
        REQUIRE(checks>1000 && changed>1000);
        printf("GROUP STATE: %u full input/output state checks; %u changed boundaries; omitted-restore control rejected; binding poison restored\n",checks,changed);
    }
}

static unsigned persistent_regular(void)
{
    unsigned hits=0;
    for(unsigned group=0;group<GROUPS;group++) {
        unsigned char key[8192];
        unsigned n=material_fixture_key(group,key,sizeof key);
        REQUIRE(n && n<=sizeof key);
        current_records=records[group];
        if(n==material_sizes[group] && !memcmp(key,material_keys[group],n)) {
            if(preserve_lifecycle) material_fixture_open();
            if(restore_group_state) REQUIRE(bench_retained_boundary_begin(group));
            REQUIRE(bench_replay(group,current_records,current_records+sizeof records[0]));
            if(preserve_lifecycle) material_fixture_close();
            if(restore_group_state) REQUIRE(bench_retained_boundary_end());
            hits++; persistent_hits++;
            continue;
        }
        persistent_misses++;
        /* Replay owns its region and leaves instanced geometry bound. A
         * fallback boundary must reconcile it before a stock leader. */
        HR(IDirect3DDevice9_SetVertexDeclaration(g_dev,g_decl));
        HR(IDirect3DDevice9_SetStreamSourceFreq(g_dev,0,1));
        HR(IDirect3DDevice9_SetStreamSourceFreq(g_dev,1,1));
        HR(IDirect3DDevice9_SetStreamSource(g_dev,0,g_vb,0,40));
        HR(IDirect3DDevice9_SetStreamSource(g_dev,1,NULL,0,0));
        HR(IDirect3DDevice9_SetIndices(g_dev,g_ib));
        material_fixture_start();
        *(unsigned char **)(frame_ptr+PF_FRAME_GROUP_END)=current_records+sizeof records[0];
        for(unsigned i=0;i<GROUP_SIZE;i++) {
            one_member(i);
            if(!i) REQUIRE(bench_retain(group));
        }
        material_fixture_end();
        memcpy(material_keys[group],key,n); material_sizes[group]=n;
    }
    return hits;
}

static void sample_epoch(unsigned round,unsigned repetition)
{
    material_fixture_verify_end(0); /* Correctness-only reads, outside timing. */
    if(restore_group_state) bench_retained_boundary_config(0,material_fixture_state);
    material_fixture_churn(persistent_churn);
    material_fixture_epoch((round*FRAMES_PER_SAMPLE+repetition)&1);
    if(advancing_clock) material_fixture_clock((round*FRAMES_PER_SAMPLE+repetition)*33);
}

int main(int argc,char **argv)
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
    validate_retained=validate_materials;
    if(argc>5) {
        REQUIRE(!strcmp(argv[5],"0") || !strcmp(argv[5],"10") || !strcmp(argv[5],"100"));
        persistent_churn=(unsigned)atoi(argv[5]);
        cached_regular=persistent_regular;
        prepare_sample=sample_epoch;
    }
    if(argc>6) {
        if(!strcmp(argv[6],"state-forward") || !strcmp(argv[6],"state-filter") ||
           !strcmp(argv[6],"state-filter-bulk")) {
            REQUIRE(argc==7);
            state_filter_mode=!strcmp(argv[6],"state-forward")?1:2;
            advancing_clock=1;
            preserve_lifecycle=1;
            dispatch_only=1;
            dispatch_bulk_enabled=!strcmp(argv[6],"state-filter-bulk");
            cached_regular=cached_extra=NULL;
            validate_retained=material_fixture_filter_check;
            validate_markers=1;
            dispatch_scope_begin=bench_scope_bulk_begin;
            dispatch_scope_end=bench_scope_bulk_end;
            validate_accounting=check_bulk_accounting;
            frame_mode=filter_frame;
            comparison_name=argv[6];
            printf("STATE FILTER MODE: %u, bulk=%d; real effect lifecycle, advancing clock, per-group and per-frame mirror invalidation\n",
                   state_filter_mode,dispatch_bulk_enabled);
        } else {
        advancing_clock=!strcmp(argv[6],"lifecycle-bulk-clock") || !strcmp(argv[6],"lifecycle-bulk-state-clock");
        REQUIRE(argc==7 && (advancing_clock || !strcmp(argv[6],"lifecycle") || !strcmp(argv[6],"lifecycle-markers") || !strcmp(argv[6],"lifecycle-bulk-markers") || !strcmp(argv[6],"lifecycle-bulk-only") || !strcmp(argv[6],"lifecycle-bulk-state")));
        preserve_lifecycle=1;
        cached_extra=persistent_regular;
        if(strcmp(argv[6],"lifecycle-bulk-only"))
            puts("LIFECYCLE: real Begin/End on each hit; prepare and validate inputs again in extras");
        if(!strcmp(argv[6],"lifecycle-markers")) {
            validate_markers=1;
            bench_retained_markers(replay_marker_begin,replay_marker_end);
            puts("MARKERS: identical per-mesh labels, entry/exit hooks, copies and scope accounting on replay");
        } else if(advancing_clock || !strcmp(argv[6],"lifecycle-bulk-markers") || !strcmp(argv[6],"lifecycle-bulk-state")) {
            validate_markers=1;
            bench_retained_bulk_markers(bench_scope_bulk_begin,bench_scope_bulk_end);
            validate_accounting=check_bulk_accounting;
            puts("BULK MARKERS: logical mesh counts and one measured aggregate interval; no native debug callbacks in fixture");
            if(!strcmp(argv[6],"lifecycle-bulk-state") || !strcmp(argv[6],"lifecycle-bulk-state-clock")) {
                restore_group_state=1;
                bench_retained_boundary_config(1,material_fixture_state);
                puts("GROUP STATE: capture and restore every replay boundary, including all palette registers");
            }
            if(advancing_clock) puts("ADVANCING CLOCK: Time advances 33 ms per regular/extra pair, same time within the pair");
        } else if(!strcmp(argv[6],"lifecycle-bulk-only")) {
            dispatch_only=1;
            cached_regular=cached_extra=NULL;
            validate_retained=NULL;
            validate_markers=1;
            dispatch_scope_begin=bench_scope_bulk_begin;
            dispatch_scope_end=bench_scope_bulk_end;
            validate_accounting=check_bulk_accounting;
            puts("BULK ONLY: original effect/leader path each draw, aggregate accounting after second member; no retained cache");
        }
        }
    }
    if(cached_regular) printf("PERSISTENT material cache, %u percent selected groups alternate inputs each pair\n",persistent_churn);
    int result=retained_benchmark(argc,argv);
    if(cached_regular) printf("PERSISTENT hits=%u misses=%u (includes validation/warmup)\n",persistent_hits,persistent_misses);
    if(state_filter_mode) {
        unsigned long long calls,sent;
        bench_filter_counts(&calls,&sent);
        printf("STATE FILTER TOTAL: %llu/%llu effect device calls forwarded (validation and warmup included)\n",sent,calls);
        material_fixture_filter(0);
    }
    bench_filter_clear();
    return result;
}
