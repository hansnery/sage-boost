/* Isolated upload/batching comparison, not a runtime renderer replacement. */
#define PF_MATERIAL_MAIN unused_material_main
#include "bench_material_queue.c"
#include "palette_texture_shaders.h"

#define TEX_W 1024u
#define TEX_H 512u
#define VERTEX_SAMPLER D3DVERTEXTEXTURESAMPLER0
static IDirect3DVertexShader9 *constant_vs, *texture_vs;
static IDirect3DVertexDeclaration9 *palette_decl;
static IDirect3DVertexBuffer9 *instance_indices;
static IDirect3DTexture9 *palette_texture, *sentinel_texture;
static unsigned staged[GROUPS][TEX_W][4];
static const D3DSAMPLERSTATETYPE sampler_keys[] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MINFILTER,
    D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE
};
static DWORD entry_sampler[6];

static void palette_setup(int argc, char **argv)
{
    const char *crt=argc>2?argv[2]:"C:/Jogos/AgeoftheRing/rotwk/msvcr71.dll";
    const char *slash=strrchr(crt,'/'),*backslash=strrchr(crt,'\\');
    if(backslash && (!slash || backslash>slash)) slash=backslash;
    REQUIRE(slash && (unsigned)(slash-crt)+1+sizeof "d3dx9_27.dll"<=sizeof material_runtime);
    memcpy(material_runtime,crt,(size_t)(slash-crt)+1);
    strcpy(material_runtime+(slash-crt)+1,"d3dx9_27.dll");
    material_setup=effect_setup;
    pose_adjust=spread;
    setup(argc,argv);
    D3DCAPS9 caps;
    D3DDEVICE_CREATION_PARAMETERS creation;
    D3DDISPLAYMODE display;
    IDirect3D9 *d3d=NULL;
    HR(IDirect3DDevice9_GetDeviceCaps(g_dev,&caps));
    REQUIRE(caps.VertexShaderVersion>=D3DVS_VERSION(3,0));
    REQUIRE(caps.MaxTextureWidth>=TEX_W && caps.MaxTextureHeight>=TEX_H);
    HR(IDirect3DDevice9_GetCreationParameters(g_dev,&creation));
    HR(IDirect3DDevice9_GetDirect3D(g_dev,&d3d));
    HR(IDirect3D9_GetAdapterDisplayMode(d3d,creation.AdapterOrdinal,&display));
    HR(IDirect3D9_CheckDeviceFormat(d3d,creation.AdapterOrdinal,creation.DeviceType,
        display.Format,D3DUSAGE_QUERY_VERTEXTEXTURE|D3DUSAGE_DYNAMIC,
        D3DRTYPE_TEXTURE,D3DFMT_A32B32G32R32F));
    IDirect3D9_Release(d3d);
    HR(IDirect3DDevice9_CreateTexture(g_dev,TEX_W,TEX_H,1,D3DUSAGE_DYNAMIC,
        D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&palette_texture,NULL));
    HR(IDirect3DDevice9_CreateTexture(g_dev,1,1,1,0,D3DFMT_A32B32G32R32F,
        D3DPOOL_DEFAULT,&sentinel_texture,NULL));
    const DWORD *code=shadow_mode?palette_code_5:palette_code_476;
    REQUIRE(g_v->ea_hash==(shadow_mode?palette_identity_5:palette_identity_476));
    REQUIRE(g_v->joints==1 && 2*BONES*GROUP_SIZE<=TEX_W);
    HR(IDirect3DDevice9_CreateVertexShader(g_dev,(const DWORD *)g_v->code,&constant_vs));
    HR(IDirect3DDevice9_CreateVertexShader(g_dev,code,&texture_vs));
    D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1];
    UINT count=MAXD3DDECLLENGTH+1;
    HR(IDirect3DVertexDeclaration9_GetDeclaration(g_decl,elements,&count));
    REQUIRE(count>0 && count<MAXD3DDECLLENGTH);
    for(unsigned i=0;i<count-1;i++)
        REQUIRE(elements[i].Stream==0 && !(elements[i].Usage==D3DDECLUSAGE_TEXCOORD && elements[i].UsageIndex==7));
    elements[count-1]=(D3DVERTEXELEMENT9){1,0,D3DDECLTYPE_FLOAT1,0,D3DDECLUSAGE_TEXCOORD,7};
    elements[count]=(D3DVERTEXELEMENT9)D3DDECL_END();
    HR(IDirect3DDevice9_CreateVertexDeclaration(g_dev,elements,&palette_decl));
    HR(IDirect3DDevice9_CreateVertexBuffer(g_dev,GROUP_SIZE*sizeof(float),D3DUSAGE_WRITEONLY,
        0,D3DPOOL_MANAGED,&instance_indices,NULL));
    float *indices=NULL;
    HR(IDirect3DVertexBuffer9_Lock(instance_indices,0,0,(void **)&indices,0));
    for(unsigned i=0;i<GROUP_SIZE;i++) indices[i]=(float)(i*2*BONES);
    HR(IDirect3DVertexBuffer9_Unlock(instance_indices));
    HR(IDirect3DDevice9_SetVertexShader(g_dev,g_ea));
    HR(IDirect3DDevice9_SetVertexDeclaration(g_dev,g_decl));
    HR(IDirect3DDevice9_SetStreamSource(g_dev,0,g_vb,0,40));
    HR(IDirect3DDevice9_SetIndices(g_dev,g_ib));
    HR(IDirect3DDevice9_SetTexture(g_dev,VERTEX_SAMPLER,(IDirect3DBaseTexture9 *)sentinel_texture));
    for(unsigned i=0;i<6;i++) HR(IDirect3DDevice9_GetSamplerState(g_dev,VERTEX_SAMPLER,sampler_keys[i],&entry_sampler[i]));
    printf("PALETTE_TEXTURE_SUPPORT,bones,%u,constant_capacity,%u,texture_capacity,%u,width,%u,height,%u\n",
           (unsigned)BONES,g_v->ir_count/(2*BONES),GROUP_SIZE,TEX_W,TEX_H);
}

