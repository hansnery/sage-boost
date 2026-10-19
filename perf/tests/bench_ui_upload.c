/* Upload-strategy screen, not a native UI replacement or gameplay benchmark. */
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bench_guard.h"

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"benchmark failed: %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
#define HR(x) REQUIRE(SUCCEEDED(x))
#define SIDE 512
#define CAPACITY 20000
#define VERTICES 24
#define ROUNDS 21
#define FRAMES 8
#define FVF (D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_SPECULAR|D3DFVF_TEX1)
#define ENTRY_FVF (D3DFVF_XYZ|D3DFVF_DIFFUSE)
typedef struct { float x,y,z,w; DWORD color,specular; float u,v; } vertex;
typedef struct { unsigned used,pending,locks,draws,wraps; int staged; } batch;
static IDirect3DDevice9 *device;
static IDirect3DVertexBuffer9 *buffer,*sentinel;
static IDirect3DTexture9 *textures[2];
static IDirect3DSurface9 *target,*readback;
static IDirect3DQuery9 *event;
static IDirect3DStateBlock9 *outer;
static vertex scratch[CAPACITY];
static DWORD pixels[3][SIDE*SIDE];
static LARGE_INTEGER frequency;

static void guard(void)
{
    REQUIRE(bench_no_game());
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    REQUIRE(snapshot!=INVALID_HANDLE_VALUE);
    PROCESSENTRY32W p={0};p.dwSize=sizeof p;
    BOOL more=Process32FirstW(snapshot,&p);
    while(more) {
        WCHAR name[MAX_PATH];lstrcpynW(name,p.szExeFile,MAX_PATH);CharLowerBuffW(name,lstrlenW(name));
        REQUIRE(!(wcsstr(name,L"realms") && wcsstr(name,L"launcher")));
        REQUIRE(wcscmp(name,L"aotr_launcher.exe") != 0);
        REQUIRE(wcscmp(name,L"aotrlauncher.exe") != 0);
        more=Process32NextW(snapshot,&p);
    }
    REQUIRE(GetLastError()==ERROR_NO_MORE_FILES);
    CloseHandle(snapshot);
}

static long long ticks(void) { LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart; }
static void drain(void)
{
    HR(IDirect3DQuery9_Issue(event,D3DISSUE_END));
    long long start=ticks();
    HRESULT result;
    while((result=IDirect3DQuery9_GetData(event,NULL,0,D3DGETDATA_FLUSH))==S_FALSE) {
        REQUIRE(ticks()-start < frequency.QuadPart*10);Sleep(0);
    }
    HR(result);
}

static void geometry(vertex *out,unsigned request,unsigned phase)
{
    static const unsigned corner[6][2]={{0,0},{1,0},{0,1},{0,1},{1,0},{1,1}};
    for(unsigned quad=0;quad<4;quad++) {
        unsigned id=request*4+quad;
        float x=(float)((id*37+phase*3)%490),y=(float)((id*19+phase*5)%490);
        for(unsigned i=0;i<6;i++) {
            vertex *v=&out[quad*6+i];
            *v=(vertex){x+corner[i][0]*12.f,y+corner[i][1]*12.f,0.5f,1.f,
                        0x9010c0f0u+((id&31)<<16),0, (float)corner[i][0],(float)corner[i][1]};
        }
    }
}

static void state(unsigned key,unsigned phase,int wrong)
{
    unsigned mode=key%3;
    unsigned texture=(key+phase)%2;
    if(wrong==1)texture^=1;
    HR(IDirect3DDevice9_SetTexture(device,0,(IDirect3DBaseTexture9 *)textures[texture]));
    HR(IDirect3DDevice9_SetRenderState(device,D3DRS_STENCILENABLE,mode!=0));
    HR(IDirect3DDevice9_SetRenderState(device,D3DRS_STENCILFUNC,mode==2?D3DCMP_EQUAL:D3DCMP_ALWAYS));
    HR(IDirect3DDevice9_SetRenderState(device,D3DRS_STENCILPASS,mode==1?D3DSTENCILOP_REPLACE:D3DSTENCILOP_KEEP));
    HR(IDirect3DDevice9_SetRenderState(device,D3DRS_COLORWRITEENABLE,mode==1?0:15));
}

