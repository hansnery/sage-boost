/* Offline device lifecycle over the same native-pose/render workload.
 * No runtime hook or native engine object is moved between threads. */
#define PF_WORKER_RESET
#define PF_WORKER_MAIN unused_worker_benchmark_main
#include "bench_batch_worker.c"
#ifdef PF_PACKET_CONSUMER
#include "packet_consumer.h"
#endif

extern int bench_inst_release_resources(void);
extern void material_fixture_lost(void);
extern void material_fixture_reset(void);
extern void material_fixture_destroy(void);

static unsigned reset_state[WORKER_STATE_WORDS],reset_words;
static IDirect3DPixelShader9 *reset_pixel;
static D3DPRESENT_PARAMETERS reset_parameters;
static unsigned completed_resets,failed_results,closed_refusals;

static void reset_barrier(void)
{
    REQUIRE(!accepting_packets && submitted==collected);
    for(unsigned i=0;i<WORKER_SLOTS;i++)REQUIRE(slots[i].state==SLOT_FREE);
}

static void worker_resources_release(void)
{
    REQUIRE(GetCurrentThreadId()==render_thread);
    reset_barrier();
    drain();reset_words=backend_state(reset_state);
    REQUIRE(!reset_pixel);
    HR(IDirect3DDevice9_GetPixelShader(g_dev,&reset_pixel));
    bench_retained_clear();
    IDirect3DStateBlock9_Release(outer);outer=NULL;
    material_fixture_lost();
#ifdef PF_PACKET_CONSUMER
    packet_consumer_close();
#else
    REQUIRE(bench_inst_release_resources());
#endif
    IDirect3DQuery9_Release(fence);fence=NULL;
    HR(IDirect3DDevice9_SetDepthStencilSurface(g_dev,NULL));
    IDirect3DSurface9 *back=NULL;
    HR(IDirect3DDevice9_GetBackBuffer(g_dev,0,0,D3DBACKBUFFER_TYPE_MONO,&back));
    HR(IDirect3DDevice9_SetRenderTarget(g_dev,0,back));
    IDirect3DSurface9_Release(back);
    for(unsigned i=0;i<16;i++)HR(IDirect3DDevice9_SetTexture(g_dev,i,NULL));
    for(unsigned i=0;i<2;i++)HR(IDirect3DDevice9_SetStreamSource(g_dev,i,NULL,0,0));
    HR(IDirect3DDevice9_SetIndices(g_dev,NULL));
    HR(IDirect3DDevice9_SetVertexShader(g_dev,NULL));
    HR(IDirect3DDevice9_SetPixelShader(g_dev,NULL));
    HR(IDirect3DDevice9_SetVertexDeclaration(g_dev,NULL));
    REQUIRE(IDirect3DSurface9_Release(g_rt)==0);g_rt=NULL;
    if(g_sds){REQUIRE(IDirect3DSurface9_Release(g_sds)==0);g_sds=NULL;}
}

static void restore_numeric_state(void)
{
    /* Copy values, not a state block that would hold resources across Reset.
     * This is fixture state restoration, not the game's default-state code. */
    unsigned n=0;
    HR(IDirect3DDevice9_SetVertexShaderConstantF(g_dev,0,(float *)(reset_state+n),256));n+=1024;
    HR(IDirect3DDevice9_SetPixelShaderConstantF(g_dev,0,(float *)(reset_state+n),32));n+=128;
    static const D3DRENDERSTATETYPE keys[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,
        D3DRS_ZFUNC,D3DRS_CULLMODE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAREF,
        D3DRS_ALPHAFUNC,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,
        D3DRS_COLORWRITEENABLE,D3DRS_STENCILENABLE};
    for(unsigned i=0;i<sizeof keys/sizeof *keys;i++)HR(IDirect3DDevice9_SetRenderState(g_dev,keys[i],reset_state[n++]));
    for(unsigned slot=0;slot<8;slot++)for(unsigned key=1;key<=13;key++)
        HR(IDirect3DDevice9_SetSamplerState(g_dev,slot,key,reset_state[n++]));
    for(unsigned slot=0;slot<2;slot++)HR(IDirect3DDevice9_SetStreamSourceFreq(g_dev,slot,reset_state[n++]));
    REQUIRE(n==reset_words);
}

