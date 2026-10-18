/* Real D3DX27 effect lifecycle with synthetic material inputs. The local
 * effect bytes and all recorded state remain in memory only. */
#define main unused_shadow_fixture_main
#include "shadow_fx/shadow_fx.c"
#undef main
#include "effect_filter_bench.h"

#define REQUIRE(c) do { if(!(c)) { fprintf(stderr,"material fixture refused %s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while(0)
#define HR(c) REQUIRE(SUCCEEDED(c))
#define F(m,...) FXCALL(effect,m,##__VA_ARGS__)
typedef struct {
    ID3DXEffect *instance;
    D3DXHANDLE tech,bones,opacity,diffuse,texture,clock,block;
    D3DXHANDLE inputs_[64],key_handles_[64];
    unsigned input_bytes_[64],input_count_,key_bytes_[64],key_count_,key_texture_[64];
    unsigned char input_values_[64][4096];
} material_context;
static material_context contexts[2],*current=&contexts[0];
#define effect (current->instance)
#define technique (current->tech)
#define bones_handle (current->bones)
#define opacity_handle (current->opacity)
#define diffuse_handle (current->diffuse)
#define texture_handle (current->texture)
#define clock_handle (current->clock)
#define material_block (current->block)
#define inputs (current->inputs_)
#define input_bytes (current->input_bytes_)
#define input_count (current->input_count_)
#define input_values (current->input_values_)
#define key_handles (current->key_handles_)
#define key_bytes (current->key_bytes_)
#define key_count (current->key_count_)
#define key_texture (current->key_texture_)
static IDirect3DTexture9 *textures[2];
static unsigned char rs_mask[256], sampler_mask[16][14], tss_mask[8][33], texture_mask[16];
static unsigned epoch;
static unsigned churn=100;
static IDirect3DDevice9 *material_device;
static unsigned verify_end,verified_ends,changed_end_states;
static unsigned clock_override;
static float clock_value;
static DWORD entry_render_states[256];
static unsigned filter_mode;

void material_fixture_filter(unsigned mode)
{
    filter_mode=mode;
    bench_filter_mode(mode);
    current=&contexts[mode?1:0];
}

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
void material_fixture_clock(unsigned ms)
{
    REQUIRE(clock_handle); clock_override=1; clock_value=ms*0.001f;
}

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
    if(clock_override) HR(F(SetFloat,clock_handle,clock_value));
}
/* A retained hit keeps the real Begin/End pair but issues its drawing
 * directly. It never sends CommitChanges/EndPass without a BeginPass. */
