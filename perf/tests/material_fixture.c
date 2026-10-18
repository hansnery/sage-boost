/* Real D3DX27 effect lifecycle with synthetic material inputs. The local
 * effect bytes and all recorded state remain in memory only. */
#define main unused_shadow_fixture_main
#include "shadow_fx/shadow_fx.c"
#undef main

#define REQUIRE(c) do { if(!(c)) { fprintf(stderr,"material fixture refused %s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while(0)
#define HR(c) REQUIRE(SUCCEEDED(c))
#define F(m,...) FXCALL(effect,m,##__VA_ARGS__)
static ID3DXEffect *effect;
static IDirect3DTexture9 *textures[2];
static D3DXHANDLE technique, bones_handle, opacity_handle, diffuse_handle, texture_handle;
static unsigned char rs_mask[256], sampler_mask[16][14], tss_mask[8][33], texture_mask[16];
static D3DXHANDLE inputs[64];
static unsigned input_bytes[64], input_count;
static unsigned char input_values[64][4096];
static D3DXHANDLE material_block;
static unsigned epoch;
static unsigned churn=100;
static D3DXHANDLE key_handles[64];
static unsigned key_bytes[64],key_count,key_texture[64];
static IDirect3DDevice9 *material_device;
static unsigned verify_end,verified_ends,changed_end_states;
static DWORD entry_render_states[256];

void material_fixture_verify_end(unsigned enabled)
{
    verify_end=enabled;
    if(enabled) {
        /* Both fixture techniques disable culling. Seed a different valid
         * entry state so the End check proves restoration, not a no-op. */
        REQUIRE(rs_mask[D3DRS_CULLMODE]);
        HR(IDirect3DDevice9_SetRenderState(material_device,D3DRS_CULLMODE,D3DCULL_CW));
    }
}
void material_fixture_end_counts(unsigned *ends,unsigned *changed)
{
    *ends=verified_ends; *changed=changed_end_states;
}

void material_fixture_epoch(unsigned value) { epoch=value; }
void material_fixture_churn(unsigned value) { REQUIRE(value<=100); churn=value; }

static void value(const char *name,const void *v,unsigned bytes)
{
    D3DXHANDLE h=F(GetParameterByName,NULL,name);
    REQUIRE(h); HR(F(SetValue,h,v,bytes));
}
static void scalar(const char *name,float f) { value(name,&f,4); }
static void integer(const char *name,int i) { value(name,&i,4); }

/* Discover exactly the material state keys issued by this fixed fixture.
 * Shaders/constants/geometry are covered by the existing retained mask.
 * Reject any additional command kind instead of silently omitting it. */
static void collect_mask(void)
{
    unsigned at=0;
    REQUIRE(!g_rec_overflow && g_rec_n);
    while(at<g_rec_n) {
        unsigned op=g_rec[at],n=0;
        switch(op) {
        case 5: REQUIRE(g_rec[at+1]<256); rs_mask[g_rec[at+1]]=1; n=3; break;
        case 6: REQUIRE(g_rec[at+1]<16); texture_mask[g_rec[at+1]]=1; n=3; break;
        case 7: REQUIRE(g_rec[at+1]<8 && g_rec[at+2]<33); tss_mask[g_rec[at+1]][g_rec[at+2]]=1; n=4; break;
        case 8: REQUIRE(g_rec[at+1]<16 && g_rec[at+2]<14); sampler_mask[g_rec[at+1]][g_rec[at+2]]=1; n=4; break;
        case 11: case 15: n=2; break;
        case 12: REQUIRE(g_rec[at+1]+g_rec[at+2]<=256); n=3+g_rec[at+2]*4; break;
        case 16: REQUIRE(g_rec[at+1]+g_rec[at+2]<=32); n=3+g_rec[at+2]*4; break;
        default: REQUIRE(0);
        }
        REQUIRE(n<=g_rec_n-at); at+=n;
    }
}

HRESULT material_fixture_mask(IDirect3DDevice9 *dev)
{
    DWORD v;
    for(unsigned i=0;i<256;i++) if(rs_mask[i]) {
        HR(IDirect3DDevice9_GetRenderState(dev,i,&v));
        HR(IDirect3DDevice9_SetRenderState(dev,i,v));
    }
    for(unsigned s=0;s<16;s++) {
        if(texture_mask[s]) {
            IDirect3DBaseTexture9 *t=NULL;
            HR(IDirect3DDevice9_GetTexture(dev,s,&t));
            HR(IDirect3DDevice9_SetTexture(dev,s,t));
            if(t) IDirect3DBaseTexture9_Release(t);
        }
        for(unsigned i=0;i<14;i++) if(sampler_mask[s][i]) {
            HR(IDirect3DDevice9_GetSamplerState(dev,s,i,&v));
            HR(IDirect3DDevice9_SetSamplerState(dev,s,i,v));
        }
        if(s<8) for(unsigned i=0;i<33;i++) if(tss_mask[s][i]) {
            HR(IDirect3DDevice9_GetTextureStageState(dev,s,i,&v));
            HR(IDirect3DDevice9_SetTextureStageState(dev,s,i,v));
        }
    }
    return S_OK;
}