static void owner_resources_recreate(void)
{
    REQUIRE(GetCurrentThreadId()==window_thread && !accepting_packets && !g_rt && !g_sds);
    HR(IDirect3DDevice9_CreateRenderTarget(g_dev,RT,RT,shadow_mode?D3DFMT_R32F:D3DFMT_A8R8G8B8,
        D3DMULTISAMPLE_NONE,0,FALSE,&g_rt,NULL));
    HR(IDirect3DDevice9_SetRenderTarget(g_dev,0,g_rt));
    if(shadow_mode) {
        HR(IDirect3DDevice9_CreateDepthStencilSurface(g_dev,RT,RT,D3DFMT_D24S8,
            D3DMULTISAMPLE_NONE,0,TRUE,&g_sds,NULL));
        HR(IDirect3DDevice9_SetDepthStencilSurface(g_dev,g_sds));
    }
    HR(IDirect3DDevice9_SetPixelShader(g_dev,reset_pixel));
    if(reset_pixel)IDirect3DPixelShader9_Release(reset_pixel);
    reset_pixel=NULL;
    restore_numeric_state();
    material_fixture_reset();
    HR(IDirect3DDevice9_CreateQuery(g_dev,D3DQUERYTYPE_EVENT,&fence));
    HR(IDirect3DDevice9_CreateStateBlock(g_dev,D3DSBT_ALL,&outer));
}

static HANDLE start_worker(void)
{
    REQUIRE(GetCurrentThreadId()==window_thread);
    InterlockedExchange(&stopping,0);REQUIRE(ResetEvent(initialized));
    HANDLE worker=CreateThread(NULL,0,worker_main,NULL,0,NULL);REQUIRE(worker);
    REQUIRE(wait_owner(initialized,60000)==WAIT_OBJECT_0);
    return worker;
}

static void stop_worker(HANDLE worker)
{
    reset_barrier();
    InterlockedExchange(&stopping,1);REQUIRE(ReleaseSemaphore(available,1,NULL));
    REQUIRE(wait_owner(worker,10000)==WAIT_OBJECT_0);
    DWORD code;REQUIRE(GetExitCodeThread(worker,&code) && !code);CloseHandle(worker);
    REQUIRE(owner_messages==submitted);
#ifdef PF_PACKET_CONSUMER
    REQUIRE(bench_scope_owner_idle(window_thread));
    REQUIRE(bench_inst_release_resources());
#else
    REQUIRE(bench_scope_transfer_owner(render_thread));
    REQUIRE(bench_inst_transfer_owner(render_thread));
#endif
}

static int reset_result(HRESULT result)
{
    REQUIRE(GetCurrentThreadId()==window_thread);
    reset_barrier();
    if(FAILED(result)){failed_results++;return 0;}
    resource_generation++;completed_resets++;
    return 1;
}

