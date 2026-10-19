/* Optimistic, fixture-only screen of caller readback barriers. This is not
 * a general D3D state mirror: one fixed effect shader, stable resources and
 * the exact bone-parameter/register mapping are known in advance.
 * Worker effect writes, state blocks, failures and resource destruction
 * would require a separate compatibility contract in a live game. */
#define PF_EXTERNAL_DRAW_MAIN unused_external_draw_main
#define material_fixture_bones known_fixture_bones
#include "bench_external_draw.c"
#undef material_fixture_bones
extern void material_fixture_bones(const void *,unsigned);
extern void material_fixture_bone_snapshot(void *,unsigned);

static IDirect3DDevice9Vtbl downstream,known_table;
static IDirect3DDevice9Vtbl *external_table;
static DWORD producer;
static unsigned known_enabled,poison_answers,poison_count;
static unsigned native_only;
static unsigned skipped_shader,skipped_constants,skipped_bindings;
static IDirect3DVertexShader9 *known_vs;
static IDirect3DVertexDeclaration9 *known_decl;
static IDirect3DIndexBuffer9 *known_ib;
static struct { IDirect3DVertexBuffer9 *vb; UINT offset,stride,frequency; } known_stream[2];
static float known_constants[256*4];
static float known_bones[256*4];

static int on_producer(void)
{
    return known_enabled && GetCurrentThreadId()==producer;
}

static HRESULT WINAPI known_get_vs(IDirect3DDevice9 *dev,IDirect3DVertexShader9 **out)
{
    if(!on_producer())return downstream.GetVertexShader(dev,out);
    REQUIRE(dev==g_dev && out);*out=known_vs;
    if(*out)IDirect3DVertexShader9_AddRef(*out);
    skipped_shader++;return D3D_OK;
}
static HRESULT WINAPI known_set_vs(IDirect3DDevice9 *dev,IDirect3DVertexShader9 *value)
{
    HRESULT hr=downstream.SetVertexShader(dev,value);
    if(on_producer() && SUCCEEDED(hr))known_vs=value;
    return hr;
}
static HRESULT WINAPI known_get_decl(IDirect3DDevice9 *dev,IDirect3DVertexDeclaration9 **out)
{
    if(!on_producer())return downstream.GetVertexDeclaration(dev,out);
    REQUIRE(dev==g_dev && out);*out=known_decl;
    if(*out)IDirect3DVertexDeclaration9_AddRef(*out);
    skipped_bindings++;return D3D_OK;
}
static HRESULT WINAPI known_set_decl(IDirect3DDevice9 *dev,IDirect3DVertexDeclaration9 *value)
{
    HRESULT hr=downstream.SetVertexDeclaration(dev,value);
    if(on_producer() && SUCCEEDED(hr))known_decl=value;
    return hr;
}
static HRESULT WINAPI known_get_indices(IDirect3DDevice9 *dev,IDirect3DIndexBuffer9 **out)
{
    if(!on_producer())return downstream.GetIndices(dev,out);
    REQUIRE(dev==g_dev && out);*out=known_ib;
    if(*out)IDirect3DIndexBuffer9_AddRef(*out);
    skipped_bindings++;return D3D_OK;
}
static HRESULT WINAPI known_set_indices(IDirect3DDevice9 *dev,IDirect3DIndexBuffer9 *value)
{
    HRESULT hr=downstream.SetIndices(dev,value);
    if(on_producer() && SUCCEEDED(hr))known_ib=value;
    return hr;
}
static HRESULT WINAPI known_get_stream(IDirect3DDevice9 *dev,UINT stream,
        IDirect3DVertexBuffer9 **out,UINT *offset,UINT *stride)
{
    if(!on_producer() || stream>=2)return downstream.GetStreamSource(dev,stream,out,offset,stride);
    REQUIRE(dev==g_dev && out && offset && stride);
    *out=known_stream[stream].vb;*offset=known_stream[stream].offset;*stride=known_stream[stream].stride;
    if(*out)IDirect3DVertexBuffer9_AddRef(*out);
    skipped_bindings++;return D3D_OK;
}
static HRESULT WINAPI known_set_stream(IDirect3DDevice9 *dev,UINT stream,
        IDirect3DVertexBuffer9 *value,UINT offset,UINT stride)
{
    HRESULT hr=downstream.SetStreamSource(dev,stream,value,offset,stride);
    if(on_producer() && stream<2 && SUCCEEDED(hr)) {
        known_stream[stream].vb=value;
        known_stream[stream].offset=offset;known_stream[stream].stride=stride;
    }
    return hr;
}
static HRESULT WINAPI known_get_frequency(IDirect3DDevice9 *dev,UINT stream,UINT *value)
{
    if(!on_producer() || stream>=2)return downstream.GetStreamSourceFreq(dev,stream,value);
    REQUIRE(dev==g_dev && value);*value=known_stream[stream].frequency;
    skipped_bindings++;return D3D_OK;
}
static HRESULT WINAPI known_set_frequency(IDirect3DDevice9 *dev,UINT stream,UINT value)
{
    HRESULT hr=downstream.SetStreamSourceFreq(dev,stream,value);
    if(on_producer() && stream<2 && SUCCEEDED(hr))known_stream[stream].frequency=value;
    return hr;
}
static HRESULT WINAPI known_get_constants(IDirect3DDevice9 *dev,UINT first,float *out,UINT count)
{
    if(on_producer() && count && first>=g_v->ir_reg &&
       first<=g_v->ir_reg+g_v->ir_count && count<=g_v->ir_reg+g_v->ir_count-first) {
        REQUIRE(dev==g_dev && out);
        memcpy(out,known_constants+first*4,count*16);
        if(poison_answers) { out[count*4-1]=0.375f;poison_count++; }
        skipped_constants++;return D3D_OK;
    }
    HRESULT hr=downstream.GetVertexShaderConstantF(dev,first,out,count);
    /* Full boundary reads stay synchronous and refresh the fixture window
     * after StateBlock::Apply, whose internal writes are not intercepted. */
    if(on_producer() && SUCCEEDED(hr) && first<256 && count<=256-first)
        memcpy(known_constants+first*4,out,count*16);
    return hr;
}
static HRESULT WINAPI known_set_constants(IDirect3DDevice9 *dev,UINT first,const float *value,UINT count)
{
    HRESULT hr=downstream.SetVertexShaderConstantF(dev,first,value,count);
    if(on_producer() && SUCCEEDED(hr) && first<256 && count<=256-first)
        memcpy(known_constants+first*4,value,count*16);
    return hr;
}