static void flush(batch *b)
{
    if(!b->pending)return;
    if(b->staged) {
        void *dst=NULL;
        HR(IDirect3DVertexBuffer9_Lock(buffer,b->used*sizeof(vertex),b->pending*sizeof(vertex),
                                     &dst,b->used?D3DLOCK_NOOVERWRITE:D3DLOCK_DISCARD));
        memcpy(dst,scratch,b->pending*sizeof(vertex));
        HR(IDirect3DVertexBuffer9_Unlock(buffer));b->locks++;
    }
    HR(IDirect3DDevice9_DrawPrimitive(device,D3DPT_TRIANGLELIST,b->used,b->pending/3));
    b->draws++;b->used+=b->pending;b->pending=0;
}

static void request(batch *b,unsigned index,unsigned phase)
{
    if(b->used+b->pending+VERTICES>CAPACITY) {flush(b);b->used=0;b->wraps++;}
    vertex *dst;
    if(b->staged)dst=scratch+b->pending;
    else {
        HR(IDirect3DVertexBuffer9_Lock(buffer,(b->used+b->pending)*sizeof(vertex),VERTICES*sizeof(vertex),
                                     (void **)&dst,b->used+b->pending?D3DLOCK_NOOVERWRITE:D3DLOCK_DISCARD));
        b->locks++;
    }
    geometry(dst,index,phase);
    if(!b->staged)HR(IDirect3DVertexBuffer9_Unlock(buffer));
    b->pending+=VERTICES;
}

static double frame(int staged,unsigned requests,unsigned group,unsigned phase,int wrong,batch *stats)
{
    batch b={0};b.staged=staged;
    drain();long long begin=ticks();
    HR(IDirect3DStateBlock9_Capture(outer));
    HR(IDirect3DDevice9_BeginScene(device));
    HR(IDirect3DDevice9_Clear(device,0,NULL,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xff102030,1,0));
    HR(IDirect3DDevice9_SetFVF(device,FVF));
    HR(IDirect3DDevice9_SetStreamSource(device,0,buffer,0,sizeof(vertex)));
    HR(IDirect3DDevice9_SetRenderState(device,D3DRS_ALPHABLENDENABLE,TRUE));
    for(unsigned i=0;i<requests;i++) {
        if(i%group==0) {flush(&b);state(i/group,phase,wrong);}
        request(&b,i,wrong==2?0:phase);
    }
    flush(&b);
    HR(IDirect3DDevice9_EndScene(device));
    HR(IDirect3DStateBlock9_Apply(outer));
    drain();
    double elapsed=(ticks()-begin)*1000.0/frequency.QuadPart;
    if(stats)*stats=b;
    return elapsed;
}

static void image(DWORD *dest)
{
    HR(IDirect3DDevice9_GetRenderTargetData(device,target,readback));
    D3DLOCKED_RECT lock;
    HR(IDirect3DSurface9_LockRect(readback,&lock,NULL,D3DLOCK_READONLY));
    for(unsigned y=0;y<SIDE;y++)memcpy(dest+y*SIDE,(char *)lock.pBits+y*lock.Pitch,SIDE*4);
    HR(IDirect3DSurface9_UnlockRect(readback));
}

static void restored(void)
{
    DWORD value;
    HR(IDirect3DDevice9_GetRenderState(device,D3DRS_ALPHABLENDENABLE,&value));REQUIRE(!value);
    HR(IDirect3DDevice9_GetRenderState(device,D3DRS_STENCILENABLE,&value));REQUIRE(!value);
    HR(IDirect3DDevice9_GetRenderState(device,D3DRS_STENCILFUNC,&value));REQUIRE(value==D3DCMP_ALWAYS);
    HR(IDirect3DDevice9_GetRenderState(device,D3DRS_STENCILPASS,&value));REQUIRE(value==D3DSTENCILOP_KEEP);
    HR(IDirect3DDevice9_GetRenderState(device,D3DRS_COLORWRITEENABLE,&value));REQUIRE(value==15);
    HR(IDirect3DDevice9_GetFVF(device,&value));REQUIRE(value==ENTRY_FVF);
    IDirect3DBaseTexture9 *texture=NULL;
    HR(IDirect3DDevice9_GetTexture(device,0,&texture));REQUIRE(texture==(IDirect3DBaseTexture9 *)textures[1]);
    IDirect3DBaseTexture9_Release(texture);
    IDirect3DVertexBuffer9 *stream=NULL;UINT offset,stride;
    HR(IDirect3DDevice9_GetStreamSource(device,0,&stream,&offset,&stride));
    REQUIRE(stream==sentinel && !offset && stride==sizeof(vertex));IDirect3DVertexBuffer9_Release(stream);
}

