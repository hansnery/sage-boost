/* Audit the current batch code, not a recovery implementation. Inject one
 * rejected API operation before forwarding it, on this fixture's device
 * only. Actual rendering/state reads use the original local D3D9 backend. */
#define PF_NATIVE_GROUP_MAIN group_gpu_main
#ifndef PF_NATIVE_GPU_GROUP_SOURCE
#define PF_NATIVE_GPU_GROUP_SOURCE "bench_native_group_gpu.c"
#endif
#include PF_NATIVE_GPU_GROUP_SOURCE
extern int bench_fault_batch_get(unsigned,unsigned);
extern void bench_fault_reset(void);
extern void bench_fault_device(void *);
extern void bench_fault_stock(unsigned);
#ifdef PF_RECOVERY_CANDIDATE
#include "../src/instsubmit.h"
extern void bench_recovery_watch(void (*)(void),void (*)(void));
extern unsigned bench_recovery_operation(void),bench_recovery_open(void);
static unsigned ff_corrupt_mode;
#endif
static void **ff_original;
static void *ff_vtable[sizeof(IDirect3DDevice9Vtbl)/sizeof(void *)];
static struct {void **vt;void *real;} ff_proxy;
static unsigned char *ff_forwarders;
static void *ff_real(void *self) { return self==&ff_proxy?ff_proxy.real:self; }
static unsigned ff_armed,ff_open,ff_batch,ff_step,ff_target_batch,ff_target_step;
static unsigned ff_injected,ff_saved,ff_forwarding,ff_instances,ff_native_dips;
static unsigned ff_state_bad,ff_bad_at_target,ff_batches,ff_fault_cases;
static unsigned ff_negative;
static unsigned ff_allow_fallback;
static unsigned ff_groups=1;
static IDirect3DVertexBuffer9 *ff_seed_vb;
typedef struct {
    unsigned vs,decl,vb[2],offset[2],stride[2],frequency[2],constants[256*4];
#ifdef PF_FAULT_EXTRA_STATE
    unsigned extra[4096];
#endif
} FaultState;
static FaultState ff_before;

static void ff_snapshot(FaultState *out)
{
    IDirect3DVertexShader9 *vs=NULL;
    IDirect3DVertexDeclaration9 *decl=NULL;
    HR(((pf_ptr_fn)ff_original[PF_DEV_GET_VS])(g_dev,(void **)&vs));
    HR(((pf_ptr_fn)ff_original[PF_DEV_GET_DECL])(g_dev,(void **)&decl));
    out->vs=(unsigned)vs; out->decl=(unsigned)decl;
    if(vs) IDirect3DVertexShader9_Release(vs);
    if(decl) IDirect3DVertexDeclaration9_Release(decl);
    for(unsigned s=0;s<2;s++) {
        IDirect3DVertexBuffer9 *vb=NULL;
        HR(((pf_get_stream_fn)ff_original[PF_DEV_GET_STREAM])(g_dev,s,(void **)&vb,
             &out->offset[s],&out->stride[s]));
        out->vb[s]=(unsigned)vb; if(vb) IDirect3DVertexBuffer9_Release(vb);
        HR(((pf_get_freq_fn)ff_original[PF_DEV_GET_STREAM_FREQ])(g_dev,s,&out->frequency[s]));
    }
    HR(((pf_const_f_fn)ff_original[PF_DEV_GET_VS_CONST_F])(g_dev,0,out->constants,256));
#ifdef PF_FAULT_EXTRA_STATE
    memset(out->extra,0,sizeof out->extra);
    out->extra[0]=PF_FAULT_EXTRA_STATE(out->extra+1,4095);
#endif
}