static void known_begin(unsigned group)
{
    material_fixture_begin(group);
    /* The fixture selected and byte-verified this exact shader at setup.
     * General effect evaluation cannot be replaced by this assignment. */
    if(known_enabled) {
        known_vs=g_ea;
        memcpy(known_constants+g_v->roles[PF_ROLE_BONES]*4,known_bones,g_v->ea_bones*32);
    }
}

void known_fixture_bones(const void *value,unsigned bytes)
{
    material_fixture_bones(value,bytes);
    if(known_enabled) {
        REQUIRE(bytes<=g_v->ea_bones*32);
        memcpy(known_bones,value,bytes);
        /* The selected fixture effects upload their entire bone parameter
         * on CommitChanges, including entries beyond this mesh's six bones.
         * This mapping is known only for these byte-verified variants. */
        memcpy(known_constants+g_v->roles[PF_ROLE_BONES]*4,known_bones,g_v->ea_bones*32);
    }
}

static void select_mode(unsigned mode)
{
    REQUIRE(mode<4);external_idle(0);
    known_enabled=0;g_dev->lpVtbl=external_table;
    unsigned threaded=mode==1 || mode==2;
    if(external_toggle)external_toggle(threaded);
    external_idle(threaded);
    if(mode<2)return;
    /* Borrow only objects held for the complete lifetime of this fixture.
     * Each public getter still returns a real owning COM reference. */
    HR(backend.GetVertexShader(g_dev,&known_vs));
    if(known_vs)IDirect3DVertexShader9_Release(known_vs);
    HR(backend.GetVertexDeclaration(g_dev,&known_decl));
    if(known_decl)IDirect3DVertexDeclaration9_Release(known_decl);
    HR(backend.GetIndices(g_dev,&known_ib));
    if(known_ib)IDirect3DIndexBuffer9_Release(known_ib);
    for(unsigned s=0;s<2;s++) {
        HR(backend.GetStreamSource(g_dev,s,&known_stream[s].vb,&known_stream[s].offset,&known_stream[s].stride));
        if(known_stream[s].vb)IDirect3DVertexBuffer9_Release(known_stream[s].vb);
        HR(backend.GetStreamSourceFreq(g_dev,s,&known_stream[s].frequency));
    }
    HR(backend.GetVertexShaderConstantF(g_dev,0,known_constants,256));
    REQUIRE(g_v->roles[PF_ROLE_BONES]>=0 &&
            (unsigned)g_v->roles[PF_ROLE_BONES]+g_v->ea_bones*2<=256);
    material_fixture_bone_snapshot(known_bones,g_v->ea_bones*32);
    external_idle(threaded);known_enabled=1;g_dev->lpVtbl=&known_table;
}

