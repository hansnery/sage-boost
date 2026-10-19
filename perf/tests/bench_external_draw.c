/* Isolated evaluation of an explicitly supplied external DLL. All fixture
 * code is ours; no external implementation is compiled or linked here. */
#define PF_MATERIAL_MAIN unused_material_screen_main
#include "bench_material_queue.c"

extern void material_fixture_native(void **,unsigned *,unsigned *);
typedef int (__cdecl *external_install_fn)(void *,void *);
typedef void (__cdecl *external_toggle_fn)(int);
typedef void (__cdecl *external_stats_fn)(char *,int);
static external_toggle_fn external_toggle;
static external_stats_fn external_stats,external_stats4;
static IDirect3DDevice9Vtbl backend;
#define DRAW_STATE_WORDS 1300

static void external_idle(int active)
{
    char brief[1024]={0},detail[8192]={0};unsigned submitted,executed;
    drain();
    if(!external_stats)return;
    external_stats(brief,sizeof brief);external_stats4(detail,sizeof detail);
    char *s=strstr(detail,"seq=");
    REQUIRE(s && sscanf(s,"seq=%u exec=%u",&submitted,&executed)==2);
    REQUIRE(submitted==executed && strstr(brief,"queued=0"));
    if(active)REQUIRE(strstr(brief,"active=1") && executed);
}

/* This state observation bypasses the candidate's mirrored getters. Image
 * comparisons below additionally observe actual geometry/shaders/textures. */
static unsigned actual_state(unsigned out[DRAW_STATE_WORDS])
{
    unsigned n=0;DWORD v;
    external_idle(0);
    HR(backend.GetVertexShaderConstantF(g_dev,0,(float *)(out+n),256));n+=1024;
    HR(backend.GetPixelShaderConstantF(g_dev,0,(float *)(out+n),32));n+=128;
    static const D3DRENDERSTATETYPE keys[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,
        D3DRS_ZFUNC,D3DRS_CULLMODE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAREF,
        D3DRS_ALPHAFUNC,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,
        D3DRS_COLORWRITEENABLE,D3DRS_STENCILENABLE};
    for(unsigned i=0;i<sizeof keys/sizeof *keys;i++) {
        HR(backend.GetRenderState(g_dev,keys[i],&v));out[n++]=v;
    }
    for(unsigned slot=0;slot<8;slot++)for(unsigned key=1;key<=13;key++) {
        HR(backend.GetSamplerState(g_dev,slot,key,&v));out[n++]=v;
    }
    for(unsigned slot=0;slot<2;slot++) {
        UINT frequency;HR(backend.GetStreamSourceFreq(g_dev,slot,&frequency));out[n++]=frequency;
    }
    REQUIRE(n<=DRAW_STATE_WORDS);return n;
}

static unsigned hash_words(const unsigned *v,unsigned n)
{
    unsigned h=2166136261u;
    for(unsigned i=0;i<n;i++)for(unsigned k=0;k<32;k+=8)h=(h^((v[i]>>k)&255))*16777619u;
    return h;
}

static void image_case(unsigned which,unsigned *pixels)
{
    /* Make unused constant registers deterministic across independent
     * references. Go through the public setters so wrapper mirrors agree. */
    static const float zero[256*4]={0};
    external_idle(0);
    HR(IDirect3DDevice9_SetVertexShaderConstantF(g_dev,0,zero,256));
    HR(IDirect3DDevice9_SetPixelShaderConstantF(g_dev,0,zero,32));
    material_fixture_epoch(which==2);
    material_fixture_clock(which==2?33:0);
    pose(which!=0);pair_optimized=0;retain_now=0;dispatch_bulk_now=1;
    frame_draw(which!=0,0,pixels);
    external_idle(0);
}

static void draw_sample(int mode,unsigned round,double *caller,double *complete)
{
    external_toggle(mode);external_idle(mode);
    *caller=*complete=0;
    for(unsigned rep=0;rep<4;rep++) {
        material_fixture_epoch((round+rep)&1);
        material_fixture_clock((round*4+rep)*33);
        double regular,extra,total;
        pair(0,&regular,&extra,&total,NULL);
        *caller+=total/4;*complete+=pair_completed_ms/4;
        external_idle(mode);
    }
}