static void stage_pose(unsigned phase)
{
    pose(0);
    for(unsigned group=0;group<GROUPS;group++)
        for(unsigned i=0;i<GROUP_SIZE;i++)
            for(unsigned b=0;b<BONES;b++) {
                float *p=(float *)(many_pivots[group*GROUP_SIZE+i]+g_bonemap[b]*0x58+0x30);
                p[4]+=0.02f*phase;
                p[5]+=(0.04f/3.0f)*phase;
                p[6]+=(0.02f/3.0f)*phase;
                unsigned *dest=staged[group][2*(i*BONES+b)];
                memcpy(dest,p,28); dest[7]=0;
            }
}

static void upload_texture(void)
{
    D3DLOCKED_RECT locked;
    HR(IDirect3DTexture9_LockRect(palette_texture,0,&locked,NULL,D3DLOCK_DISCARD));
    REQUIRE(locked.Pitch>=(int)(TEX_W*16));
    for(unsigned group=0;group<GROUPS;group++)
        memcpy((unsigned char *)locked.pBits+group*locked.Pitch,staged[group],GROUP_SIZE*BONES*32);
    HR(IDirect3DTexture9_UnlockRect(palette_texture,0));
}

static double palette_frame(int texture,unsigned phase,int stale,unsigned *pixels)
{
    unsigned draws=0,meshes=0,refs=g_method.refs;
    const unsigned capacity=texture?GROUP_SIZE:g_v->ir_count/(2*BONES);
    REQUIRE(capacity>0);
    drain();
    long long start=ticks();
    stage_pose(phase);
    HR(IDirect3DStateBlock9_Capture(outer));
    if(texture && !stale) upload_texture();
    HR(IDirect3DDevice9_BeginScene(g_dev));
    HR(IDirect3DDevice9_Clear(g_dev,0,NULL,D3DCLEAR_TARGET|(shadow_mode?D3DCLEAR_ZBUFFER:0),
                             shadow_mode?0xffffffffu:0,1,0));
    if(texture) {
        HR(IDirect3DDevice9_SetTexture(g_dev,VERTEX_SAMPLER,(IDirect3DBaseTexture9 *)palette_texture));
        HR(IDirect3DDevice9_SetSamplerState(g_dev,VERTEX_SAMPLER,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));
        HR(IDirect3DDevice9_SetSamplerState(g_dev,VERTEX_SAMPLER,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
        HR(IDirect3DDevice9_SetSamplerState(g_dev,VERTEX_SAMPLER,D3DSAMP_MINFILTER,D3DTEXF_POINT));
        HR(IDirect3DDevice9_SetSamplerState(g_dev,VERTEX_SAMPLER,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
        HR(IDirect3DDevice9_SetSamplerState(g_dev,VERTEX_SAMPLER,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
        HR(IDirect3DDevice9_SetSamplerState(g_dev,VERTEX_SAMPLER,D3DSAMP_SRGBTEXTURE,0));
    }
    for(unsigned group=0;group<GROUPS;group++) {
        material_fixture_begin(group);
        HR(IDirect3DDevice9_SetVertexShader(g_dev,texture?texture_vs:constant_vs));
        HR(IDirect3DDevice9_SetVertexDeclaration(g_dev,palette_decl));
        HR(IDirect3DDevice9_SetStreamSource(g_dev,1,instance_indices,0,sizeof(float)));
        HR(IDirect3DDevice9_SetStreamSourceFreq(g_dev,1,D3DSTREAMSOURCE_INSTANCEDATA|1));
        if(texture) {
            float params[]={1.f/TEX_W,1.f/TEX_H,(group+0.5f)/TEX_H,0};
            HR(IDirect3DDevice9_SetVertexShaderConstantF(g_dev,g_v->ir_reg,params,1));
        }
        for(unsigned i=0;i<GROUP_SIZE;i+=capacity) {
            unsigned n=GROUP_SIZE-i; if(n>capacity)n=capacity;
            if(!texture) HR(IDirect3DDevice9_SetVertexShaderConstantF(g_dev,g_v->ir_reg,
                (const float *)staged[group][i*2*BONES],n*2*BONES));
            HR(IDirect3DDevice9_SetStreamSourceFreq(g_dev,0,D3DSTREAMSOURCE_INDEXEDDATA|n));
            HR(IDirect3DDevice9_DrawIndexedPrimitive(g_dev,D3DPT_TRIANGLELIST,0,0,
                (GRID+1)*(GRID+1),0,GRID*GRID*2));
            draws++; meshes+=n;
        }
        material_fixture_end();
        HR(IDirect3DDevice9_SetVertexShader(g_dev,g_ea));
        HR(IDirect3DDevice9_SetVertexDeclaration(g_dev,g_decl));
        HR(IDirect3DDevice9_SetStreamSourceFreq(g_dev,0,1));
        HR(IDirect3DDevice9_SetStreamSourceFreq(g_dev,1,1));
        HR(IDirect3DDevice9_SetStreamSource(g_dev,1,NULL,0,0));
    }
    HR(IDirect3DDevice9_EndScene(g_dev));
    HR(IDirect3DStateBlock9_Apply(outer));
    /* The installed DXVK did not restore all six vertex sampler states
     * through D3DSBT_ALL. Restore the new path's owned writes explicitly;
     * only that path pays this additional cost. Checks remain unchanged. */
    if(texture) {
        HR(IDirect3DDevice9_SetTexture(g_dev,VERTEX_SAMPLER,(IDirect3DBaseTexture9 *)sentinel_texture));
        for(unsigned i=0;i<6;i++)
            HR(IDirect3DDevice9_SetSamplerState(g_dev,VERTEX_SAMPLER,sampler_keys[i],entry_sampler[i]));
    }
    drain();
    double complete=(ticks()-start)*1000.0/qpf;
    REQUIRE(meshes==GROUPS*GROUP_SIZE && draws==GROUPS*((GROUP_SIZE+capacity-1)/capacity));
    REQUIRE(g_method.refs==refs);
    check_state();
    IDirect3DBaseTexture9 *after=NULL;
    HR(IDirect3DDevice9_GetTexture(g_dev,VERTEX_SAMPLER,&after));
    REQUIRE(after==(IDirect3DBaseTexture9 *)sentinel_texture);
    IDirect3DBaseTexture9_Release(after);
    for(unsigned i=0;i<6;i++) {
        DWORD value=0;
        HR(IDirect3DDevice9_GetSamplerState(g_dev,VERTEX_SAMPLER,sampler_keys[i],&value));
        if(value!=entry_sampler[i])
            fprintf(stderr,"vertex sampler %u: expected %lu, got %lu\n",(unsigned)sampler_keys[i],entry_sampler[i],value);
        REQUIRE(value==entry_sampler[i]);
    }
    if(pixels) read_pixels(pixels);
    return complete;
}

static unsigned image_difference(const unsigned *a,const unsigned *b,int expected_match)
{
    unsigned bad=0,covered=0;
    for(unsigned i=0;i<RT*RT;i++) {
        unsigned delta=0;
        covered+=a[i]!=(shadow_mode?PF_DEPTH_CLEAR:0);
        if(shadow_mode) delta=pf_depth_diff(a[i],b[i]);
        else for(unsigned shift=0;shift<32;shift+=8) {
            unsigned x=(a[i]>>shift)&255,y=(b[i]>>shift)&255;
            unsigned d=x>y?x-y:y-x; if(d>delta)delta=d;
        }
        bad+=delta>(shadow_mode?PF_DEPTH_TOLERANCE:2);
    }
    printf("PALETTE_IMAGE,match,%d,covered,%u,bad,%u\n",expected_match,covered,bad);
    REQUIRE(covered>1000);
    REQUIRE(expected_match?!bad:bad>100);
    return bad;
}

int main(int argc,char **argv)
{
    static unsigned baseline[RT*RT],candidate[RT*RT],stale[RT*RT];
    REQUIRE(bench_no_game());
    palette_setup(argc,argv);
    material_fixture_clock(0);
    palette_frame(1,0,0,NULL);
    for(unsigned phase=1;phase<=3;phase++) {
        palette_frame(0,phase,0,baseline);
        palette_frame(1,phase,1,stale);
        image_difference(baseline,stale,0);
        palette_frame(1,phase,0,candidate);
        image_difference(baseline,candidate,1);
    }
    printf("PALETTE_CONTROLS,bones,%u,mode,%s,phases,3,constant_draws,%u,texture_draws,%u\n",
           (unsigned)BONES,shadow_mode?"shadow":"lit",GROUPS*fixture_batches(GROUP_SIZE),GROUPS);
    double saved[ROUNDS];
    for(unsigned round=0;round<ROUNDS+WARMUP;round++) {
        double elapsed[2]={0};
        REQUIRE(bench_no_game());
        for(unsigned order=0;order<2;order++) {
            unsigned mode=order^(round&1);
            for(unsigned rep=0;rep<FRAMES_PER_SAMPLE;rep++) {
                material_fixture_clock((round*FRAMES_PER_SAMPLE+rep)*33);
                material_fixture_epoch((round+rep)&1);
                elapsed[mode]+=palette_frame(mode,(round+rep)%4,0,NULL)/FRAMES_PER_SAMPLE;
            }
        }
        REQUIRE(bench_no_game());
        if(round<WARMUP)continue;
        saved[round-WARMUP]=elapsed[0]-elapsed[1];
        printf("PALETTE_SAMPLE,%u,%.5f,%.5f,%.5f\n",round-WARMUP,elapsed[0],elapsed[1],saved[round-WARMUP]);
    }
    qsort(saved,ROUNDS,sizeof(double),compare_double);
    printf("PALETTE_SUMMARY,bones,%u,mode,%s,complete_saved_median_ms,%.5f,p10,%.5f\n",
           (unsigned)BONES,shadow_mode?"shadow":"lit",saved[ROUNDS/2],saved[2]);
    IDirect3DDevice9_SetTexture(g_dev,VERTEX_SAMPLER,NULL);
    IDirect3DStateBlock9_Release(outer);
    IDirect3DTexture9_Release(sentinel_texture);
    IDirect3DTexture9_Release(palette_texture);
    IDirect3DVertexBuffer9_Release(instance_indices);
    IDirect3DVertexDeclaration9_Release(palette_decl);
    IDirect3DVertexShader9_Release(constant_vs);
    IDirect3DVertexShader9_Release(texture_vs);
    IDirect3DQuery9_Release(fence);
    bench_filter_clear();
    return 0;
}
