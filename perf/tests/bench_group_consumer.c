/* Native group control flow on the producer, real effect results on the
 * GPU owner. Fixed fixture resources only; not an installed render bridge. */
#define PF_FIXTURE_DEVICE_FLAGS (D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED)
static void *gc_render_entry(void);
#define PF_NATIVE_GROUP_RENDER gc_render_entry()
#define PF_NATIVE_GROUP_MAIN unused_native_group_main
#include "bench_native_group_gpu.c"
#include "material_packet.h"
#define PF_PACKET_CONSUMER_GROUP_ONLY
#include "packet_consumer.h"
extern void material_fixture_identity(unsigned);
extern HRESULT material_fixture_try_open(unsigned *);
extern void material_fixture_native(void **,unsigned *,unsigned *);
extern int bench_scope_owner_idle(DWORD);

enum { GC_FRAME, GC_BEGIN, GC_DRAW_END, GC_END_ONLY, GC_FINISH, GC_CLAIM, GC_STOP };
typedef struct {
    unsigned kind,extra,group,count,identity,fail,check;
    fixture_material_values material;
    float palette[GROUP_SIZE][BONES][8];
    HRESULT result;
    unsigned passes;
} GcRequest;
typedef struct { HANDLE done; GcRequest request; } GcSlot;
static GcSlot gc_slots[2];
static HANDLE gc_available,gc_worker;
static DWORD gc_producer,gc_gpu;
unsigned gc_mode; /* Read by the tail bridge; changed only with an idle queue. */
static unsigned gc_sent,gc_received,gc_extra,gc_epoch,gc_fail,gc_fault;
static unsigned gc_native_optimized=1;
static unsigned gc_captured,gc_successes,gc_failures,gc_identity_seen[2];
static unsigned gc_output[RT*RT],gc_state[1600],gc_state_words;
static GcRequest gc_group;
static void *gc_effects[2];
static unsigned gc_depth_max;

static int gc_pixels_equal(const unsigned *expected,const unsigned *actual)
{
    for(unsigned i=0;i<RT*RT;i++) {
        unsigned difference=shadow_mode?pf_depth_diff(expected[i],actual[i]):expected[i]!=actual[i];
        if(shadow_mode && difference>gc_depth_max)gc_depth_max=difference;
        if(difference>(shadow_mode?PF_DEPTH_TOLERANCE:0))return 0;
    }
    return 1;
}