unsigned material_fixture_state(IDirect3DDevice9 *dev,unsigned *out,unsigned capacity)
{
    unsigned n=0; DWORD v;
#define PUT(v) do { REQUIRE(n<capacity); out[n++]=(unsigned)(UINT_PTR)(v); } while(0)
    for(unsigned i=0;i<256;i++) if(rs_mask[i]) {
        HR(IDirect3DDevice9_GetRenderState(dev,i,&v)); PUT(v);
    }
    for(unsigned s=0;s<16;s++) {
        if(texture_mask[s]) {
            IDirect3DBaseTexture9 *t=NULL;
            HR(IDirect3DDevice9_GetTexture(dev,s,&t)); PUT(t);
            if(t) IDirect3DBaseTexture9_Release(t);
        }
        for(unsigned i=0;i<14;i++) if(sampler_mask[s][i]) {
            HR(IDirect3DDevice9_GetSamplerState(dev,s,i,&v)); PUT(v);
        }
        if(s<8) for(unsigned i=0;i<33;i++) if(tss_mask[s][i]) {
            HR(IDirect3DDevice9_GetTextureStageState(dev,s,i,&v)); PUT(v);
        }
    }
#undef PUT
    return n;
}

static void prepare(unsigned group)
{
    unsigned delta=group%100<churn?epoch:0;
    float opacity=0.55f+((group+delta)%8)/20.f;
    float diffuse[3]={0.35f+((group+delta)%4)*0.15f,0.8f,0.6f};
    HR(F(SetTechnique,technique));
    HR(F(ApplyParameterBlock,material_block));
    /* Re-evaluate the fixed source values every group. This includes
     * their SetValue cost, but not the game's source-callback computation. */
    for(unsigned i=0;i<input_count;i++) HR(F(SetValue,inputs[i],input_values[i],input_bytes[i]));
    HR(F(SetFloat,opacity_handle,opacity));
    HR(F(SetValue,diffuse_handle,diffuse,sizeof diffuse));
    HR(F(SetTexture,texture_handle,(IDirect3DBaseTexture9 *)textures[(group+delta)&1]));
}
/* A retained hit keeps the real Begin/End pair but issues its drawing
 * directly. It never sends CommitChanges/EndPass without a BeginPass. */
void material_fixture_open(void)
{
    UINT passes=0;
    if(verify_end) for(unsigned i=0;i<256;i++) if(rs_mask[i])
        HR(IDirect3DDevice9_GetRenderState(material_device,i,&entry_render_states[i]));
    HR(F(Begin,&passes,6)); REQUIRE(passes==1);
}
void material_fixture_close(void)
{
    DWORD state;
    if(verify_end) for(unsigned i=0;i<256;i++) if(rs_mask[i]) {
        HR(IDirect3DDevice9_GetRenderState(material_device,i,&state));
        changed_end_states+=state!=entry_render_states[i];
    }
    HR(F(End));
    if(verify_end) {
        for(unsigned i=0;i<256;i++) if(rs_mask[i]) {
            HR(IDirect3DDevice9_GetRenderState(material_device,i,&state));
            REQUIRE(state==entry_render_states[i]);
        }
        verified_ends++;
    }
}
void material_fixture_start(void)
{
    material_fixture_open();
    HR(F(BeginPass,0));
}
void material_fixture_begin(unsigned group) { prepare(group); material_fixture_start(); }
void material_fixture_end(void) { HR(F(EndPass)); material_fixture_close(); }
void material_fixture_bones(const void *bones,unsigned bytes)
{
    HR(F(SetRawValue,bones_handle,bones,0,bytes)); HR(F(CommitChanges));
}

unsigned material_fixture_key(unsigned group,unsigned char *out,unsigned capacity)
{
    unsigned n=0;
    prepare(group);
    for(unsigned i=0;i<key_count;i++) {
        REQUIRE(key_bytes[i]<=capacity-n);
        if(key_texture[i]) {
            IDirect3DBaseTexture9 *texture=NULL;
            HR(F(GetTexture,key_handles[i],&texture));
            memcpy(out+n,&texture,sizeof texture);
            if(texture) IDirect3DBaseTexture9_Release(texture);
        } else HR(F(GetValue,key_handles[i],out+n,key_bytes[i]));
        n+=key_bytes[i];
    }
    return n;
}