/* Bypass both the external DLL and our optimistic replies. The existing
 * frame check uses public getters and cannot validate their own answers. */
static void actual_bindings(unsigned out[11])
{
    IDirect3DVertexShader9 *vs=NULL;IDirect3DVertexDeclaration9 *decl=NULL;
    IDirect3DIndexBuffer9 *ib=NULL;unsigned n=0;
    external_idle(0);
    HR(backend.GetVertexShader(g_dev,&vs));out[n++]=(unsigned)vs;
    HR(backend.GetVertexDeclaration(g_dev,&decl));out[n++]=(unsigned)decl;
    HR(backend.GetIndices(g_dev,&ib));out[n++]=(unsigned)ib;
    if(vs)IDirect3DVertexShader9_Release(vs);
    if(decl)IDirect3DVertexDeclaration9_Release(decl);
    if(ib)IDirect3DIndexBuffer9_Release(ib);
    for(unsigned s=0;s<2;s++) {
        IDirect3DVertexBuffer9 *vb=NULL;
        HR(backend.GetStreamSource(g_dev,s,&vb,&out[n+1],&out[n+2]));out[n]=(unsigned)vb;n+=3;
        HR(backend.GetStreamSourceFreq(g_dev,s,&out[n++]));
        if(vb)IDirect3DVertexBuffer9_Release(vb);
    }
    REQUIRE(n==11);
}

static unsigned references[3][RT*RT],states[3][DRAW_STATE_WORDS],bindings[3][11],sizes[3];
static void check_cases(unsigned stage)
{
    for(unsigned mode=0;mode<4;mode++)for(unsigned c=0;c<3;c++) {
        if(native_only && (mode==1 || mode==2))continue;
        static unsigned pixels[RT*RT],state[DRAW_STATE_WORDS];unsigned binding[11];
        select_mode(mode);image_case(c,pixels);
        unsigned n=actual_state(state);actual_bindings(binding);
        unsigned different=0;for(unsigned i=0;i<RT*RT;i++)different+=pixels[i]!=references[c][i];
        int equal=n==sizes[c] && !memcmp(state,states[c],n*4) && !memcmp(binding,bindings[c],sizeof binding);
        printf("CHECK: stage=%u mode=%u case=%u different_pixels=%u state_equal=%d hash=%08X\n",
               stage,mode,c,different,equal,hash_words(pixels,RT*RT));
        if(!equal) {
            unsigned shown=0;
            for(unsigned i=0;i<n && i<sizes[c] && shown<8;i++)if(state[i]!=states[c][i]) {
                printf("STATE DIFFERENCE: word=%u actual=%08X expected=%08X\n",i,state[i],states[c][i]);shown++;
            }
            for(unsigned i=0;i<11;i++)if(binding[i]!=bindings[c][i])
                printf("BINDING DIFFERENCE: word=%u actual=%08X expected=%08X\n",i,binding[i],bindings[c][i]);
        }
        REQUIRE(!different && equal);
    }
}

static unsigned drains_now(void)
{
    char detail[8192]={0};unsigned value;external_stats4(detail,sizeof detail);
    char *d=strstr(detail,"drains=");REQUIRE(d && sscanf(d,"drains=%u",&value)==1);return value;
}