static void gc_wait(HANDLE event)
{
    REQUIRE(GetCurrentThreadId()==gc_producer);
    for(;;) {
        DWORD result=MsgWaitForMultipleObjectsEx(1,&event,10000,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        if(result==WAIT_OBJECT_0)return;
        REQUIRE(result==WAIT_OBJECT_0+1);
        MSG m;while(PeekMessageW(&m,NULL,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}
    }
}

static unsigned gc_snapshot(unsigned *out)
{
    HR(IDirect3DDevice9_GetVertexShaderConstantF(g_dev,0,(float *)out,256));
    HR(IDirect3DDevice9_GetPixelShaderConstantF(g_dev,0,(float *)(out+1024),32));
    unsigned words=1152+material_fixture_state(g_dev,out+1152,1600-1152);
    IDirect3DVertexShader9 *shader=NULL;unsigned char code[32768];UINT size=0;
    HR(IDirect3DDevice9_GetVertexShader(g_dev,&shader));REQUIRE(shader);
    HR(IDirect3DVertexShader9_GetFunction(shader,NULL,&size));REQUIRE(size && size<=sizeof code);
    HR(IDirect3DVertexShader9_GetFunction(shader,code,&size));
    out[words++]=fnv(code,size);check_state_shader(shader);IDirect3DVertexShader9_Release(shader);
    return words;
}

static void gc_execute(GcRequest *r)
{
    REQUIRE(GetCurrentThreadId()==(gc_mode==2?gc_gpu:gc_producer));
    switch(r->kind) {
    case GC_CLAIM: consumer.owner=GetCurrentThreadId();break;
    case GC_FRAME: {
        static const float zero[1024]={0};
        if(r->check) {
            HR(IDirect3DDevice9_SetVertexShaderConstantF(g_dev,0,zero,256));
            HR(IDirect3DDevice9_SetPixelShaderConstantF(g_dev,0,zero,32));
        }
        HR(IDirect3DDevice9_SetVertexShader(g_dev,g_ea));
        HR(IDirect3DDevice9_SetVertexDeclaration(g_dev,g_decl));
        HR(IDirect3DDevice9_SetStreamSource(g_dev,0,g_vb,0,40));
        HR(IDirect3DDevice9_SetIndices(g_dev,g_ib));
        HR(IDirect3DDevice9_BeginScene(g_dev));
        HR(IDirect3DDevice9_Clear(g_dev,0,NULL,D3DCLEAR_TARGET|(shadow_mode?D3DCLEAR_ZBUFFER:0),shadow_mode?0xffffffffu:0,1,0));
        /* The existing native-group fixture restores this caller state on
         * both regular and extra frames. Retain that boundary in all modes. */
        HR(IDirect3DStateBlock9_Capture(outer));
        break;
    }
    case GC_BEGIN:
        REQUIRE(r->identity<2);gc_identity_seen[r->identity]++;
        material_fixture_identity(gc_fault==4?(r->identity^1):r->identity);
        void *selected;unsigned technique,block;
        material_fixture_native(&selected,&technique,&block);
        REQUIRE(selected==gc_effects[r->identity] && technique && block);
        material_fixture_captured(&r->material,1);
        material_fixture_prepare(0);
        r->passes=0;
        /* Inject a failed API result before opening the real effect. The
         * actual successful Begin and its pass count are never predicted. */
        r->result=r->fail?E_FAIL:material_fixture_try_open(&r->passes);
        material_fixture_captured(NULL,0);
        break;
    case GC_DRAW_END: {
        REQUIRE(r->count && r->count<=GROUP_SIZE);
        material_fixture_pass_begin(0);
        IDirect3DVertexShader9 *shader=NULL;
        HR(IDirect3DDevice9_GetVertexShader(g_dev,&shader));REQUIRE(shader);
        packet_consumer_group((const float *)r->palette,r->count,r->extra,shader);
        IDirect3DVertexShader9_Release(shader);
        material_fixture_pass_end();material_fixture_close();
        break;
    }
    case GC_END_ONLY: material_fixture_close();break;
    case GC_FINISH:
        HR(IDirect3DStateBlock9_Apply(outer));
        HR(IDirect3DDevice9_EndScene(g_dev));drain();
        if(r->check){read_pixels(gc_output);gc_state_words=gc_snapshot(gc_state);}
        break;
    case GC_STOP: packet_consumer_close();break;
    default: REQUIRE(!"unknown group command");
    }
}

static DWORD WINAPI gc_run(void *unused)
{
    (void)unused;gc_gpu=GetCurrentThreadId();
    for(;;) {
        REQUIRE(WaitForSingleObject(gc_available,10000)==WAIT_OBJECT_0);
        GcSlot *slot=&gc_slots[gc_received++%2];
        unsigned stop=slot->request.kind==GC_STOP;
        gc_execute(&slot->request);REQUIRE(SetEvent(slot->done));
        if(stop)return 0;
    }
}

static void gc_send(GcRequest *r,unsigned wait)
{
    if(gc_mode!=2){gc_execute(r);return;}
    GcSlot *slot=&gc_slots[gc_sent++%2];gc_wait(slot->done);
    REQUIRE(ResetEvent(slot->done));slot->request=*r;
    REQUIRE(ReleaseSemaphore(gc_available,1,NULL));
    if(wait){gc_wait(slot->done);r->result=slot->request.result;r->passes=slot->request.passes;}
}

static int __attribute__((thiscall)) gc_begin(void *self,int *passes,int flags)
{
    if(!gc_mode) {
        material_fixture_identity(ng_current_group&1);
        return ng_begin(self,passes,flags);
    }
    ng_state();REQUIRE(self==&g_method && flags==0xFFFF && g_method.refs>=2);
    memset(&gc_group,0,sizeof gc_group);
    gc_group.kind=GC_BEGIN;gc_group.group=ng_current_group;
    gc_group.identity=ng_current_group&1;gc_group.extra=gc_extra;
    gc_group.fail=gc_fail && ng_current_group==7;
    material_fixture_capture(ng_current_group,gc_epoch,100,gc_epoch*33,&gc_group.material);
    gc_send(&gc_group,1);*passes=(int)gc_group.passes;
    int ok=SUCCEEDED(gc_group.result);
    if(ok){REQUIRE(gc_group.passes==1);gc_successes++;}
    else gc_failures++;
    if(gc_fault==1 && ok)*passes=0; /* Cached/incorrect reply must lose output. */
    if(gc_fault==2 && !ok){*passes=1;ok=1;}
    return ok;
}
static void __attribute__((thiscall)) gc_pass(void *self,unsigned pass)
{
    if(!gc_mode){ng_begin_pass(self,pass);return;}
    ng_state();REQUIRE(self==&g_method && pass==0 && gc_group.passes==1);
}
static void __attribute__((thiscall)) gc_end_pass(void *self)
{
    if(!gc_mode){ng_end_pass(self);return;}
    ng_state();REQUIRE(self==&g_method && gc_group.count);
    gc_group.kind=GC_DRAW_END;gc_send(&gc_group,0);
    memset(gc_group.palette,0xA5,sizeof gc_group.palette); /* Publication owns all values. */
}
static void __attribute__((thiscall)) gc_end(void *self)
{
    if(!gc_mode){ng_end(self);return;}
    ng_state();REQUIRE(self==&g_method);
    if(gc_group.kind!=GC_DRAW_END){gc_group.kind=GC_END_ONLY;gc_send(&gc_group,0);}
    /* End is ordered in the preceding owned DRAW_END command. */
}
void __attribute__((thiscall)) gc_capture_render(fake_mesh *mesh,fake_method *method,int count)
{
    REQUIRE(GetCurrentThreadId()==gc_producer && gc_mode && count==1 && method==&g_method);
    REQUIRE(mesh>=many_meshes && mesh<many_meshes+GROUPS*GROUP_SIZE && method->refs>=2);
    unsigned index=(unsigned)(mesh-many_meshes),at=gc_group.count++;
    REQUIRE(at<GROUP_SIZE);
    for(unsigned b=0;b<BONES;b++) {
        memcpy(gc_group.palette[at][b],many_pivots[index]+g_bonemap[b]*0x58+0x30,28);
        gc_group.palette[at][b][7]=0;
    }
    if(gc_fault==3 && at)memcpy(gc_group.palette[at],gc_group.palette[0],sizeof gc_group.palette[0]);
    gc_captured++;method->refs--; /* Consume the native owning temporary. */
}
void gc_render_bridge(void);
__asm__(".text\n.globl _gc_render_bridge\n_gc_render_bridge:\n"
        "cmpl $0,_gc_mode\njne _gc_capture_render\njmp *_pf_enter_stubs+64\n");
static void *gc_render_entry(void){return gc_render_bridge;}
static void gc_empty_frame(void) {}

static void gc_frame(unsigned mode,unsigned phase,unsigned check,unsigned fail)
{
    gc_mode=mode;gc_extra=phase!=0;gc_epoch=phase>=2;gc_fail=fail;
    GcRequest request={0};request.kind=GC_CLAIM;gc_send(&request,1);
    REQUIRE(bench_scope_owner_idle(gc_producer));
    pose(gc_extra);material_fixture_verify_end(0);
    /* Producer only mutates its own mesh/pivot storage while worker draws. */
    unsigned refs=g_method.refs,captured=gc_captured;
    if(!mode){material_fixture_epoch(gc_epoch);material_fixture_clock(gc_epoch*33);ng_frame_mode(gc_native_optimized);bench_extra_mode(gc_extra);}
    else bench_native_group_mode(0);
    request.kind=GC_FRAME;request.extra=gc_extra;request.check=check;gc_send(&request,1);
    for(unsigned group=0;group<GROUPS;group++) {
        ng_current_group=group;ng_begin_fail=fail && group==7;
        ng_group_call(ng_target(!mode && gc_native_optimized),ng_renderer,records[group],records[group]+sizeof records[0]);
    }
    ng_begin_fail=0;
    request.kind=GC_FINISH;gc_send(&request,1);
    if(!mode)bench_extra_mode(0);
    REQUIRE(!ng_group_abi_errors && g_method.refs==refs && bench_scope_owner_idle(gc_producer));
    if(mode)REQUIRE(gc_captured-captured==(GROUPS-(fail?1:0))*GROUP_SIZE);
}

int main(int argc,char **argv)
{
    REQUIRE(argc==11 && bench_no_game());gc_producer=GetCurrentThreadId();
    gc_fault=(unsigned)atoi(argv[10]);REQUIRE(gc_fault<=4);
    nm_image=argv[7];nm_manifest=argv[8];ng_manifest=argv[9];setup_complete=ng_setup;
    const char *slash=strrchr(argv[2],'/'),*back=strrchr(argv[2],'\\');
    if(back && (!slash || back>slash))slash=back;
    REQUIRE(slash && (unsigned)(slash-argv[2])+sizeof "d3dx9_27.dll"<sizeof material_runtime);
    memcpy(material_runtime,argv[2],slash-argv[2]+1);strcpy(material_runtime+(slash-argv[2])+1,"d3dx9_27.dll");
    material_setup=effect_setup;material_begin=material_fixture_begin;material_end=material_fixture_end;
    pose_adjust=spread;dispatch_scope_begin=bench_scope_bulk_begin;dispatch_scope_end=bench_scope_bulk_end;
    setup(7,argv);pf_timing_set_detail(1);packet_consumer_init();
    pf_timing_set_next(PF_FRAME,gc_empty_frame);
    ((void (*)(void))pf_enter_stubs[PF_FRAME])();
    for(unsigned i=0;i<2;i++) {
        unsigned technique,block;material_fixture_identity(i);
        material_fixture_native(&gc_effects[i],&technique,&block);
        REQUIRE(gc_effects[i] && technique && block);
    }
    REQUIRE(gc_effects[0]!=gc_effects[1]);material_fixture_identity(0);
    D3DDEVICE_CREATION_PARAMETERS creation;HR(IDirect3DDevice9_GetCreationParameters(g_dev,&creation));
    REQUIRE((creation.BehaviorFlags&D3DCREATE_MULTITHREADED) && GetWindowThreadProcessId(creation.hFocusWindow,NULL)==gc_producer);
    nm_method_vt[2]=gc_begin;nm_method_vt[3]=gc_pass;nm_method_vt[5]=gc_end_pass;nm_method_vt[6]=gc_end;
    gc_available=CreateSemaphoreW(NULL,0,2,NULL);REQUIRE(gc_available);
    for(unsigned i=0;i<2;i++){gc_slots[i].done=CreateEventW(NULL,TRUE,TRUE,NULL);REQUIRE(gc_slots[i].done);}
    gc_worker=CreateThread(NULL,0,gc_run,NULL,0,NULL);REQUIRE(gc_worker);
    static unsigned expected[RT*RT],state[1600],first[RT*RT],second[RT*RT];unsigned cases=0;
    for(unsigned fail=0;fail<2;fail++)for(unsigned phase=0;phase<3;phase++) {
        gc_native_optimized=0;gc_frame(0,phase,1,fail);memcpy(expected,gc_output,sizeof expected);
        gc_native_optimized=1;gc_frame(0,phase,1,fail);
        REQUIRE(gc_pixels_equal(expected,gc_output));
        gc_frame(0,phase,1,fail);memcpy(expected,gc_output,sizeof expected);
        if(!fail) {
            unsigned covered=0,changed=0;
            for(unsigned i=0;i<RT*RT;i++) {
                covered+=expected[i]!=(shadow_mode?PF_DEPTH_CLEAR:0);
                if(phase)changed+=expected[i]!=(phase==1?first[i]:second[i]);
            }
            REQUIRE(covered>1000 && (!phase || changed>100));
            if(!phase)memcpy(first,expected,sizeof first);
            if(phase==1)memcpy(second,expected,sizeof second);
            printf("GROUP ORACLE: phase=%u covered=%u changed=%u hash=%08X\n",phase,covered,changed,fnv((const unsigned char *)expected,sizeof expected));
        }
        unsigned words=gc_state_words;memcpy(state,gc_state,words*4);
        for(unsigned mode=1;mode<=2;mode++) {
            gc_frame(mode,phase,1,fail);
            for(unsigned i=0;i<RT*RT;i++)if(shadow_mode?pf_depth_diff(expected[i],gc_output[i])>PF_DEPTH_TOLERANCE:expected[i]!=gc_output[i]) {
                fprintf(stderr,"group consumer image: mode=%u phase=%u fail=%u pixel=%u expected=%08X actual=%08X captured=%u\n",mode,phase,fail,i,expected[i],gc_output[i],gc_captured);
                break;
            }
            for(unsigned i=0;i<words;i++)if(state[i]!=gc_state[i]) {
                fprintf(stderr,"group consumer state: mode=%u phase=%u fail=%u word=%u expected=%08X actual=%08X\n",mode,phase,fail,i,state[i],gc_state[i]);
                break;
            }
            REQUIRE(gc_pixels_equal(expected,gc_output));
            REQUIRE(words==gc_state_words && !memcmp(state,gc_state,words*4));cases++;
        }
    }
    printf("NATIVE GROUP CONSUMER: %u image/state cases; lit pixels and state exact, shadow max difference=%u/2^24 (existing tolerance=%u); actual Begin reply, alternating effect identity, copied palette poisoning and native method ownership pass\n",cases,gc_depth_max,PF_DEPTH_TOLERANCE);
    for(unsigned round=0;round<12;round++) {
        REQUIRE(bench_no_game());double elapsed[3];
        for(unsigned offset=0;offset<3;offset++) {
            unsigned mode=(round+offset)%3;
            /* Warm the selected ownership mode before its measured pair. */
            gc_frame(mode,0,0,0);
            LONGLONG start=ticks();gc_frame(mode,0,0,0);gc_frame(mode,1,0,0);
            elapsed[mode]=(ticks()-start)*1000.0/qpf;
        }
        printf("sample,%u,%.4f,%.4f,%.4f,%.4f\n",round,elapsed[0],elapsed[1],elapsed[2],elapsed[0]-elapsed[2]);
    }
    gc_mode=2;GcRequest end={0};end.kind=GC_CLAIM;gc_send(&end,1);end.kind=GC_STOP;gc_send(&end,1);
    gc_wait(gc_worker);DWORD exit_code;REQUIRE(GetExitCodeThread(gc_worker,&exit_code) && !exit_code && gc_sent==gc_received);
    CloseHandle(gc_worker);CloseHandle(gc_available);for(unsigned i=0;i<2;i++)CloseHandle(gc_slots[i].done);
    REQUIRE(gc_failures==6 && gc_identity_seen[0] && gc_identity_seen[1]);
    printf("RESULTS: successful_begins=%u failed_begins=%u captured_meshes=%u worker_commands=%u; no game or runtime hook\n",gc_successes,gc_failures,gc_captured,gc_sent);
    REQUIRE(bench_no_game());return 0;
}
