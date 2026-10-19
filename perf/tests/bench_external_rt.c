/* External binary evaluation only. No Accelerator implementation is included
 * or linked, and this executable must never be installed into the game. */
#define PF_EFFECT_MAIN unused_effect_screen_main
#include "bench_effect.c"

typedef int (__cdecl *external_install_fn)(void *,void *);
typedef void (__cdecl *external_toggle_fn)(int);
typedef void (__cdecl *external_stats_fn)(char *,int);
static external_toggle_fn toggle;
static external_stats_fn stats,stats4;
static IDirect3DDevice9Vtbl original;
#define RT_GROUPS 1222
#define RT_REPEATS 4
#define RT_SAMPLES 21
#define SNAP_WORDS 1400
static const D3DRENDERSTATETYPE render_keys[]={
    D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_CULLMODE,
    D3DRS_ALPHATESTENABLE,D3DRS_ALPHAREF,D3DRS_ALPHAFUNC,
    D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,
    D3DRS_COLORWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_STENCILFUNC,
    D3DRS_STENCILREF,D3DRS_STENCILMASK,D3DRS_STENCILWRITEMASK,
    D3DRS_STENCILFAIL,D3DRS_STENCILZFAIL,D3DRS_STENCILPASS,
    D3DRS_FOGENABLE,D3DRS_LIGHTING,D3DRS_SCISSORTESTENABLE,
    D3DRS_DEPTHBIAS,D3DRS_SLOPESCALEDEPTHBIAS
};

static void queue_check(int active)
{
    char brief[1024]={0},detail[8192]={0};
    unsigned seq=0,executed=0;
    if(!stats)return;
    stats(brief,sizeof brief);stats4(detail,sizeof detail);
    char *s=strstr(detail,"seq=");
    REQUIRE(s && sscanf(s,"seq=%u exec=%u",&seq,&executed)==2);
    if(seq!=executed || !strstr(brief,"queued=0"))
        fprintf(stderr,"QUEUE CHECK: %s\n%s\n",brief,detail);
    REQUIRE(seq==executed && strstr(brief,"queued=0"));
    if(active)REQUIRE(strstr(brief,"active=1") && executed);
}

/* Bypass the external wrapper's getters: inspect the actual backend after
 * draining the worker and GPU, not a cached answer from its state mirror. */
static unsigned snapshot(unsigned out[SNAP_WORDS])
{
    unsigned n=0;
    IDirect3DVertexShader9 *vs=NULL;IDirect3DPixelShader9 *ps=NULL;
    drain();queue_check(0);
    HR(original.GetVertexShader(device,&vs));
    HR(original.GetPixelShader(device,&ps));
    out[n++]=shader_hash(vs,0);out[n++]=shader_hash(ps,1);
    if(vs)IDirect3DVertexShader9_Release(vs);
    if(ps)IDirect3DPixelShader9_Release(ps);
    HR(original.GetVertexShaderConstantF(device,0,(float*)(out+n),256));n+=1024;
    HR(original.GetPixelShaderConstantF(device,0,(float*)(out+n),32));n+=128;
    for(unsigned i=0;i<sizeof render_keys/sizeof *render_keys;i++) {
        DWORD value=0;HR(original.GetRenderState(device,render_keys[i],&value));out[n++]=value;
    }
    for(unsigned slot=0;slot<8;slot++)for(unsigned key=1;key<=13;key++) {
        DWORD value=0;HR(original.GetSamplerState(device,slot,key,&value));out[n++]=value;
    }
    /* Shader GetFunction/Release calls above also pass through the external
     * resource wrappers. Finish those before exposing an idle-state oracle. */
    drain();queue_check(0);
    REQUIRE(n<=SNAP_WORDS);return n;
}

static void seed(void)
{
    static const float zero[256*4]={0};
    drain();queue_check(0);
    HR(original.SetVertexShaderConstantF(device,0,zero,256));
    HR(original.SetPixelShaderConstantF(device,0,zero,32));
    HR(original.SetRenderState(device,D3DRS_CULLMODE,D3DCULL_CW));
}