static int native_benchmark(void)
{
    double saving[24];
    puts("NATIVE: external DLL not loaded; mode 0 unhooked, mode 3 fixed-effect replies only");
    puts("round,native_caller_ms,known_caller_ms,native_complete_ms,known_complete_ms,saved_complete_ms");
    for(unsigned r=0;r<32;r++) {
        double caller[2]={0},complete[2]={0};REQUIRE(bench_no_game());
        for(unsigned j=0;j<2;j++) {
            unsigned m=j^(r&1);select_mode(m?3:0);
            for(unsigned rep=0;rep<4;rep++) {
                material_fixture_epoch((r+rep)&1);material_fixture_clock((r*4+rep)*33);
                double a,b,c;pair(0,&a,&b,&c,NULL);
                caller[m]+=c/4;complete[m]+=pair_completed_ms/4;
            }
        }
        if(r<8)continue;
        saving[r-8]=complete[0]-complete[1];
        printf("native_sample,%u,%.4f,%.4f,%.4f,%.4f,%.4f\n",r-8,
               caller[0],caller[1],complete[0],complete[1],saving[r-8]);
    }
    check_cases(1);qsort(saving,24,sizeof(double),compare_double);
    printf("NATIVE SUMMARY: %s saved_complete_median=%.4f ms\n",shadow_mode?"shadow":"lit",(saving[11]+saving[12])/2);
    select_mode(0);REQUIRE(bench_no_game());return 0;
}