static int ff_reject(unsigned operation)
{
#ifdef PF_RECOVERY_CANDIDATE
    if(ff_armed && !ff_forwarding && bench_recovery_operation()==15) {
        REQUIRE(operation==PF_DEV_GET_VS_CONST_F && !ff_open);
        if(ff_target_step==15 && !ff_injected) { ff_injected=1; return 1; }
        return 0;
    }
#endif
    if(!ff_open || ff_forwarding) return 0;
    ff_step++;
    /* The one-stream fixture's native call order is an independent oracle. */
    static const unsigned order[]={0,PF_DEV_GET_VS_CONST_F,PF_DEV_SET_VS,PF_DEV_SET_DECL,
        PF_DEV_SET_STREAM,PF_DEV_SET_STREAM_FREQ,PF_DEV_SET_STREAM_FREQ,PF_DEV_SET_VS_CONST_F,
        PF_DEV_DRAW_INDEXED,PF_DEV_SET_STREAM_FREQ,PF_DEV_SET_STREAM_FREQ,PF_DEV_SET_STREAM,
        PF_DEV_SET_DECL,PF_DEV_SET_VS,PF_DEV_SET_VS_CONST_F};
#ifdef PF_RECOVERY_CANDIDATE
    if(bench_recovery_open()) {
        ff_step=bench_recovery_operation();
        if(ff_step==19) REQUIRE(operation==PF_DEV_SET_STREAM);
        else if(ff_step>14) return 0; /* Individually replayed draws, separately counted. */
    }
#endif
    if(ff_step!=19) REQUIRE(ff_step<sizeof order/sizeof order[0] && operation==order[ff_step]);
    if(ff_batch==ff_target_batch && ff_step==ff_target_step && !ff_injected) {
        ff_injected=1; return 1;
    }
    return 0;
}
static void ff_close_batch(void)
{
    FaultState after={0}; ff_snapshot(&after);
    unsigned changed=0;
    const unsigned *a=(const unsigned *)&ff_before,*b=(const unsigned *)&after;
    for(unsigned i=0;i<sizeof after/sizeof(unsigned);i++) if(a[i]!=b[i]) {
#ifdef PF_FAULT_EXTRA_STATE
        if(changed<8) fprintf(stderr,"state difference batch=%u word=%u old=%08x new=%08x\n",ff_batch,i,a[i],b[i]);
#endif
        changed++;
    }
    ff_state_bad+=changed;
    if(ff_batch==ff_target_batch) ff_bad_at_target=changed;
    ff_open=0; ff_batches++;
}
static void ff_end_operation(void)
{
#ifdef PF_RECOVERY_CANDIDATE
    if(bench_recovery_open()) return;
#endif
    if(!ff_open || ff_forwarding || ff_step!=(ff_saved?14u:13u)) return;
    ff_close_batch();
}
#ifdef PF_RECOVERY_CANDIDATE
static void ff_recovery_begin(void)
{
    if(!ff_armed) return;
    REQUIRE(!ff_open);
    ff_open=1; ff_batch++; ff_step=0; ff_saved=1;
    ff_snapshot(&ff_before);
}
static void ff_recovery_end(void)
{
    if(!ff_armed) return;
    REQUIRE(ff_open && !ff_forwarding);
    ff_close_batch();
}
#endif

#define FF_CALL(slot,expr) do { \
    int reject=ff_reject(slot); HRESULT hr; \
    if(reject) hr=D3DERR_INVALIDCALL; \
    else { ff_forwarding++; hr=(expr); ff_forwarding--; } \
    ff_end_operation(); return hr; \
} while(0)