static void controls(unsigned requests,unsigned group)
{
    batch direct,staged;
    for(unsigned phase=1;phase<=3;phase++) {
        frame(0,requests,group,phase,0,&direct);image(pixels[0]);restored();
        frame(1,requests,group,phase,0,&staged);image(pixels[1]);restored();
        REQUIRE(!memcmp(pixels[0],pixels[1],sizeof pixels[0]));
        REQUIRE(direct.draws==staged.draws && direct.wraps==staged.wraps);
        REQUIRE(direct.locks==requests && staged.locks==staged.draws);
        REQUIRE(direct.wraps==(requests==1024?1u:0u));
        unsigned covered=0;for(unsigned i=0;i<SIDE*SIDE;i++)covered+=pixels[0][i]!=0xff102030;
        REQUIRE(covered>1000);
        frame(1,requests,group,phase,2,NULL);image(pixels[2]);restored();
        REQUIRE(memcmp(pixels[0],pixels[2],sizeof pixels[0]));
        frame(1,requests,group,phase,1,NULL);image(pixels[2]);
        REQUIRE(memcmp(pixels[0],pixels[2],sizeof pixels[0]));restored();
    }
    printf("UI_CONTROLS,requests,%u,group,%u,direct_locks,%u,staged_locks,%u,draws,%u,wraps,%u,phases,3,exact_images,3,nonempty_images,3,stale_rejected,3,state_fault_rejected,3,restoration_checks,12\n",
           requests,group,direct.locks,staged.locks,direct.draws,direct.wraps);
}

static double sample(int staged,unsigned requests,unsigned group)
{
    double total=0;for(unsigned i=0;i<FRAMES;i++)total+=frame(staged,requests,group,1+i%3,0,NULL);
    return total/FRAMES;
}