int main(int argc,char **argv)
{
    REQUIRE(argc==5 && bench_no_game());producer=GetCurrentThreadId();
    REQUIRE(!strcmp(argv[3],"lit") || !strcmp(argv[3],"shadow"));
    const char *slash=strrchr(argv[2],'/'),*backslash=strrchr(argv[2],'\\');
    if(backslash && (!slash || backslash>slash))slash=backslash;
    REQUIRE(slash && (unsigned)(slash-argv[2])+1+sizeof "d3dx9_27.dll"<=sizeof material_runtime);
    memcpy(material_runtime,argv[2],(size_t)(slash-argv[2])+1);
    strcpy(material_runtime+(slash-argv[2])+1,"d3dx9_27.dll");
    material_setup=effect_setup;material_begin=known_begin;
    material_end=material_fixture_end;pose_adjust=spread;
    baseline_bulk_enabled=1;dispatch_scope_begin=bench_scope_bulk_begin;
    dispatch_scope_end=bench_scope_bulk_end;
    setup(argc,argv);backend=*g_dev->lpVtbl;pf_timing_set_detail(1);
    for(unsigned c=0;c<3;c++) {
        image_case(c,references[c]);sizes[c]=actual_state(states[c]);actual_bindings(bindings[c]);
    }
    unsigned covered=0,moved=0,changed=0;
    for(unsigned i=0;i<RT*RT;i++) {
        covered+=references[0][i]!=(shadow_mode?PF_DEPTH_CLEAR:0);
        moved+=references[0][i]!=references[1][i];changed+=references[1][i]!=references[2][i];
    }
    REQUIRE(covered>1000 && moved>100 && changed>100);
    printf("ORACLE: covered=%u fresh_pose=%u changed_material=%u hashes=%08X/%08X/%08X\n",
           covered,moved,changed,hash_words(references[0],RT*RT),hash_words(references[1],RT*RT),hash_words(references[2],RT*RT));
    native_only=!strcmp(argv[4],"native");
    if(!native_only) {
        void *effect=NULL;unsigned technique,block;material_fixture_native(&effect,&technique,&block);REQUIRE(effect);
        HMODULE module=LoadLibraryA(argv[4]);REQUIRE(module);
        external_install_fn install=(external_install_fn)(void*)GetProcAddress(module,"AotrRtTestInstall");
        external_toggle=(external_toggle_fn)(void*)GetProcAddress(module,"AotrRtTestToggle");
        external_stats=(external_stats_fn)(void*)GetProcAddress(module,"AotrRtTestStats");
        external_stats4=(external_stats_fn)(void*)GetProcAddress(module,"AotrRtTestStatsV4");
        REQUIRE(install && external_toggle && external_stats && external_stats4 && install(g_dev,effect)==1);
    } else REQUIRE(!GetModuleHandleA("bfme2_accel.new.dll") && !GetModuleHandleA("bfme2_accel.dll"));
    external_table=g_dev->lpVtbl;downstream=*external_table;known_table=downstream;
    known_table.GetVertexShader=known_get_vs;known_table.SetVertexShader=known_set_vs;
    known_table.GetVertexDeclaration=known_get_decl;known_table.SetVertexDeclaration=known_set_decl;
    known_table.GetIndices=known_get_indices;known_table.SetIndices=known_set_indices;
    known_table.GetStreamSource=known_get_stream;known_table.SetStreamSource=known_set_stream;
    known_table.GetStreamSourceFreq=known_get_frequency;known_table.SetStreamSourceFreq=known_set_frequency;
    known_table.GetVertexShaderConstantF=known_get_constants;known_table.SetVertexShaderConstantF=known_set_constants;
    check_cases(0);
    /* A bogus cached answer must fail the independent post-frame state
     * oracle even if the overwritten register is unused by this image. */
    {
        static unsigned pixels[RT*RT],state[DRAW_STATE_WORDS];
        select_mode(2);poison_answers=1;image_case(0,pixels);poison_answers=0;
        unsigned n=actual_state(state);
        REQUIRE(poison_count && n==sizes[0] && memcmp(state,states[0],n*4));
        printf("NEGATIVE: poisoned_constant_answer_detected=1 count=%u\n",poison_count);
    }
    if(native_only)return native_benchmark();
    for(unsigned mode=0;mode<4;mode++) {
        select_mode(mode);skipped_shader=skipped_constants=skipped_bindings=0;
        unsigned before=drains_now();double a,b,c;pair(0,&a,&b,&c,NULL);external_idle(mode==1 || mode==2);
        printf("DRAINS: mode=%u delta=%u skipped_shader=%u skipped_constants=%u skipped_bindings=%u\n",
               mode,drains_now()-before,skipped_shader,skipped_constants,skipped_bindings);
    }
    puts("LIMITS: optimistic fixed-effect/state screen; no general resource lifetime/failure/Reset contract, native sources, terrain, reflections, Present or gameplay FPS");
    puts("round,off_caller_ms,threaded_caller_ms,combined_caller_ms,known_only_caller_ms,off_complete_ms,threaded_complete_ms,combined_complete_ms,known_only_complete_ms,combined_vs_threaded_ms,combined_vs_off_ms,combined_vs_known_only_ms");
    const unsigned order[8][4]={{0,1,2,3},{1,2,3,0},{2,3,0,1},{3,0,1,2},
                                {3,2,1,0},{2,1,0,3},{1,0,3,2},{0,3,2,1}};
    double saving[24],overall[24],worker[24];
    for(unsigned r=0;r<32;r++) {
        double caller[4]={0},complete[4]={0};REQUIRE(bench_no_game());
        for(unsigned j=0;j<4;j++) {
            unsigned mode=order[r%8][j];select_mode(mode);
            for(unsigned rep=0;rep<4;rep++) {
                material_fixture_epoch((r+rep)&1);material_fixture_clock((r*4+rep)*33);
                double a,b,c;pair(0,&a,&b,&c,NULL);
                caller[mode]+=c/4;complete[mode]+=pair_completed_ms/4;external_idle(mode==1 || mode==2);
            }
        }
        if(r<8)continue;
        saving[r-8]=complete[1]-complete[2];overall[r-8]=complete[0]-complete[2];
        worker[r-8]=complete[3]-complete[2];
        printf("sample,%u,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",r-8,
               caller[0],caller[1],caller[2],caller[3],complete[0],complete[1],complete[2],complete[3],
               saving[r-8],overall[r-8],worker[r-8]);
    }
    check_cases(1);
    qsort(saving,24,sizeof(double),compare_double);qsort(overall,24,sizeof(double),compare_double);
    qsort(worker,24,sizeof(double),compare_double);
    printf("SUMMARY: %s combined_vs_threaded_median=%.4f combined_vs_off_median=%.4f combined_vs_known_only_median=%.4f ms\n",
           shadow_mode?"shadow":"lit",(saving[11]+saving[12])/2,(overall[11]+overall[12])/2,(worker[11]+worker[12])/2);
    select_mode(0);REQUIRE(bench_no_game());return 0;
}