static HRESULT WINAPI ff_get_constants(void *self,unsigned first,void *data,unsigned count)
{
    if(ff_armed && !ff_open && !ff_forwarding && bench_fault_batch_get(first,count)) {
        ff_open=1; ff_batch++; ff_step=0; ff_saved=1;
        ff_snapshot(&ff_before);
    }
    int reject=ff_reject(PF_DEV_GET_VS_CONST_F);
    HRESULT hr=D3DERR_INVALIDCALL;
    if(!reject) { ff_forwarding++; hr=((pf_const_f_fn)ff_original[PF_DEV_GET_VS_CONST_F])(ff_real(self),first,data,count); ff_forwarding--; }
    if(ff_open && ff_step==1) ff_saved=SUCCEEDED(hr);
    return hr;
}
static HRESULT WINAPI ff_set_vs(void *self,void *value)
{ FF_CALL(PF_DEV_SET_VS,((pf_ptr_fn)ff_original[PF_DEV_SET_VS])(ff_real(self),value)); }
static HRESULT WINAPI ff_set_decl(void *self,void *value)
{ FF_CALL(PF_DEV_SET_DECL,((pf_ptr_fn)ff_original[PF_DEV_SET_DECL])(ff_real(self),value)); }
static HRESULT WINAPI ff_set_stream(void *self,unsigned stream,void *value,unsigned offset,unsigned stride)
{
#ifdef PF_RECOVERY_CANDIDATE
    /* Deliberate oracle corruption belongs to this proxy, never to the
     * production transaction. Pretend the null-offset seed was applied. */
    if(ff_corrupt_mode==4 && bench_recovery_operation()==PF_SUBMIT_SEED_NULL_STREAM) return S_OK;
#endif
    FF_CALL(PF_DEV_SET_STREAM,((pf_set_stream_fn)ff_original[PF_DEV_SET_STREAM])(ff_real(self),stream,value,offset,stride));
}
static HRESULT WINAPI ff_set_frequency(void *self,unsigned stream,unsigned value)
{ FF_CALL(PF_DEV_SET_STREAM_FREQ,((pf_set_freq_fn)ff_original[PF_DEV_SET_STREAM_FREQ])(ff_real(self),stream,value)); }
static HRESULT WINAPI ff_set_constants(void *self,unsigned first,void *data,unsigned count)
{
#ifdef PF_RECOVERY_CANDIDATE
    if(ff_corrupt_mode==7 && bench_recovery_operation()==PF_SUBMIT_PENDING_RESTORE) return S_OK;
#endif
    FF_CALL(PF_DEV_SET_VS_CONST_F,((pf_const_f_fn)ff_original[PF_DEV_SET_VS_CONST_F])(ff_real(self),first,data,count));
}
static HRESULT WINAPI ff_draw(void *self,unsigned type,int base,unsigned first,unsigned vertices,
                               unsigned start,unsigned primitives)
{
#ifdef PF_RECOVERY_CANDIDATE
    unsigned operation=bench_recovery_operation();
    if((ff_corrupt_mode==2 && operation==PF_SUBMIT_REPLAY_DRAW) ||
       (ff_corrupt_mode==5 && operation==PF_SUBMIT_PENDING_DRAW)) return S_OK;
    unsigned copies=1+((ff_corrupt_mode==6 && operation==PF_SUBMIT_PENDING_DRAW) ||
        (ff_corrupt_mode==3 && operation==PF_SUBMIT_DRAW && ff_target_step>=9 &&
         ff_target_step<=14 && ff_batch==ff_target_batch));
#else
    unsigned copies=1;
#endif
    unsigned frequency=1;
    HR(((pf_get_freq_fn)ff_original[PF_DEV_GET_STREAM_FREQ])(ff_real(self),0,&frequency));
    int instanced=(frequency&PF_STREAM_INDEXED)!=0;
    int reject=ff_reject(PF_DEV_DRAW_INDEXED);
    HRESULT hr=D3DERR_INVALIDCALL;
    if(!reject) {
        for(unsigned i=0;i<copies;i++) {
            ff_forwarding++;
            hr=((pf_dip_fn)ff_original[PF_DEV_DRAW_INDEXED])(ff_real(self),type,base,first,vertices,start,primitives);
            ff_forwarding--;
            if(SUCCEEDED(hr)) {
                if(instanced) ff_instances+=frequency&0x3fffffffu;
                else ff_native_dips++;
            }
        }
    }
    if(ff_negative && instanced) ff_instances++; /* Broken logical-count oracle control. */
    return hr;
}
#undef FF_CALL