int main(int argc,char **argv)
{
    REQUIRE((argc==9 || argc==10) && bench_no_game());
    REQUIRE(!strcmp(argv[3],"lit") || !strcmp(argv[3],"shadow"));
    REQUIRE(!strcmp(argv[4],"owned") || !strcmp(argv[4],"checked"));
    REQUIRE((argv[8][0]=='0' || argv[8][0]=='1') && !argv[8][1]);
    const char *fault=argc==10?argv[9]:"";
    unsigned early_reset=!strcmp(fault,"reset-before-drain");
    skip_generation_check=!strcmp(fault,"stale-generation");
    stale_pose_values=!strcmp(fault,"stale-pose");stale_material_values=!strcmp(fault,"stale-material");
    wrong_source_setter=!strcmp(fault,"wrong-setter");stale_source_values=!strcmp(fault,"stale-source");
    unsigned no_mt=!strcmp(fault,"device-flags");
#ifdef PF_PACKET_CONSUMER
    consumer_shared_extra=!strcmp(fault,"shared-extra");
    consumer_transfer_timing=!strcmp(fault,"frame-owner");
    if(!consumer_shared_extra && !consumer_transfer_timing)
#endif
    REQUIRE(!*fault || early_reset || skip_generation_check || stale_pose_values || stale_material_values || wrong_source_setter || stale_source_values || no_mt);
    if(no_mt)fixture_device_flags&=~D3DCREATE_MULTITHREADED;
    native_pose_init(argv[5],argv[7],(unsigned)(argv[8][0]-'0'));
    integrity_checks=!strcmp(argv[4],"checked");worker_argc=argc;worker_argv=argv;
    available=CreateSemaphoreW(NULL,0,WORKER_SLOTS,NULL);
    initialized=CreateEventW(NULL,TRUE,FALSE,NULL);consume_gate=CreateEventW(NULL,TRUE,TRUE,NULL);
    REQUIRE(available && initialized && consume_gate);
    for(unsigned i=0;i<WORKER_SLOTS;i++){slots[i].done=CreateEventW(NULL,FALSE,FALSE,NULL);REQUIRE(slots[i].done);}
    window_thread=GetCurrentThreadId();initialize_worker();verify_window_owner();
#ifdef PF_PACKET_CONSUMER
    pf_timing_stats producer_before;pf_timing_get_stats(&producer_before);
    pf_inst_counts native_before;pf_inst_totals(&native_before);
    pf_timing_set_inst(pf_inst_enter,pf_inst_take);
    unsigned original_refs=g_method.refs;
    consumer_inside=CreateEventW(NULL,FALSE,FALSE,NULL);
    consumer_continue=CreateEventW(NULL,FALSE,FALSE,NULL);
    REQUIRE(consumer_inside && consumer_continue);consumer_probe_pending=1;
#endif
    IDirect3DSwapChain9 *chain=NULL;
    HR(IDirect3DDevice9_GetSwapChain(g_dev,0,&chain));
    HR(IDirect3DSwapChain9_GetPresentParameters(chain,&reset_parameters));
    IDirect3DSwapChain9_Release(chain);
    native_source_capture_init(argv[5],argv[6],material_runtime);
    HANDLE worker=start_worker();
    ownership_controls();
    for(unsigned cycle=0;cycle<3;cycle++) {
        REQUIRE(bench_no_game());
        REQUIRE(ResetEvent(consume_gate));
        prepare(0,0,0,1,0,0);REQUIRE(publish(0,0));
        prepare(1,0,0,1,1,0);REQUIRE(publish(0,0));
        accepting_packets=0;
        unsigned old_submitted=submitted,old_generation=resource_generation;
        REQUIRE(!publish(0,0) && submitted==old_submitted);closed_refusals++;
        if(early_reset)reset_barrier(); /* Must fail before any teardown. */
        REQUIRE(SetEvent(consume_gate));collect(0,NULL,NULL);collect(0,NULL,NULL);
        stop_worker(worker);
        /* Fault injection tests result handling, not a real lost device. */
        REQUIRE(!reset_result(D3DERR_DEVICELOST));
        REQUIRE(resource_generation==old_generation && !accepting_packets && !g_rt && !fence);
        REQUIRE(!publish(0,0) && submitted==old_submitted);closed_refusals++;
        HR(IDirect3DDevice9_TestCooperativeLevel(g_dev));
        D3DPRESENT_PARAMETERS parameters=reset_parameters;
        HRESULT result=IDirect3DDevice9_Reset(g_dev,&parameters);
        printf("RESET CALL: cycle=%u hr=%08lX thread=%lu\n",cycle,(unsigned long)result,(unsigned long)GetCurrentThreadId());
        HR(result);REQUIRE(reset_result(result));
        owner_resources_recreate();worker=start_worker();accepting_packets=1;
        prepare(0,0,0,1,0,0);scratch.resource_generation=old_generation;
        REQUIRE(publish(0,0));collect(2,NULL,NULL); /* Valid checksum, stale resources. */
        ownership_controls();
    }
    accepting_packets=0;stop_worker(worker);
#ifdef PF_PACKET_CONSUMER
    pf_timing_stats producer_after;pf_timing_get_stats(&producer_after);
    pf_inst_counts native_after;pf_inst_totals(&native_after);
    REQUIRE(producer_preparations==submitted && producer_probes==3 && !consumer_probe_pending);
    REQUIRE(producer_after.frames-producer_before.frames==producer_preparations+producer_probes);
    REQUIRE(producer_after.render_calls-producer_before.render_calls==producer_preparations);
    REQUIRE(producer_after.scopes==producer_before.scopes && producer_after.stale==producer_before.stale &&
            producer_after.stale_scopes==producer_before.stale_scopes && g_method.refs==original_refs);
    REQUIRE(!memcmp(&native_before,&native_after,sizeof native_before));
    REQUIRE(consumer.frames[0]+consumer.frames[1]==30);
    for(unsigned i=0;i<2;i++)REQUIRE(consumer.groups[i]==consumer.frames[i]*GROUPS &&
        consumer.meshes[i]==consumer.groups[i]*GROUP_SIZE &&
        consumer.draws[i]==consumer.groups[i]*(1+fixture_batches(GROUP_SIZE-1)));
    printf("SEPARATE CONSUMER: producer_frames=%u producer_render=%u concurrent_probes=%u worker_frames=%u/%u meshes=%u/%u; native method references and frame counters isolated\n",
        producer_after.frames-producer_before.frames,producer_preparations,producer_probes,
        consumer.frames[0],consumer.frames[1],consumer.meshes[0],consumer.meshes[1]);
    CloseHandle(consumer_inside);CloseHandle(consumer_continue);
#endif
    HR(IDirect3DDevice9_TestCooperativeLevel(g_dev));
    material_fixture_destroy();
    if(reset_pixel)IDirect3DPixelShader9_Release(reset_pixel);
    reset_pixel=NULL;
    IDirect3DSurface9_Release(g_sys);g_sys=NULL;
    IDirect3DVertexBuffer9_Release(g_vb);g_vb=NULL;
    IDirect3DIndexBuffer9_Release(g_ib);g_ib=NULL;
    IDirect3DVertexShader9_Release(g_ea);g_ea=NULL;
    IDirect3DVertexDeclaration9_Release(g_decl);g_decl=NULL;
    ULONG remaining=IDirect3DDevice9_Release(g_dev);g_dev=NULL;
    printf("DEVICE RELEASE: remaining=%lu thread=%lu\n",(unsigned long)remaining,(unsigned long)GetCurrentThreadId());
    REQUIRE(!remaining);
    REQUIRE((WNDPROC)SetWindowLongPtrW(owner_window,GWLP_WNDPROC,(LONG_PTR)owner_previous_proc)==owner_window_proc);
    REQUIRE(DestroyWindow(owner_window));
    for(unsigned i=0;i<WORKER_SLOTS;i++)CloseHandle(slots[i].done);
    CloseHandle(available);CloseHandle(initialized);CloseHandle(consume_gate);
    REQUIRE(submitted==37 && collected==37 && owner_messages==37);
    REQUIRE(native_source_capture_calls()==submitted*GROUPS*3);
    native_source_capture_close();native_pose_close(submitted+WORKER_ORACLES);
    printf("LIFECYCLE: resets=%u injected_failed_results=%u closed_refusals=%u stale_rejections=3 exact_images=30 generations=%u packets=%u; final device release on window thread\n",
        completed_resets,failed_results,closed_refusals,resource_generation,submitted);
    puts("LIMITS: fixture resources and copied numeric state; failed HRESULT injected, no actual device loss; no native Reset hook, live engine lifecycle or gameplay FPS");
    REQUIRE(bench_no_game());return 0;
}