void material_fixture_init(IDirect3DDevice9 *dev,const void *code,unsigned size,int shadow,const char *runtime)
{
    create_effect_fn create;
    D3DXEFFECT_DESC desc;
    const float white[4]={1,1,1,1}, gray[3]={0.4f,0.4f,0.4f};
    const float view[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const float projection[16]={2.f/54,0,0,0, 0,2.f/48,0,0, 0,0,.2f,0, -1,-1,.4f,1};
    const float texcoord[4]={1,1,0,0};
    HMODULE dll=LoadLibraryA(runtime);
    material_device=dev;
    create=(create_effect_fn)(void*)GetProcAddress(dll,"D3DXCreateEffect"); REQUIRE(create);
    HR(create(dev,code,size,NULL,NULL,0,NULL,&effect,NULL));
    technique=F(GetTechniqueByName,shadow?"_CreateShadowMap":"Default"); REQUIRE(technique);
    integer("NumJointsPerVertex",1); integer("NumTextures",1); integer("NumShadows",0);
    integer("DepthWriteEnable",1); integer("AlphaTestEnable",1); integer("CullingEnable",0);
    integer("BlendMode",0); integer("ObjectShroudStatus",0); integer("HouseColorEnable",0);
    value("View",view,sizeof view); value("Projection",projection,sizeof projection);
    value("AmbientLightColor",gray,sizeof gray); value("ColorDiffuse",white,12);
    value("ColorAmbient",white,12); scalar("Opacity",1); scalar("OpacityOverride",1);
    value("TexCoordTransform_0",texcoord,sizeof texcoord);
    for(unsigned i=0;i<2;i++) {
        D3DLOCKED_RECT locked;
        HR(IDirect3DDevice9_CreateTexture(dev,8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&textures[i],NULL));
        HR(IDirect3DTexture9_LockRect(textures[i],0,&locked,NULL,0));
        for(unsigned y=0;y<8;y++) for(unsigned x=0;x<8;x++)
            ((unsigned*)((char*)locked.pBits+y*locked.Pitch))[x]=((x+y)%3?0xff000000u:0) | (i?0x00e0c0a0u:0x0080e0c0u);
        HR(IDirect3DTexture9_UnlockRect(textures[i],0));
    }
    bones_handle=F(GetParameterByName,NULL,"WorldBones");
    opacity_handle=F(GetParameterByName,NULL,"OpacityOverride");
    diffuse_handle=F(GetParameterByName,NULL,"ColorDiffuse");
    texture_handle=F(GetParameterByName,NULL,"Texture_0");
    REQUIRE(bones_handle && opacity_handle && diffuse_handle && texture_handle);
    HR(F(GetDesc,&desc));
    HR(F(BeginParameterBlock));
    for(unsigned i=0;i<desc.Parameters;i++) {
        D3DXPARAMETER_DESC p; D3DXHANDLE h=F(GetParameter,NULL,i);
        HR(F(GetParameterDesc,h,&p));
        if(!F(IsParameterUsed,h,technique)||p.Class==D3DXPC_OBJECT||!p.Bytes||
           !strcmp(p.Name,"WorldBones")||!strcmp(p.Name,"WorldBones_L")) continue;
        REQUIRE(input_count<64 && p.Bytes<=4096);
        HR(F(GetValue,h,input_values[input_count],p.Bytes));
        if(!p.Semantic && !F(GetAnnotationByName,h,"SasBindAddress") &&
           strcmp(p.Name,"OpacityOverride") && strcmp(p.Name,"Time")) {
            HR(F(SetValue,h,input_values[input_count],p.Bytes));
        } else {
            inputs[input_count]=h; input_bytes[input_count]=p.Bytes;
            input_count++;
        }
    }
    material_block=F(EndParameterBlock); REQUIRE(material_block);
    for(unsigned i=0;i<desc.Parameters;i++) {
        D3DXPARAMETER_DESC p; D3DXHANDLE h=F(GetParameter,NULL,i);
        HR(F(GetParameterDesc,h,&p));
        if(!F(IsParameterUsed,h,technique)||!p.Bytes||
           !strcmp(p.Name,"WorldBones")||!strcmp(p.Name,"WorldBones_L")) continue;
        if(p.Class==D3DXPC_OBJECT && p.Type!=D3DXPT_TEXTURE) continue;
        REQUIRE(key_count<64);
        key_handles[key_count]=h; key_texture[key_count]=p.Type==D3DXPT_TEXTURE;
        key_bytes[key_count++]=p.Type==D3DXPT_TEXTURE?sizeof(void*):p.Bytes;
    }
    g_rec=calloc(REC_MAX,4); REQUIRE(g_rec);
    HR(F(SetStateManager,(ID3DXEffectStateManager*)&g_rm));
    for(unsigned i=0;i<8;i++) {
        g_rec_n=0; g_rec_overflow=0;
        material_fixture_begin(i); material_fixture_end(); collect_mask();
    }
    HR(F(SetStateManager,NULL));
    printf("Material fixture: D3DX27, %s, %u fixed input setters/group, fresh palette on each leader\n",shadow?"shadow":"lit",input_count);
    {
        unsigned states=0,samplers=0,stages=0,tex=0,bytes=0;
        for(unsigned i=0;i<256;i++) states+=rs_mask[i];
        for(unsigned s=0;s<16;s++) {
            tex+=texture_mask[s];
            for(unsigned i=0;i<14;i++) samplers+=sampler_mask[s][i];
            if(s<8) for(unsigned i=0;i<33;i++) stages+=tss_mask[s][i];
        }
        for(unsigned i=0;i<key_count;i++) bytes+=key_bytes[i];
        printf("Material state mask: %u render, %u sampler, %u texture-stage, %u textures; %u input bytes/key\n",states,samplers,stages,tex,bytes);
    }
}