typedef struct {unsigned pixels,state,active,instances,native,attempts,suppressed,batches;} FaultResult;
static FaultResult ff_case(unsigned extra,unsigned optimized,unsigned target_batch,unsigned target_step,
                            const unsigned *reference,unsigned *out)
{
    FaultResult result={0};
    bench_fault_reset(); bench_extra_mode(extra); ng_frame_mode(optimized);
#ifdef PF_FAULT_STOCK_REFERENCE
    if(!optimized) bench_fault_stock(1);
#endif
    pose(extra);
    /* Give every member its own screen region. Repeated source poses cannot
     * hide a lost follower in these fault controls. */
    for(unsigned i=0;i<GROUP_SIZE;i++) for(unsigned p=0;p<PIVOTS;p++) {
        float *dst=(float *)(many_pivots[i]+p*0x58+0x30);
        dst[4]+=10.f*(i%5); dst[5]+=9.f*(i/5);
    }
    ff_batch=ff_step=ff_injected=ff_open=ff_instances=ff_native_dips=ff_batches=0;
    ff_state_bad=ff_bad_at_target=0;
    ff_target_batch=target_batch; ff_target_step=target_step;
    g_stock_draws=0; ng_current_group=0;
    HR(IDirect3DStateBlock9_Apply(outer));
    HR(IDirect3DDevice9_SetVertexShader(g_dev,g_ea));
    HR(IDirect3DDevice9_SetVertexDeclaration(g_dev,g_decl));
    HR(IDirect3DDevice9_SetStreamSource(g_dev,0,g_vb,0,40));
    HR(IDirect3DDevice9_SetStreamSource(g_dev,1,ff_seed_vb,0,4));
    HR(IDirect3DDevice9_SetIndices(g_dev,g_ib));
    HR(IDirect3DDevice9_SetStreamSourceFreq(g_dev,0,1));
    HR(IDirect3DDevice9_SetStreamSourceFreq(g_dev,1,1));
    HR(IDirect3DDevice9_BeginScene(g_dev));
    HR(IDirect3DDevice9_Clear(g_dev,0,NULL,D3DCLEAR_TARGET |
        (shadow_mode?D3DCLEAR_ZBUFFER:0),shadow_mode?0xffffffffu:0,1,0));
    unsigned refs=g_method.refs;
    pf_timing_stats before,after; pf_timing_get_stats(&before);
    ff_armed=1;
    for(unsigned group=0;group<ff_groups;group++) {
        ng_current_group=group;
        ng_group_call(ng_target(optimized),ng_renderer,records[group],records[group]+sizeof records[group]);
    }
    ff_armed=0;
    pf_timing_get_stats(&after);
    REQUIRE(!ff_open && !ff_forwarding && !ng_group_abi_errors && g_method.refs==refs);
    REQUIRE(after.stale_scopes==before.stale_scopes && after.scopes-before.scopes==(extra?0:26*ff_groups));
    REQUIRE(ff_injected==(target_step!=0));
    HR(IDirect3DDevice9_EndScene(g_dev)); drain(); read_pixels(out);
    result.state=ff_bad_at_target; result.active=pf_inst_active();
    result.instances=ff_instances; result.native=ff_native_dips;
    result.attempts=g_stock_draws; result.suppressed=bench_leader_suppressed(); result.batches=ff_batches;
    if(reference) for(unsigned i=0;i<RT*RT;i++) {
        unsigned delta=shadow_mode?pf_depth_diff(reference[i],out[i]):reference[i]!=out[i];
        result.pixels+=delta>(shadow_mode?PF_DEPTH_TOLERANCE:0);
    }
    unsigned expected_active=1,expected_ordinary=optimized?0:1;
#ifdef PF_FAULT_STOCK_REFERENCE
    if(!optimized) { expected_active=0;expected_ordinary=25; }
#endif
    if(!target_step && (ff_state_bad || result.active!=expected_active || result.instances+result.native!=25*ff_groups ||
                       (!ff_allow_fallback && result.native!=expected_ordinary) || result.pixels))
        fprintf(stderr,"clean fault control: optimized=%u state=%u active=%u instances=%u ordinary=%u pixels=%u\n",
                optimized,ff_state_bad,result.active,result.instances,result.native,result.pixels);
    if(!target_step) REQUIRE(!ff_state_bad && result.active==expected_active && result.instances+result.native==25*ff_groups &&
                            (ff_allow_fallback || result.native==expected_ordinary) && !result.pixels);
    bench_extra_mode(0); bench_fault_reset();
    return result;
}