int main(int argc,char **argv)
{
    REQUIRE(argc==2);guard();REQUIRE(sizeof(vertex)==32);QueryPerformanceFrequency(&frequency);
    WCHAR path[MAX_PATH];
    if(!strcmp(argv[1],"native")) {GetSystemDirectoryW(path,MAX_PATH);lstrcatW(path,L"\\d3d9.dll");}
    else REQUIRE(MultiByteToWideChar(CP_UTF8,0,argv[1],-1,path,MAX_PATH));
    HMODULE module=LoadLibraryW(path);REQUIRE(module);
    typedef IDirect3D9 *(WINAPI *create_fn)(UINT);
    create_fn create=(create_fn)(void *)GetProcAddress(module,"Direct3DCreate9");REQUIRE(create);
    IDirect3D9 *d3d=create(D3D_SDK_VERSION);REQUIRE(d3d);
    HWND window=CreateWindowExW(0,L"STATIC",L"sage-boost offline UI upload",WS_OVERLAPPED,0,0,SIDE,SIDE,NULL,NULL,GetModuleHandleW(NULL),NULL);
    REQUIRE(window);
    D3DPRESENT_PARAMETERS pp={0};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=window;pp.BackBufferWidth=SIDE;pp.BackBufferHeight=SIDE;
    pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24S8;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    HR(IDirect3D9_CreateDevice(d3d,D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device));
    HR(IDirect3DDevice9_CreateRenderTarget(device,SIDE,SIDE,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,NULL));
    HR(IDirect3DDevice9_SetRenderTarget(device,0,target));
    HR(IDirect3DDevice9_CreateOffscreenPlainSurface(device,SIDE,SIDE,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,NULL));
    HR(IDirect3DDevice9_CreateVertexBuffer(device,CAPACITY*sizeof(vertex),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,FVF,D3DPOOL_DEFAULT,&buffer,NULL));
    HR(IDirect3DDevice9_CreateVertexBuffer(device,sizeof(vertex),D3DUSAGE_WRITEONLY,FVF,D3DPOOL_MANAGED,&sentinel,NULL));
    for(unsigned t=0;t<2;t++) {
        HR(IDirect3DDevice9_CreateTexture(device,4,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&textures[t],NULL));
        D3DLOCKED_RECT lock;HR(IDirect3DTexture9_LockRect(textures[t],0,&lock,NULL,0));
        for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)
            *(DWORD *)((char *)lock.pBits+y*lock.Pitch+x*4)=t?0xe0ff2080u:0xb020ff40u;
        HR(IDirect3DTexture9_UnlockRect(textures[t],0));
    }
    HR(IDirect3DDevice9_CreateQuery(device,D3DQUERYTYPE_EVENT,&event));
    HR(IDirect3DDevice9_SetRenderState(device,D3DRS_LIGHTING,FALSE));
    HR(IDirect3DDevice9_SetRenderState(device,D3DRS_ALPHABLENDENABLE,FALSE));
    HR(IDirect3DDevice9_SetRenderState(device,D3DRS_STENCILENABLE,FALSE));
    HR(IDirect3DDevice9_SetRenderState(device,D3DRS_STENCILFUNC,D3DCMP_ALWAYS));
    HR(IDirect3DDevice9_SetRenderState(device,D3DRS_STENCILPASS,D3DSTENCILOP_KEEP));
    HR(IDirect3DDevice9_SetRenderState(device,D3DRS_COLORWRITEENABLE,15));
    HR(IDirect3DDevice9_SetRenderState(device,D3DRS_ZENABLE,FALSE));
    HR(IDirect3DDevice9_SetRenderState(device,D3DRS_CULLMODE,D3DCULL_NONE));
    HR(IDirect3DDevice9_SetRenderState(device,D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));
    HR(IDirect3DDevice9_SetRenderState(device,D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
    HR(IDirect3DDevice9_SetRenderState(device,D3DRS_STENCILREF,1));
    HR(IDirect3DDevice9_SetRenderState(device,D3DRS_STENCILFAIL,D3DSTENCILOP_KEEP));
    HR(IDirect3DDevice9_SetRenderState(device,D3DRS_STENCILZFAIL,D3DSTENCILOP_KEEP));
    HR(IDirect3DDevice9_SetTextureStageState(device,0,D3DTSS_COLOROP,D3DTOP_MODULATE));
    HR(IDirect3DDevice9_SetTextureStageState(device,0,D3DTSS_COLORARG1,D3DTA_TEXTURE));
    HR(IDirect3DDevice9_SetTextureStageState(device,0,D3DTSS_COLORARG2,D3DTA_DIFFUSE));
    HR(IDirect3DDevice9_SetTextureStageState(device,0,D3DTSS_ALPHAOP,D3DTOP_MODULATE));
    HR(IDirect3DDevice9_SetTextureStageState(device,0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE));
    HR(IDirect3DDevice9_SetTextureStageState(device,0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE));
    HR(IDirect3DDevice9_SetTexture(device,0,(IDirect3DBaseTexture9 *)textures[1]));
    HR(IDirect3DDevice9_SetStreamSource(device,0,sentinel,0,sizeof(vertex)));
    HR(IDirect3DDevice9_SetFVF(device,ENTRY_FVF));
    HR(IDirect3DDevice9_CreateStateBlock(device,D3DSBT_ALL,&outer));
    char loaded[MAX_PATH];GetModuleFileNameA(module,loaded,MAX_PATH);
    printf("UI_BACKEND,%s,%s\n",!strcmp(argv[1],"native")?"native":"dxvk",loaded);
    const unsigned counts[]={64,256,1024},groups[]={1,16,64};
    for(unsigned c=0;c<3;c++)for(unsigned g=0;g<3;g++) {
        controls(counts[c],groups[g]);
        for(unsigned warm=0;warm<3;warm++) {sample(0,counts[c],groups[g]);sample(1,counts[c],groups[g]);}
        for(unsigned round=0;round<ROUNDS;round++) {
            guard();double a,b;
            if(round&1) {b=sample(1,counts[c],groups[g]);a=sample(0,counts[c],groups[g]);}
            else {a=sample(0,counts[c],groups[g]);b=sample(1,counts[c],groups[g]);}
            printf("UI_SAMPLE,%u,%u,%u,%.6f,%.6f,%.6f\n",counts[c],groups[g],round,a,b,a-b);
            fflush(stdout);
        }
        restored();guard();
    }
    puts("UI_UPLOAD_PASS");
    IDirect3DStateBlock9_Release(outer);IDirect3DQuery9_Release(event);
    IDirect3DSurface9_Release(readback);IDirect3DSurface9_Release(target);
    IDirect3DVertexBuffer9_Release(buffer);IDirect3DVertexBuffer9_Release(sentinel);
    for(unsigned t=0;t<2;t++)IDirect3DTexture9_Release(textures[t]);
    IDirect3DDevice9_Release(device);IDirect3D9_Release(d3d);DestroyWindow(window);FreeLibrary(module);
    return 0;
}
