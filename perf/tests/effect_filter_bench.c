/* A bounded D3DX state-write filter for the controlled GPU fixture only.
 * Sources and Begin/BeginPass/CommitChanges/EndPass/End still run. Objects
 * are borrowed from the fixture, which keeps them alive for the whole run.
 * This is not a runtime device interception or lifetime implementation. */
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <d3dx9effect.h>
#include <string.h>
#include "effect_filter_bench.h"

static IDirect3DDevice9 *device;
static IDirect3DStateBlock9 *restore;
static unsigned mode, opened;
static unsigned long long requested, forwarded;
static DWORD rs[256], sampler[16][14], tss[8][33];
static unsigned char rs_ok[256], sampler_ok[16][14], tss_ok[8][33];
static IDirect3DBaseTexture9 *textures[16];
static unsigned char texture_ok[16], vs_ok, ps_ok;
static IDirect3DVertexShader9 *vs;
static IDirect3DPixelShader9 *ps;
static unsigned vf[256][4], pf[224][4];
static unsigned char vf_ok[256], pf_ok[224];

void bench_filter_invalidate(void)
{
    memset(rs_ok,0,sizeof rs_ok); memset(sampler_ok,0,sizeof sampler_ok);
    memset(tss_ok,0,sizeof tss_ok); memset(texture_ok,0,sizeof texture_ok);
    memset(vf_ok,0,sizeof vf_ok); memset(pf_ok,0,sizeof pf_ok);
    vs_ok=ps_ok=0;
}

static HRESULT WINAPI qi(ID3DXEffectStateManager *self, REFIID iid, void **out)
{
    /* The fixture never queries another interface; reject unknown requests. */
    static const GUID unknown={0,0,0,{0xc0,0,0,0,0,0,0,0x46}};
    static const GUID manager={0x79aab587,0x6dbc,0x4fa7,{0x82,0xde,0x37,0xfa,0x17,0x81,0xc5,0xce}};
    if(!out) return E_POINTER;
    *out=NULL;
    if(memcmp(iid,&unknown,sizeof unknown) && memcmp(iid,&manager,sizeof manager))
        return E_NOINTERFACE;
    *out=self; return S_OK;
}
static ULONG WINAPI ref(ID3DXEffectStateManager *self) { (void)self; return 1; }

#define FILTER(old, valid, value, call) do { \
    requested++; \
    if(mode!=1 && (valid) && (old)==(value)) return S_OK; \
    forwarded++; HRESULT hr=(call); \
    (valid)=SUCCEEDED(hr); if(SUCCEEDED(hr)) (old)=(value); return hr; \
} while(0)
#define DIRECT(call) do { requested++; forwarded++; return (call); } while(0)

static HRESULT WINAPI set_rs(ID3DXEffectStateManager *s,D3DRENDERSTATETYPE k,DWORD v)
{
    (void)s; if((unsigned)k>=256) return D3DERR_INVALIDCALL;
    FILTER(rs[k],rs_ok[k],v,IDirect3DDevice9_SetRenderState(device,k,v));
}
static HRESULT WINAPI set_sampler(ID3DXEffectStateManager *s,DWORD stage,D3DSAMPLERSTATETYPE k,DWORD v)
{
    (void)s; if(stage>=16 || (unsigned)k>=14) return D3DERR_INVALIDCALL;
    FILTER(sampler[stage][k],sampler_ok[stage][k],v,IDirect3DDevice9_SetSamplerState(device,stage,k,v));
}
static HRESULT WINAPI set_tss(ID3DXEffectStateManager *s,DWORD stage,D3DTEXTURESTAGESTATETYPE k,DWORD v)
{
    (void)s; if(stage>=8 || (unsigned)k>=33) return D3DERR_INVALIDCALL;
    FILTER(tss[stage][k],tss_ok[stage][k],v,IDirect3DDevice9_SetTextureStageState(device,stage,k,v));
}
static HRESULT WINAPI set_texture(ID3DXEffectStateManager *s,DWORD stage,IDirect3DBaseTexture9 *v)
{
    (void)s; if(stage>=16) return D3DERR_INVALIDCALL;
    FILTER(textures[stage],texture_ok[stage],v,IDirect3DDevice9_SetTexture(device,stage,v));
}
static HRESULT WINAPI set_vs(ID3DXEffectStateManager *s,IDirect3DVertexShader9 *v)
{
    (void)s; FILTER(vs,vs_ok,v,IDirect3DDevice9_SetVertexShader(device,v));
}
static HRESULT WINAPI set_ps(ID3DXEffectStateManager *s,IDirect3DPixelShader9 *v)
{
    (void)s; FILTER(ps,ps_ok,v,IDirect3DDevice9_SetPixelShader(device,v));
}
static HRESULT constants(unsigned pixel,UINT r,const FLOAT *values,UINT n)
{
    unsigned (*cache)[4]=pixel?pf:vf;
    unsigned char *valid=pixel?pf_ok:vf_ok;
    unsigned limit=pixel?224:256;
    if(r>limit || n>limit-r || (!values && n)) return D3DERR_INVALIDCALL;
    requested++;
    unsigned same=mode!=1;
    for(unsigned i=0;same && i<n;i++) same=valid[r+i]!=0;
    if(same && !memcmp(cache+r,values,n*16)) return S_OK;
    forwarded++;
    HRESULT hr=pixel?IDirect3DDevice9_SetPixelShaderConstantF(device,r,values,n):
                     IDirect3DDevice9_SetVertexShaderConstantF(device,r,values,n);
    memset(valid+r,SUCCEEDED(hr),n);
    if(SUCCEEDED(hr)) memcpy(cache+r,values,n*16);
    return hr;
}
static HRESULT WINAPI set_vf(ID3DXEffectStateManager *s,UINT r,const FLOAT *v,UINT n)
{ (void)s; return constants(0,r,v,n); }
static HRESULT WINAPI set_pf(ID3DXEffectStateManager *s,UINT r,const FLOAT *v,UINT n)
{ (void)s; return constants(1,r,v,n); }
static HRESULT WINAPI set_transform(ID3DXEffectStateManager *s,D3DTRANSFORMSTATETYPE k,const D3DMATRIX *v)
{ (void)s; DIRECT(IDirect3DDevice9_SetTransform(device,k,v)); }
static HRESULT WINAPI set_material(ID3DXEffectStateManager *s,const D3DMATERIAL9 *v)
{ (void)s; DIRECT(IDirect3DDevice9_SetMaterial(device,v)); }
static HRESULT WINAPI set_light(ID3DXEffectStateManager *s,DWORD k,const D3DLIGHT9 *v)
{ (void)s; DIRECT(IDirect3DDevice9_SetLight(device,k,v)); }
static HRESULT WINAPI light_enable(ID3DXEffectStateManager *s,DWORD k,WINBOOL v)
{ (void)s; DIRECT(IDirect3DDevice9_LightEnable(device,k,v)); }
static HRESULT WINAPI set_npatch(ID3DXEffectStateManager *s,FLOAT v)
{ (void)s; DIRECT(IDirect3DDevice9_SetNPatchMode(device,v)); }
static HRESULT WINAPI set_fvf(ID3DXEffectStateManager *s,DWORD v)
{ (void)s; vs_ok=0; DIRECT(IDirect3DDevice9_SetFVF(device,v)); }
static HRESULT WINAPI set_vi(ID3DXEffectStateManager *s,UINT r,const INT *v,UINT n)
{ (void)s; DIRECT(IDirect3DDevice9_SetVertexShaderConstantI(device,r,v,n)); }
static HRESULT WINAPI set_vb(ID3DXEffectStateManager *s,UINT r,const WINBOOL *v,UINT n)
{ (void)s; DIRECT(IDirect3DDevice9_SetVertexShaderConstantB(device,r,v,n)); }
static HRESULT WINAPI set_pi(ID3DXEffectStateManager *s,UINT r,const INT *v,UINT n)
{ (void)s; DIRECT(IDirect3DDevice9_SetPixelShaderConstantI(device,r,v,n)); }
static HRESULT WINAPI set_pb(ID3DXEffectStateManager *s,UINT r,const WINBOOL *v,UINT n)
{ (void)s; DIRECT(IDirect3DDevice9_SetPixelShaderConstantB(device,r,v,n)); }
static ID3DXEffectStateManagerVtbl vtable={qi,ref,ref,set_transform,set_material,set_light,
    light_enable,set_rs,set_texture,set_tss,set_sampler,set_npatch,set_fvf,
    set_vs,set_vf,set_vi,set_vb,set_ps,set_pf,set_pi,set_pb};
