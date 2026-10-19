/* Black-box attribution of the external DLL's own drain counter. This
 * observes documented test statistics; no foreign implementation is used. */
#define PF_EXTERNAL_DRAW_MAIN unused_external_draw_main
#include "bench_external_draw.c"

extern void material_fixture_prepare(unsigned);
extern void material_fixture_pass_begin(unsigned);
extern void material_fixture_pass_end(void);

typedef struct { unsigned drains,submitted,executed; } counters;
static counters count_now(void)
{
    char detail[8192]={0};counters c={0};external_stats4(detail,sizeof detail);
    char *s=strstr(detail,"seq="),*d=strstr(detail,"drains=");
    REQUIRE(s && d && sscanf(s,"seq=%u exec=%u",&c.submitted,&c.executed)==2 &&
        sscanf(d,"drains=%u",&c.drains)==1);
    return c;
}
#define PROBE(label,code) do { \
    counters before=count_now();code; counters after=count_now(); \
    printf("probe,%u,%s,%u,%u\n",round,label,after.drains-before.drains,after.submitted-before.submitted); \
} while(0)

int main(int argc,char **argv)
{
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
    setup(argc,argv);backend=*g_dev->lpVtbl;
    static unsigned pixels[RT*RT];image_case(0,pixels);
    void *effect=NULL;unsigned technique,block;
    material_fixture_native(&effect,&technique,&block);REQUIRE(effect);
    HMODULE module=LoadLibraryA(argv[4]);REQUIRE(module);
    external_install_fn install=(external_install_fn)(void*)GetProcAddress(module,"AotrRtTestInstall");
    external_toggle=(external_toggle_fn)(void*)GetProcAddress(module,"AotrRtTestToggle");
    external_stats=(external_stats_fn)(void*)GetProcAddress(module,"AotrRtTestStats");
    external_stats4=(external_stats_fn)(void*)GetProcAddress(module,"AotrRtTestStatsV4");
    REQUIRE(install && external_toggle && external_stats && external_stats4);
    REQUIRE(install(g_dev,effect)==1);external_toggle(1);external_idle(1);
    puts("round,operation,drain_delta,submitted_delta");
    for(unsigned round=0;round<4;round++) {
        counters a=count_now(),b=count_now();
        REQUIRE(a.drains==b.drains && a.submitted==b.submitted);
        unsigned bones[BONES*8]={0};
        for(unsigned j=0;j<BONES;j++)memcpy(bones+8*j,g_pivots[0]+g_bonemap[j]*0x58+0x30,28);
        PROBE("BeginScene",HR(IDirect3DDevice9_BeginScene(g_dev)));
        PROBE("prepare_technique_block_sources",material_fixture_prepare(round));
        PROBE("effect_Begin",material_fixture_open());
        PROBE("effect_BeginPass",material_fixture_pass_begin(0));
        PROBE("bones_SetRawValue_CommitChanges",material_fixture_bones(bones,sizeof bones));
        IDirect3DVertexShader9 *vs=NULL;IDirect3DVertexDeclaration9 *decl=NULL;
        IDirect3DVertexBuffer9 *vb=NULL;IDirect3DIndexBuffer9 *ib=NULL;
        UINT frequency,offset,stride;float constants[BONES*8];
        PROBE("GetVertexShader",HR(IDirect3DDevice9_GetVertexShader(g_dev,&vs)));
        PROBE("GetVertexDeclaration",HR(IDirect3DDevice9_GetVertexDeclaration(g_dev,&decl)));
        PROBE("GetStreamSourceFreq",HR(IDirect3DDevice9_GetStreamSourceFreq(g_dev,0,&frequency)));
        PROBE("GetStreamSource",HR(IDirect3DDevice9_GetStreamSource(g_dev,0,&vb,&offset,&stride)));
        PROBE("GetIndices",HR(IDirect3DDevice9_GetIndices(g_dev,&ib)));
        PROBE("GetVertexShaderConstantF",HR(IDirect3DDevice9_GetVertexShaderConstantF(g_dev,g_v->roles[PF_ROLE_BONES],constants,2*BONES)));
        /* The first getter can drain work needed by later getters. Test
         * constants again after a new effect commit to avoid that masking. */
        bones[4]^=1;material_fixture_bones(bones,sizeof bones);
        PROBE("GetVertexShaderConstantF_after_Commit",HR(IDirect3DDevice9_GetVertexShaderConstantF(g_dev,g_v->roles[PF_ROLE_BONES],constants,2*BONES)));
        bones[4]^=1;material_fixture_bones(bones,sizeof bones);
        PROBE("GetVertexDeclaration_after_Commit",HR(IDirect3DDevice9_GetVertexDeclaration(g_dev,&decl)));IDirect3DVertexDeclaration9_Release(decl);
        bones[4]^=1;material_fixture_bones(bones,sizeof bones);
        PROBE("GetStreamSourceFreq_after_Commit",HR(IDirect3DDevice9_GetStreamSourceFreq(g_dev,0,&frequency)));
        bones[4]^=1;material_fixture_bones(bones,sizeof bones);
        PROBE("GetStreamSource_after_Commit",HR(IDirect3DDevice9_GetStreamSource(g_dev,0,&vb,&offset,&stride)));IDirect3DVertexBuffer9_Release(vb);
        bones[4]^=1;material_fixture_bones(bones,sizeof bones);
        PROBE("GetIndices_after_Commit",HR(IDirect3DDevice9_GetIndices(g_dev,&ib)));IDirect3DIndexBuffer9_Release(ib);
        /* Resource ownership cannot be inferred from a Release measured
         * after an earlier getter already drained. Requeue work first. */
        bones[4]^=1;material_fixture_bones(bones,sizeof bones);
        PROBE("shader_AddRef_after_Commit",REQUIRE(vs);IDirect3DVertexShader9_AddRef(vs));
        bones[4]^=1;material_fixture_bones(bones,sizeof bones);
        PROBE("shader_Release_after_Commit",IDirect3DVertexShader9_Release(vs));
        PROBE("resource_Release",if(vs)IDirect3DVertexShader9_Release(vs);if(decl)IDirect3DVertexDeclaration9_Release(decl);if(vb)IDirect3DVertexBuffer9_Release(vb);if(ib)IDirect3DIndexBuffer9_Release(ib));
        PROBE("DrawIndexedPrimitive",HR(IDirect3DDevice9_DrawIndexedPrimitive(g_dev,D3DPT_TRIANGLELIST,0,0,(GRID+1)*(GRID+1),0,GRID*GRID*2)));
        PROBE("effect_EndPass",material_fixture_pass_end());
        PROBE("effect_End",material_fixture_close());
        PROBE("EndScene",HR(IDirect3DDevice9_EndScene(g_dev)));
        external_idle(1);
    }
    puts("LIMITS: drain counters identify synchronous boundaries, not their elapsed cost; grouped preparation is not attributed to an individual setter");
    external_toggle(0);REQUIRE(bench_no_game());return 0;
}