static void observe(ID3DXEffect *fx,input *inputs,unsigned count,unsigned variant,
                    unsigned during[SNAP_WORDS],unsigned after[SNAP_WORDS],unsigned *words)
{
    UINT passes=0;
    seed();HR(IDirect3DDevice9_BeginScene(device));
    set_inputs(fx,0,inputs,count,variant);
    HR(FXCALL(fx,Begin,&passes,6));REQUIRE(passes==1);
    HR(FXCALL(fx,BeginPass,0));HR(FXCALL(fx,CommitChanges));
    *words=snapshot(during);
    HR(FXCALL(fx,EndPass));HR(FXCALL(fx,End));
    REQUIRE(snapshot(after)==*words);
    HR(IDirect3DDevice9_EndScene(device));drain();queue_check(0);
}

static void time_work(ID3DXEffect *fx,input *inputs,unsigned count,int threaded,
                      double *caller,double *complete)
{
    toggle(threaded);drain();
    long long begin=ticks();
    for(unsigned rep=0;rep<RT_REPEATS;rep++) {
        HR(IDirect3DDevice9_BeginScene(device));
        for(unsigned g=0;g<RT_GROUPS;g++) {
            set_inputs(fx,0,inputs,count,g&1);cycle(fx,6);
        }
        HR(IDirect3DDevice9_EndScene(device));
    }
    long long sent=ticks();drain();long long done=ticks();
    queue_check(threaded);
    *caller=(sent-begin)*1000.0/frequency/RT_REPEATS;
    *complete=(done-begin)*1000.0/frequency/RT_REPEATS;
}

