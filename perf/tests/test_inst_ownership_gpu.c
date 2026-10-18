/* Real-device counterpart to the deterministic COM lifetime test. A private
 * IUnknown on the buffer reports actual destruction without reading a freed
 * buffer pointer. The creator releases its reference after leader capture. */
#define main fixture_main
#include "test_inst.c"
#undef main
#include "bench_guard.h"
#define REQUIRE(c) do { if(!(c)){fprintf(stderr,"GPU ownership refused line %u: %s\n",__LINE__,#c);exit(1);} } while(0)
#define HR(c) REQUIRE(SUCCEEDED(c))

typedef struct { IUnknown unknown; LONG refs, destroyed; } Lifetime;
static HRESULT WINAPI lifetime_query(IUnknown *self,REFIID iid,void **out)
{ (void)self;(void)iid;*out=NULL;return E_NOINTERFACE; }
static ULONG WINAPI lifetime_add(IUnknown *self)
{ return (ULONG)InterlockedIncrement(&((Lifetime *)self)->refs); }
static ULONG WINAPI lifetime_release(IUnknown *self)
{
    Lifetime *value=(void *)self;LONG refs=InterlockedDecrement(&value->refs);
    if(!refs)InterlockedExchange(&value->destroyed,1);
    REQUIRE(refs>=0);return (ULONG)refs;
}
static IUnknownVtbl lifetime_vt={lifetime_query,lifetime_add,lifetime_release};
static const GUID lifetime_id={0x6cfedb42,0x4d0a,0x42b6,{0x88,0x1c,0x6a,0xc7,0x55,0x22,0x0d,0x91}};

static void drain_gpu(void)
{
    IDirect3DQuery9 *q=NULL;HR(IDirect3DDevice9_CreateQuery(g_dev,D3DQUERYTYPE_EVENT,&q));
    HR(IDirect3DQuery9_Issue(q,D3DISSUE_END));
    DWORD until=GetTickCount()+5000;HRESULT hr;
    while((hr=IDirect3DQuery9_GetData(q,NULL,0,D3DGETDATA_FLUSH))==S_FALSE) {
        REQUIRE((LONG)(GetTickCount()-until)<0);Sleep(1);
    }
    HR(hr);IDirect3DQuery9_Release(q);
}

static void render_owned(IDirect3DVertexBuffer9 **creator,unsigned count,unsigned passes)
{
    fake_mesh *list[MESHES];for(unsigned i=0;i<MESHES;i++)list[i]=g_mesh+i;
    HR(IDirect3DDevice9_BeginScene(g_dev));
    HR(IDirect3DDevice9_Clear(g_dev,0,NULL,D3DCLEAR_TARGET,0,1,0));
    HR(IDirect3DDevice9_SetVertexShader(g_dev,g_ea));
    HR(IDirect3DDevice9_SetVertexDeclaration(g_dev,g_decl));
    HR(IDirect3DDevice9_SetStreamSource(g_dev,0,g_vb,0,40));
    HR(IDirect3DDevice9_SetIndices(g_dev,g_ib));
    for(unsigned i=0;i<count;i++) {
        batch_call(list,i,MESHES,passes,1);
        if(!i && *creator){IDirect3DVertexBuffer9_Release(*creator);*creator=NULL;}
    }
    HR(IDirect3DDevice9_EndScene(g_dev));drain_gpu();
}

int main(int argc,char **argv)
{
    REQUIRE(bench_no_game());REQUIRE(fixture_main(argc,argv)==0 && g_dev && g_ea);
    HR(IDirect3DDevice9_SetRenderTarget(g_dev,0,g_rt));
    HR(IDirect3DDevice9_SetDepthStencilSurface(g_dev,NULL));
    build_scene();setup_constants();
    static unsigned reference[RT*RT],pixels[RT*RT];
    draw_frame(0,reference);
    unsigned covered=0;for(unsigned i=0;i<RT*RT;i++)covered+=reference[i]!=0;
    REQUIRE(covered>100);
    for(unsigned mode=0;mode<3;mode++) {
        Lifetime lifetime={{&lifetime_vt},1,0};
        IDirect3DVertexBuffer9 *creator=NULL,*identity,*found=NULL;
        HR(IDirect3DDevice9_CreateVertexBuffer(g_dev,4096,D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,&creator,NULL));
        identity=creator;
        HR(IDirect3DVertexBuffer9_SetPrivateData(creator,&lifetime_id,&lifetime.unknown,sizeof(IUnknown *),D3DSPD_IUNKNOWN));
        IUnknown_Release(&lifetime.unknown);REQUIRE(lifetime.refs==1 && !lifetime.destroyed);
        HR(IDirect3DDevice9_SetStreamSource(g_dev,1,creator,12,16));
        pf_inst_init(0);pf_inst_counts before,after;pf_inst_take(&before);
        render_owned(&creator,mode==1?1:MESHES,mode==2?2:1);
        REQUIRE(!creator && !lifetime.destroyed && lifetime.refs==1);
        UINT offset=0,stride=0;
        HR(IDirect3DDevice9_GetStreamSource(g_dev,1,&found,&offset,&stride));
        REQUIRE(found==identity && offset==12 && stride==16);IDirect3DVertexBuffer9_Release(found);
        if(mode==0) {
            /* A following group must acquire/release its own reference. */
            render_owned(&creator,MESHES,1);
            HR(IDirect3DDevice9_GetRenderTargetData(g_dev,g_rt,g_sys));
            D3DLOCKED_RECT lr;HR(IDirect3DSurface9_LockRect(g_sys,&lr,NULL,D3DLOCK_READONLY));
            for(unsigned y=0;y<RT;y++)memcpy(pixels+y*RT,(char *)lr.pBits+y*lr.Pitch,RT*4);
            HR(IDirect3DSurface9_UnlockRect(g_sys));
            REQUIRE(!memcmp(reference,pixels,sizeof reference));
            pf_inst_take(&after);REQUIRE(after.meshes==2*(MESHES-1) && after.leaders==2);
        }
        /* Complete/refused groups must release without initialization
         * masking a leak. Only the deliberately unfinished group needs it. */
        if(mode==1)pf_inst_init(0);
        HR(IDirect3DDevice9_SetStreamSource(g_dev,1,NULL,0,0));drain_gpu();
        REQUIRE(lifetime.destroyed && !lifetime.refs);
        printf("GPU_OWNERSHIP,mode,%u,destroyed_after_unbind,1\n",mode);
    }
    REQUIRE(bench_no_game());return 0;
}