#ifndef PF_EXTERNAL_DRAW_MAIN
#define PF_EXTERNAL_DRAW_MAIN main
#endif
int PF_EXTERNAL_DRAW_MAIN(int argc,char **argv)
{
    /* Reuse the established fixture CLI for its first four arguments. */
    REQUIRE(argc==5 && bench_no_game());
    REQUIRE(!strcmp(argv[3],"lit") || !strcmp(argv[3],"shadow"));
    const char *slash=strrchr(argv[2],'/'),*backslash=strrchr(argv[2],'\\');
    if(backslash && (!slash || backslash>slash))slash=backslash;
    REQUIRE(slash && (unsigned)(slash-argv[2])+1+sizeof "d3dx9_27.dll"<=sizeof material_runtime);
    memcpy(material_runtime,argv[2],(size_t)(slash-argv[2])+1);
    strcpy(material_runtime+(slash-argv[2])+1,"d3dx9_27.dll");
    material_setup=effect_setup;material_begin=material_fixture_begin;
    material_end=material_fixture_end;pose_adjust=spread;
    baseline_bulk_enabled=1;dispatch_scope_begin=bench_scope_bulk_begin;
    dispatch_scope_end=bench_scope_bulk_end;
    setup(argc,argv);backend=*g_dev->lpVtbl;pf_timing_set_detail(1);
    static unsigned reference[3][RT*RT],reference_state[3][DRAW_STATE_WORDS];
    unsigned words[3],covered=0,moved=0,changed=0;
    for(unsigned c=0;c<3;c++) {
        image_case(c,reference[c]);words[c]=actual_state(reference_state[c]);
    }
    for(unsigned i=0;i<RT*RT;i++) {
        covered+=reference[0][i]!=(shadow_mode?PF_DEPTH_CLEAR:0);
        moved+=reference[0][i]!=reference[1][i];
        changed+=reference[1][i]!=reference[2][i];
    }
    REQUIRE(covered>1000 && moved>100 && changed>100);
    printf("ORACLE: %s covered=%u fresh_pose=%u changed_material=%u hashes=%08X/%08X/%08X\n",
        shadow_mode?"shadow":"lit",covered,moved,changed,
        hash_words(reference[0],RT*RT),hash_words(reference[1],RT*RT),hash_words(reference[2],RT*RT));
    void *effect=NULL;unsigned technique,block;
    material_fixture_native(&effect,&technique,&block);REQUIRE(effect);
    HMODULE module=LoadLibraryA(argv[4]);REQUIRE(module);
    external_install_fn install=(external_install_fn)(void*)GetProcAddress(module,"AotrRtTestInstall");
    external_toggle=(external_toggle_fn)(void*)GetProcAddress(module,"AotrRtTestToggle");
    external_stats=(external_stats_fn)(void*)GetProcAddress(module,"AotrRtTestStats");
    external_stats4=(external_stats_fn)(void*)GetProcAddress(module,"AotrRtTestStatsV4");
    REQUIRE(install && external_toggle && external_stats && external_stats4);
    REQUIRE(install(g_dev,effect)==1);
    for(unsigned mode=0;mode<2;mode++)for(unsigned c=0;c<3;c++) {
        static unsigned pixels[RT*RT],state[DRAW_STATE_WORDS];
        external_toggle(mode);image_case(c,pixels);unsigned n=actual_state(state);
        unsigned different=0;for(unsigned i=0;i<RT*RT;i++)different+=pixels[i]!=reference[c][i];
        printf("CHECK: mode=%u case=%u different_pixels=%u state_equal=%d hash=%08X\n",
            mode,c,different,n==words[c] && !memcmp(state,reference_state[c],n*4),hash_words(pixels,RT*RT));
        REQUIRE(!different && n==words[c] && !memcmp(state,reference_state[c],n*4));
        external_idle(mode);
    }
    puts("LIMITS: synthetic geometry/materials, 288 groups of 25 meshes; bulk-after-second prototype; no native sources, terrain, reflections, Present or gameplay FPS");
    puts("round,off_caller_ms,on_caller_ms,off_complete_ms,on_complete_ms,saved_complete_ms");
    double saving[21];
    for(unsigned r=0;r<24;r++) {
        double caller[2],complete[2];REQUIRE(bench_no_game());
        for(unsigned j=0;j<2;j++) {
            unsigned mode=j^(r&1);draw_sample(mode,r,&caller[mode],&complete[mode]);
        }
        if(r<3)continue;
        saving[r-3]=complete[0]-complete[1];
        printf("sample,%u,%.4f,%.4f,%.4f,%.4f,%.4f\n",r-3,caller[0],caller[1],complete[0],complete[1],saving[r-3]);
    }
    qsort(saving,21,sizeof(double),compare_double);
    printf("SUMMARY: %s pair_completion_saving_median=%.4f ms\n",shadow_mode?"shadow":"lit",saving[10]);
    external_idle(0);char detail[8192];external_stats4(detail,sizeof detail);printf("FINAL QUEUE: %s\n",detail);
    external_toggle(0);REQUIRE(bench_no_game());
    /* The hooked module and referenced resources live until process exit. */
    return 0;
}