int main(int argc,char **argv)
{
    REQUIRE(argc==3 && bench_no_game());
    char path[MAX_PATH];unsigned size=0,pos=16,off=0,len=0,found=0;
    snprintf(path,sizeof path,"%s/Shaders.big",argv[1]);
    unsigned char *big=read_file(path,&size);REQUIRE(big && size>=16);
    REQUIRE(!memcmp(big,"BIGF",4)||!memcmp(big,"BIG4",4));
    unsigned files=be32(big+8);
    for(unsigned i=0;i<files;i++) {
        REQUIRE(pos+8<size);unsigned start=be32(big+pos),bytes=be32(big+pos+4);
        const char *name=(const char*)big+pos+8;REQUIRE(memchr(name,0,size-pos-8));
        pos+=8+(unsigned)strlen(name)+1;
        if(ends_with(name,"defaultw3d.fxo")){off=start;len=bytes;found++;}
    }
    REQUIRE(found==1 && off<=size && len<=size-off);
    snprintf(path,sizeof path,"%s/d3dx9_27.dll",argv[1]);
    HMODULE fxmod=LoadLibraryA(path),d9=LoadLibraryA("d3d9.dll");REQUIRE(fxmod&&d9);
    create_effect_fn create=(create_effect_fn)(void*)GetProcAddress(fxmod,"D3DXCreateEffect");
    create9_fn create9=(create9_fn)(void*)GetProcAddress(d9,"Direct3DCreate9");REQUIRE(create&&create9);
    IDirect3D9 *d3d=create9(D3D_SDK_VERSION);REQUIRE(d3d);
    HWND wnd=CreateWindowExA(0,"STATIC","external_effect_screen",WS_POPUP,0,0,64,64,NULL,NULL,NULL,NULL);
    REQUIRE(wnd);D3DPRESENT_PARAMETERS pp={0};pp.Windowed=TRUE;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=wnd;pp.BackBufferWidth=pp.BackBufferHeight=64;
    HR(IDirect3D9_CreateDevice(d3d,0,D3DDEVTYPE_HAL,wnd,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device));
    original=*device->lpVtbl;
    HR(IDirect3DDevice9_CreateQuery(device,D3DQUERYTYPE_EVENT,&fence));
    LARGE_INTEGER f;QueryPerformanceFrequency(&f);frequency=(double)f.QuadPart;
    const char *names[2]={"Default","_CreateShadowMap"};
    ID3DXEffect *fx[2]={0};input inputs[2][256];unsigned counts[2];
    static unsigned before[2][2][SNAP_WORDS],end[2][2][SNAP_WORDS];unsigned lengths[2][2];
    for(unsigned t=0;t<2;t++) {
        HR(create(device,big+off,len,NULL,NULL,0,NULL,&fx[t],NULL));
        D3DXHANDLE tech=FXCALL(fx[t],GetTechniqueByName,names[t]);REQUIRE(tech);
        HR(FXCALL(fx[t],SetTechnique,tech));
        ID3DXEffect *same[2]={fx[t],fx[t]};D3DXHANDLE same_tech[2]={tech,tech};
        counts[t]=collect_inputs(same,same_tech,inputs[t],256);REQUIRE(counts[t]);
        for(unsigned v=0;v<2;v++)observe(fx[t],inputs[t],counts[t],v,before[t][v],end[t][v],&lengths[t][v]);
        REQUIRE(lengths[t][0]==lengths[t][1] && memcmp(before[t][0],before[t][1],lengths[t][0]*4));
    }
    HMODULE external=LoadLibraryA(argv[2]);REQUIRE(external);
    external_install_fn install=(external_install_fn)(void*)GetProcAddress(external,"AotrRtTestInstall");
    toggle=(external_toggle_fn)(void*)GetProcAddress(external,"AotrRtTestToggle");
    stats=(external_stats_fn)(void*)GetProcAddress(external,"AotrRtTestStats");
    stats4=(external_stats_fn)(void*)GetProcAddress(external,"AotrRtTestStatsV4");
    REQUIRE(install&&toggle&&stats&&stats4);REQUIRE(install(device,fx[0])==1);
    for(unsigned t=0;t<2;t++)for(unsigned mode=0;mode<2;mode++)for(unsigned v=0;v<2;v++) {
        unsigned a[SNAP_WORDS],b[SNAP_WORDS],n;
        toggle(mode);observe(fx[t],inputs[t],counts[t],v,a,b,&n);queue_check(mode);
        if(n!=lengths[t][v] || memcmp(a,before[t][v],n*4) || memcmp(b,end[t][v],n*4)) {
            unsigned first=0;while(first<n && a[first]==before[t][v][first])first++;
            printf("STATE MISMATCH technique=%u mode=%u variant=%u first_inpass_word=%u\n",t,mode,v,first);
            return 2;
        }
    }
    {
        unsigned a[SNAP_WORDS],b[SNAP_WORDS];unsigned n=snapshot(a);
        float poison[4]={9,8,7,6};HR(original.SetVertexShaderConstantF(device,0,poison,1));
        REQUIRE(snapshot(b)==n && memcmp(a,b,n*4));
    }
    puts("STATE: 8 in-pass/post-End comparisons against unhooked backend; changed-input and actual-constant poison controls passed");
    puts("LIMITS: no geometry/draws, native sources or saved-game FPS; timed off mode retains passthrough wrappers");
    puts("technique,round,off_caller_ms,on_caller_ms,off_complete_ms,on_complete_ms,saved_complete_ms");
    for(unsigned t=0;t<2;t++) {
        double saving[RT_SAMPLES];
        for(unsigned r=0;r<RT_SAMPLES+3;r++) {
            double caller[2],complete[2];REQUIRE(bench_no_game());
            for(unsigned j=0;j<2;j++) {
                unsigned mode=j^(r&1);time_work(fx[t],inputs[t],counts[t],mode,&caller[mode],&complete[mode]);
            }
            if(r<3)continue;
            unsigned row=r-3;saving[row]=complete[0]-complete[1];
            printf("sample,%u,%u,%.4f,%.4f,%.4f,%.4f,%.4f\n",t,row,caller[0],caller[1],complete[0],complete[1],saving[row]);
        }
        qsort(saving,RT_SAMPLES,sizeof(double),cmp);
        printf("SUMMARY technique=%u inputs=%u groups=%u median_complete_saving=%.4f ms\n",t,counts[t],RT_GROUPS,saving[RT_SAMPLES/2]);
    }
    drain();queue_check(0);char detail[8192];stats4(detail,sizeof detail);printf("FINAL QUEUE: %s\n",detail);
    toggle(0);REQUIRE(bench_no_game());
    /* External hooks still reference their module. Keep it loaded until process
     * exit; no unloading or production cleanup contract is claimed here. */
    return 0;
}