void material_fixture_open(void)
{
    UINT passes=0;
    if(verify_end) for(unsigned i=0;i<256;i++) if(rs_mask[i])
        HR(IDirect3DDevice9_GetRenderState(material_device,i,&entry_render_states[i]));
    if(filter_mode) HR(bench_filter_begin());
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
    if(filter_mode) HR(bench_filter_end());
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
/* Native group integration drives its Begin/Pass/End virtuals separately. */
void material_fixture_prepare(unsigned group) { prepare(group); }
void material_fixture_pass_begin(unsigned pass) { HR(F(BeginPass,pass)); }
void material_fixture_pass_end(void) { HR(F(EndPass)); }
void material_fixture_bones(const void *bones,unsigned bytes)
{
    HR(F(SetRawValue,bones_handle,bones,0,bytes)); HR(F(CommitChanges));
}

static unsigned read_key(unsigned char *out,unsigned capacity)
{
    unsigned n=0;
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

unsigned material_fixture_key(unsigned group,unsigned char *out,unsigned capacity)
{
    prepare(group);
    return read_key(out,capacity);
}

void material_fixture_clock_probe(void)
{
    unsigned char before[8192],after[8192];
    unsigned clocks=0;
    prepare(0);
    for(unsigned i=0;i<key_count;i++) {
        D3DXPARAMETER_DESC p;
        const char *address=NULL;
        D3DXHANDLE annotation;
        HR(F(GetParameterDesc,key_handles[i],&p));
        annotation=F(GetAnnotationByName,key_handles[i],"SasBindAddress");
        if(annotation) HR(F(GetString,annotation,&address));
        if(strcmp(p.Name,"Time") && (!p.Semantic || _stricmp(p.Semantic,"Time")) &&
           (!address || !contains_i(address,"Sas.Time."))) continue;
        REQUIRE(p.Type==D3DXPT_FLOAT && p.Bytes==sizeof(float));
        float old;
        HR(F(GetFloat,key_handles[i],&old));
        unsigned n=read_key(before,sizeof before);
        HR(F(SetFloat,key_handles[i],old+123.0f));
        REQUIRE(read_key(after,sizeof after)==n && memcmp(before,after,n));
        HR(F(SetFloat,key_handles[i],old));
        REQUIRE(read_key(after,sizeof after)==n && !memcmp(before,after,n));
        /* Distinguish a changed parameter key from changed device commands
         * in this fixed material. The recorder does not draw or persist EA
         * shader code. Equality here is not a general Time-omission proof. */
        unsigned *commands=malloc(REC_MAX*sizeof(unsigned)); REQUIRE(commands);
        HR(F(SetStateManager,(ID3DXEffectStateManager*)&g_rm));
        g_rec_n=0; g_rec_overflow=0;
        material_fixture_start(); HR(F(CommitChanges)); material_fixture_end();
        REQUIRE(!g_rec_overflow);
        unsigned command_words=g_rec_n;
        memcpy(commands,g_rec,command_words*sizeof(unsigned));
        HR(F(SetFloat,key_handles[i],old+123.0f));
        g_rec_n=0; g_rec_overflow=0;
        material_fixture_start(); HR(F(CommitChanges)); material_fixture_end();
        REQUIRE(!g_rec_overflow);
        unsigned changed=g_rec_n!=command_words;
        for(unsigned k=0;k<g_rec_n && k<command_words;k++) changed+=commands[k]!=g_rec[k];
        HR(F(SetFloat,key_handles[i],old));
        HR(F(SetStateManager,NULL)); free(commands);
        printf("CLOCK COMMANDS: %u/%u recorded words, %u differences for fixed fixture inputs\n",command_words,g_rec_n,changed);
        clocks++;
    }
    printf("CLOCK KEY: %u clock-dependent fields; each changed value invalidates the exact key\n",clocks);
}

static unsigned filter_snapshot(unsigned *out)
{
    IDirect3DVertexShader9 *v=NULL;
    IDirect3DPixelShader9 *p=NULL;
    HR(IDirect3DDevice9_GetVertexShader(material_device,&v));
    HR(IDirect3DDevice9_GetPixelShader(material_device,&p));
    /* Independently created effects can own distinct COM shader objects.
     * Compare their exact code hashes, never serialize their bytecode. */
    unsigned char code[32768]; UINT bytes=sizeof code;
    out[0]=out[1]=0;
    if(v) {
        HR(IDirect3DVertexShader9_GetFunction(v,NULL,&bytes)); REQUIRE(bytes<=sizeof code);
        HR(IDirect3DVertexShader9_GetFunction(v,code,&bytes));
        out[0]=fnv1a(code,bytes);
    }
    bytes=sizeof code;
    if(p) {
        HR(IDirect3DPixelShader9_GetFunction(p,NULL,&bytes)); REQUIRE(bytes<=sizeof code);
        HR(IDirect3DPixelShader9_GetFunction(p,code,&bytes));
        out[1]=fnv1a(code,bytes);
    }
    if(v) IDirect3DVertexShader9_Release(v);
    if(p) IDirect3DPixelShader9_Release(p);
    HR(IDirect3DDevice9_GetVertexShaderConstantF(material_device,0,(float*)(out+2),256));
    HR(IDirect3DDevice9_GetPixelShaderConstantF(material_device,0,(float*)(out+1026),32));
    return 1154+material_fixture_state(material_device,out+1154,2048-1154);
}

void material_fixture_filter_check(void)
{
    IDirect3DStateBlock9 *entry=NULL;
    static unsigned during[2][8][2048],after[2][8][2048];
    static const unsigned groups[8]={0,0,1,1,0,2,2,0};
    unsigned char bones[8192]; D3DXPARAMETER_DESC bone_desc;
    unsigned checks=0;
    unsigned long long calls0,sent0,calls1,sent1;
    HR(IDirect3DDevice9_CreateStateBlock(material_device,D3DSBT_ALL,&entry));
    HR(IDirect3DStateBlock9_Capture(entry));
    bench_filter_counts(&calls0,&sent0);
    material_fixture_verify_end(1);
    material_fixture_filter(0);
    HR(F(GetParameterDesc,bones_handle,&bone_desc)); REQUIRE(bone_desc.Bytes<=sizeof bones);
    HR(F(GetValue,bones_handle,bones,bone_desc.Bytes));
    for(unsigned mode=1;mode<=2;mode++) for(unsigned phase=0;phase<2;phase++) {
        unsigned n[2][8],m[2][8];
        material_fixture_epoch(phase);
        material_fixture_clock(1000+phase*33);
        for(unsigned alternative=0;alternative<2;alternative++) {
            HR(IDirect3DStateBlock9_Apply(entry));
            material_fixture_filter(alternative?mode:0);
            HR(F(SetRawValue,bones_handle,bones,0,bone_desc.Bytes));
            for(unsigned step=0;step<8;step++) {
                material_fixture_begin(groups[step]);
                HR(F(CommitChanges));
                HR(F(CommitChanges));
                n[alternative][step]=filter_snapshot(during[alternative][step]);
                material_fixture_end();
                m[alternative][step]=filter_snapshot(after[alternative][step]);
            }
        }
        for(unsigned step=0;step<8;step++) {
            for(unsigned word=0;word<n[0][step];word++) if(during[0][step][word]!=during[1][step][word]) {
                printf("STATE FILTER mismatch: mode=%u phase=%u step=%u in-pass word=%u\n",mode,phase,step,word);
                break;
            }
            for(unsigned word=0;word<m[0][step];word++) if(after[0][step][word]!=after[1][step][word]) {
                printf("STATE FILTER mismatch: mode=%u phase=%u step=%u post-End word=%u\n",mode,phase,step,word);
                break;
            }
            REQUIRE(n[0][step]==n[1][step] && !memcmp(during[0][step],during[1][step],n[0][step]*4));
            REQUIRE(m[0][step]==m[1][step] && !memcmp(after[0][step],after[1][step],m[0][step]*4));
            checks++;
        }
    }
    /* An unobserved original draw invalidates a device mirror. Prove this
     * control fails without invalidation, then verify the supported path. */
    HR(IDirect3DStateBlock9_Apply(entry));
    material_fixture_filter(2);
    material_fixture_begin(0);
    IDirect3DBaseTexture9 *expected=NULL,*actual=NULL;
    unsigned slot=0;
    for(;slot<16;slot++) {
        HR(IDirect3DDevice9_GetTexture(material_device,slot,&expected));
        if(expected) break;
    }
    REQUIRE(expected && slot<16);
    HR(IDirect3DDevice9_SetTexture(material_device,slot,NULL));
    ID3DXEffectStateManager *manager=bench_filter_manager();
    HR(manager->lpVtbl->SetTexture(manager,slot,expected));
    HR(IDirect3DDevice9_GetTexture(material_device,slot,&actual));
    REQUIRE(actual!=expected); /* Deliberately stale mirror must be detected. */
    if(actual) IDirect3DBaseTexture9_Release(actual);
    bench_filter_invalidate();
    HR(manager->lpVtbl->SetTexture(manager,slot,expected));
    HR(IDirect3DDevice9_GetTexture(material_device,slot,&actual));
    REQUIRE(actual==expected);
    if(actual) IDirect3DBaseTexture9_Release(actual);
    IDirect3DBaseTexture9_Release(expected);
    material_fixture_end();
    material_fixture_filter(0);
    HR(IDirect3DStateBlock9_Apply(entry));
    IDirect3DStateBlock9_Release(entry);
    bench_filter_counts(&calls1,&sent1);
    REQUIRE(calls1>calls0 && sent1>sent0 && calls1-calls0>sent1-sent0);
    printf("STATE FILTER: %u original/manager in-pass and post-End comparisons; external-write negative control detected; invalidation recovered; %llu/%llu calls forwarded\n",
           checks,sent1-sent0,calls1-calls0);
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
    g_rec=calloc(REC_MAX,4); REQUIRE(g_rec);
    for(unsigned i=0;i<2;i++) {
        D3DLOCKED_RECT locked;
        HR(IDirect3DDevice9_CreateTexture(dev,8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&textures[i],NULL));
        HR(IDirect3DTexture9_LockRect(textures[i],0,&locked,NULL,0));
        for(unsigned y=0;y<8;y++) for(unsigned x=0;x<8;x++)
            ((unsigned*)((char*)locked.pBits+y*locked.Pitch))[x]=((x+y)%3?0xff000000u:0) | (i?0x00e0c0a0u:0x0080e0c0u);
        HR(IDirect3DTexture9_UnlockRect(textures[i],0));
    }
    for(unsigned instance=0;instance<2;instance++) {
    current=&contexts[instance];
    HR(create(dev,code,size,NULL,NULL,0,NULL,&effect,NULL));
    technique=F(GetTechniqueByName,shadow?"_CreateShadowMap":"Default"); REQUIRE(technique);
    integer("NumJointsPerVertex",1); integer("NumTextures",1); integer("NumShadows",0);
    integer("DepthWriteEnable",1); integer("AlphaTestEnable",1); integer("CullingEnable",0);
    integer("BlendMode",0); integer("ObjectShroudStatus",0); integer("HouseColorEnable",0);
    value("View",view,sizeof view); value("Projection",projection,sizeof projection);
    value("AmbientLightColor",gray,sizeof gray); value("ColorDiffuse",white,12);
    value("ColorAmbient",white,12); scalar("Opacity",1); scalar("OpacityOverride",1);
    value("TexCoordTransform_0",texcoord,sizeof texcoord);
    bones_handle=F(GetParameterByName,NULL,"WorldBones");
    opacity_handle=F(GetParameterByName,NULL,"OpacityOverride");
    diffuse_handle=F(GetParameterByName,NULL,"ColorDiffuse");
    texture_handle=F(GetParameterByName,NULL,"Texture_0");
    clock_handle=F(GetParameterByName,NULL,"Time");
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
    HR(F(SetStateManager,(ID3DXEffectStateManager*)&g_rm));
    for(unsigned i=0;i<8;i++) {
        g_rec_n=0; g_rec_overflow=0;
        material_fixture_begin(i); material_fixture_end(); collect_mask();
    }
    HR(F(SetStateManager,instance?bench_filter_manager():NULL));
    }
    current=&contexts[0];
    bench_filter_init(dev,rs_mask,texture_mask);
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