static void ff_validate(void)
{
    ng_validate();
    ff_original=*(void ***)g_dev;
    /* Do not replace a driver object's vptr: implementations can have
     * private virtual slots beyond the public COM interface. This separate
     * proxy forwards the public ABI after replacing only its this argument. */
    ff_forwarders=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    REQUIRE(ff_forwarders && sizeof ff_vtable/sizeof(void *)*32<=4096);
    const unsigned char forward[]={0x8B,0x44,0x24,0x04,0x8B,0x40,0x04,
        0x89,0x44,0x24,0x04,0x8B,0x00,0xFF,0xA0,0,0,0,0};
    for(unsigned i=0;i<sizeof ff_vtable/sizeof(void *);i++) {
        unsigned char *p=ff_forwarders+32*i; memcpy(p,forward,sizeof forward);
        *(unsigned *)(p+15)=i*4; ff_vtable[i]=p;
    }
    DWORD old;
    REQUIRE(VirtualProtect(ff_forwarders,4096,PAGE_EXECUTE_READ,&old));
    REQUIRE(FlushInstructionCache(GetCurrentProcess(),ff_forwarders,4096));
    ff_vtable[PF_DEV_GET_VS_CONST_F]=ff_get_constants;
    ff_vtable[PF_DEV_SET_VS]=ff_set_vs; ff_vtable[PF_DEV_SET_DECL]=ff_set_decl;
    ff_vtable[PF_DEV_SET_STREAM]=ff_set_stream; ff_vtable[PF_DEV_SET_STREAM_FREQ]=ff_set_frequency;
    ff_vtable[PF_DEV_SET_VS_CONST_F]=ff_set_constants; ff_vtable[PF_DEV_DRAW_INDEXED]=ff_draw;
    ff_proxy.vt=ff_vtable; ff_proxy.real=g_dev; bench_fault_device(&ff_proxy);
#ifdef PF_RECOVERY_CANDIDATE
    bench_recovery_watch(ff_recovery_begin,ff_recovery_end);
#endif
    /* Native DIP hook continues to this proxy, just like the batch's VT call. */
    pf_timing_set_next(PF_INST_DIP,ff_draw);
    /* Make stream binding failure observable rather than accidentally
     * retaining the correct instance-offset buffer from an earlier case. */
    HR(IDirect3DDevice9_CreateVertexBuffer(g_dev,4096,D3DUSAGE_WRITEONLY,0,
                                          D3DPOOL_MANAGED,&ff_seed_vb,NULL));
    void *seed=NULL; HR(IDirect3DVertexBuffer9_Lock(ff_seed_vb,0,0,&seed,0));
    memset(seed,0,4096); HR(IDirect3DVertexBuffer9_Unlock(ff_seed_vb));
    static unsigned reference[RT*RT],out[RT*RT];
    unsigned loss=0,leaks=0,unsafe=0;
    puts("fault_extra,fault_batch,fault_step,bad_pixels,immediate_state_words,instancing_active,submitted_instances,native_dips,native_attempts,suppressed_leaders,batches");
    for(unsigned extra=0;extra<2;extra++) {
        ff_case(extra,0,0,0,NULL,reference);
        ff_case(extra,1,0,0,reference,out);
        for(unsigned batch=1;batch<=2;batch++) for(unsigned step=1;step<=14;step++) {
            FaultResult r=ff_case(extra,1,batch,step,reference,out);
            printf("FAULT,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",extra,batch,step,
                    r.pixels,r.state,r.active,r.instances,r.native,r.attempts,r.suppressed,r.batches);
            loss+=r.pixels!=0; leaks+=r.state!=0; unsafe+=r.active && (r.pixels || r.state);
#ifdef PF_RECOVERY_CANDIDATE
            REQUIRE(!r.pixels && !r.state && !r.active && r.instances+r.native==25);
#else
            /* The direct rejected-DIP counterexamples must be detected. */
            if(step==8) REQUIRE(r.pixels && !r.active && r.instances+r.native<25);
            if(step==4) REQUIRE(r.pixels);
            if(step==14) REQUIRE(r.state && r.active);
#endif
            ff_fault_cases++;
        }
#ifdef PF_RECOVERY_CANDIDATE
        FaultResult r=ff_case(extra,1,1,15,reference,out);
        printf("RECOVERY_CAPTURE,%u,%u,%u,%u,%u,%u\n",extra,r.pixels,r.state,r.active,r.instances,r.native);
        REQUIRE(!r.pixels && !r.state && !r.active && !r.instances && r.native==25 && !r.suppressed);
#ifdef PF_FAULT_STOCK_REFERENCE
        for(unsigned batch=1;batch<=2;batch++) {
            r=ff_case(extra,1,batch,19,reference,out);
            printf("RECOVERY_NULL,%u,%u,%u,%u,%u,%u,%u\n",extra,batch,r.pixels,r.state,r.active,r.instances,r.native);
            REQUIRE(!r.pixels && !r.state && !r.active && r.instances+r.native==25);
        }
#endif
#endif
    }
#ifdef PF_RECOVERY_CANDIDATE
    REQUIRE(ff_fault_cases==56 && !loss && !leaks && !unsafe);
#ifdef PF_FAULT_ADDITIONAL_CONTROLS
    PF_FAULT_ADDITIONAL_CONTROLS();
#endif
    bench_recovery_watch(NULL,NULL);
#else
    REQUIRE(ff_fault_cases==56 && loss && leaks && unsafe);
#endif
    bench_fault_device(NULL);
    pf_timing_set_next(PF_INST_DIP,ff_original[PF_DEV_DRAW_INDEXED]);
    HR(IDirect3DStateBlock9_Apply(outer));
    IDirect3DVertexBuffer9_Release(ff_seed_vb); ff_seed_vb=NULL;
    bench_fault_reset();
#ifdef PF_RECOVERY_CANDIDATE
    printf("RECOVERY: %u batch faults plus two pre-deferral capture failures; %u changed outputs, %u state leaks, %u enabled with error\n",
#else
    printf("FAULT AUDIT: %u injected cases; %u changed-output cases, %u immediate-state leaks, %u remained enabled with a detected error; current code intentionally not qualified\n",
#endif
            ff_fault_cases,loss,leaks,unsafe);
}
static void ff_setup(void)
{
    validate_retained=ff_validate; benchmark_controls_only=1;
    comparison_name="native-gpu-fault-audit";
}
#ifndef PF_NATIVE_FAULT_MAIN
#define PF_NATIVE_FAULT_MAIN main
#endif
int PF_NATIVE_FAULT_MAIN(int argc,char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
#ifndef PF_FAULT_ARGUMENTS
#define PF_FAULT_ARGUMENTS 10
#endif
    if(argc==PF_FAULT_ARGUMENTS+1) {
        unsigned mode=(unsigned)atoi(argv[PF_FAULT_ARGUMENTS]);
        ff_negative=mode==1;
#ifdef PF_RECOVERY_CANDIDATE
        ff_corrupt_mode=mode;
#endif
        argc--;
    }
    ng_after_setup=ff_setup;
    return group_gpu_main(argc,argv);
}