static ID3DXEffectStateManager manager={&vtable};

void bench_filter_init(IDirect3DDevice9 *dev,const unsigned char *mask,const unsigned char *texmask)
{
    DWORD values[256];
    IDirect3DBaseTexture9 *entry_textures[16]={0};
    device=dev;
    for(unsigned i=0;i<256;i++) if(mask[i])
        if(FAILED(IDirect3DDevice9_GetRenderState(dev,i,&values[i]))) return;
    if(FAILED(IDirect3DDevice9_BeginStateBlock(dev))) return;
    HRESULT hr=S_OK;
    for(unsigned i=0;i<256;i++) if(mask[i]) {
        HRESULT result=IDirect3DDevice9_SetRenderState(dev,i,values[i]);
        if(FAILED(result)) hr=result;
    }
    for(unsigned i=0;i<16;i++) if(texmask[i]) {
        HRESULT result=IDirect3DDevice9_GetTexture(dev,i,&entry_textures[i]);
        if(SUCCEEDED(result)) result=IDirect3DDevice9_SetTexture(dev,i,entry_textures[i]);
        if(FAILED(result)) hr=result;
    }
    HRESULT end=IDirect3DDevice9_EndStateBlock(dev,&restore);
    for(unsigned i=0;i<16;i++) if(entry_textures[i]) IDirect3DBaseTexture9_Release(entry_textures[i]);
    if(FAILED(hr) || FAILED(end)) bench_filter_clear();
    bench_filter_invalidate();
}
ID3DXEffectStateManager *bench_filter_manager(void) { return &manager; }
void bench_filter_mode(unsigned value) { mode=value; bench_filter_invalidate(); }
HRESULT bench_filter_begin(void)
{
    if(!restore || opened || mode<1 || mode>2) return D3DERR_INVALIDCALL;
    HRESULT hr=IDirect3DStateBlock9_Capture(restore);
    if(SUCCEEDED(hr)) opened=1;
    return hr;
}
HRESULT bench_filter_end(void)
{
    if(!opened) return D3DERR_INVALIDCALL;
    opened=0;
    HRESULT hr=IDirect3DStateBlock9_Apply(restore);
    /* Begin(flags=6) still restores render states and texture bindings.
     * Shader/constants and sampler values remain live in this fixture. */
    memset(rs_ok,0,sizeof rs_ok); memset(texture_ok,0,sizeof texture_ok);
    if(FAILED(hr)) bench_filter_invalidate();
    return hr;
}
void bench_filter_counts(unsigned long long *calls,unsigned long long *sent)
{ *calls=requested; *sent=forwarded; }
void bench_filter_clear(void)
{
    if(restore) IDirect3DStateBlock9_Release(restore);
    restore=NULL; opened=0; bench_filter_invalidate();
}
