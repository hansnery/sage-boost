/* Native group control flow on the producer, real effect results on the
 * GPU owner. Fixed fixture resources only; not an installed render bridge. */
#define PF_FIXTURE_DEVICE_FLAGS (D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED)
static void *gc_render_entry(void);
#define PF_NATIVE_GROUP_RENDER gc_render_entry()
#define PF_NATIVE_GROUP_MAIN unused_native_group_main
#ifdef PF_GROUP_CAPTURE
#define ng_group_bridge gc_bulk_bridge
#endif
#include "bench_native_group_gpu.c"
#ifdef PF_GROUP_CAPTURE
#undef ng_group_bridge
#endif
#include "material_packet.h"
#define PF_PACKET_CONSUMER_GROUP_ONLY
#include "packet_consumer.h"
extern void material_fixture_identity(unsigned);
extern HRESULT material_fixture_try_open(unsigned *);
extern void material_fixture_native(void **,unsigned *,unsigned *);
extern int bench_scope_owner_idle(DWORD);

enum { GC_FRAME, GC_BEGIN, GC_DRAW_END, GC_END_ONLY, GC_FINISH, GC_CLAIM, GC_STOP, GC_COMMANDS };
typedef struct {
    unsigned kind,extra,group,count,identity,fail,check;
#ifdef PF_GROUP_BATCH_FIRST
    unsigned batch_first,recovery;
#endif
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
#ifdef PF_GROUP_BATCH_FIRST
static unsigned gc_batch_first=1,gc_group_size=GROUP_SIZE,gc_failure_probe;
#else
#define gc_group_size GROUP_SIZE
#endif
#ifdef PF_GROUP_COMMANDS
#include "group_commands.h"
#ifdef PF_GROUP_CAPTURE
#ifdef PF_GROUP_BATCH_FIRST
#define GC_MAX_FAULT 13
#else
#define GC_MAX_FAULT 12
#endif
static unsigned gc_fast_capture=1,gc_bulk_captured;
#else
#define GC_MAX_FAULT 8
#endif
#define GC_EFFECTS 2
#define GC_CASE_NAME "NATIVE GROUP COMMANDS:"
#else
#define GC_MAX_FAULT 4
#define GC_EFFECTS 2
#define GC_CASE_NAME "NATIVE GROUP CONSUMER:"
#endif
static unsigned gc_worker_mode(void){return gc_mode==2 || gc_mode==4;}

#ifdef PF_GROUP_PROFILE
enum { GCP_BEGIN,GCP_PASS,GCP_TRAVERSE,GCP_END_PASS,GCP_END,GCP_PUBLISH,GCP_GROUP,GCP_FRAME,GCP_COUNT };
static unsigned gc_profiling;
static LONGLONG gc_profile_ticks[GCP_COUNT],gc_traversal_start;
#define GC_PROFILE_START(name) LONGLONG name=gc_profiling?ticks():0
#define GC_PROFILE_STOP(name,kind) do { if(gc_profiling)gc_profile_ticks[kind]+=ticks()-(name); } while(0)
#else
#define GC_PROFILE_START(name) ((void)0)
#define GC_PROFILE_STOP(name,kind) ((void)0)
#endif

#ifdef PF_GROUP_BATCH_FIRST
#include "group_first_batch.h"
#endif

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
    REQUIRE(GetCurrentThreadId()==(gc_worker_mode()?gc_gpu:gc_producer));
    switch(r->kind) {
    case GC_CLAIM: consumer.owner=GetCurrentThreadId();break;
    case GC_FRAME: {
#ifdef PF_GROUP_BATCH_FIRST
        gc_first_draws=gc_first_failures=0;gc_first_fail_armed=r->recovery;
        consumer_expect_recovery=r->recovery;consumer_recoveries=0;
        consumer_skip_first=gc_fault==13 && r->batch_first;
#endif
#ifdef PF_GROUP_COMMANDS
        gc_command_verify=r->check;
        if(r->check)HR(IDirect3DDevice9_SetRenderState(g_dev,D3DRS_CULLMODE,D3DCULL_CW));
#endif
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
#ifdef PF_GROUP_COMMANDS
    case GC_COMMANDS: gc_command_replay(gc_mode==4?&gc_command_slots[r->group]:&gc_commands);break;
#endif
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
    GC_PROFILE_START(publish_start);
    if(!gc_worker_mode()){
        gc_execute(r);
        if(r->kind==GC_COMMANDS)GC_PROFILE_STOP(publish_start,GCP_PUBLISH);
        return;
    }
    unsigned index=gc_sent++%2;
    GcSlot *slot=&gc_slots[index];gc_wait(slot->done);
    REQUIRE(ResetEvent(slot->done));slot->request=*r;
#ifdef PF_GROUP_COMMANDS
    if(r->kind==GC_COMMANDS) {
        GcCommandBatch *to=&gc_command_slots[index];
        to->count=gc_commands.count;to->bytes=gc_commands.bytes;to->groups=gc_commands.groups;
        memcpy(to->command,gc_commands.command,to->count*sizeof(GcCommand));
        memcpy(to->data,gc_commands.data,to->bytes);slot->request.group=index;
    }
#endif
    REQUIRE(ReleaseSemaphore(gc_available,1,NULL));
    if(wait){gc_wait(slot->done);r->result=slot->request.result;r->passes=slot->request.passes;}
    if(r->kind==GC_COMMANDS)GC_PROFILE_STOP(publish_start,GCP_PUBLISH);
}

static int __attribute__((thiscall)) gc_begin(void *self,int *passes,int flags)
{
    if(!gc_mode) {
        material_fixture_identity(ng_current_group&1);
        return ng_begin(self,passes,flags);
    }
    GC_PROFILE_START(begin_start);
    ng_state();REQUIRE(self==&g_method && flags==0xFFFF && g_method.refs>=2);
    memset(&gc_group,0,sizeof gc_group);
    gc_group.kind=GC_BEGIN;gc_group.group=ng_current_group;
    gc_group.identity=ng_current_group&1;gc_group.extra=gc_extra;
    gc_group.fail=gc_fail && ng_current_group==7;
    material_fixture_capture(ng_current_group,gc_epoch,100,gc_epoch*33,&gc_group.material);
#ifdef PF_GROUP_COMMANDS
    if(gc_mode>=3) {
        unsigned commands=gc_commands.count;gc_command_capture=1;gc_command_audited++;
        gc_identity_seen[gc_group.identity]++;
        if(gc_fault==5){DWORD value;IDirect3DDevice9_GetRenderState(g_dev,D3DRS_CULLMODE,&value);}
        if(gc_fault==12){IDirect3DDevice9_AddRef(g_dev);IDirect3DDevice9_Release(g_dev);}
        material_fixture_identity(gc_fault==4?(gc_group.identity^1):gc_group.identity);
        void *selected;unsigned technique,block;material_fixture_native(&selected,&technique,&block);
        REQUIRE(selected==gc_effects[gc_group.identity]);
        material_fixture_command_manager(&gc_command_manager);
        material_fixture_captured(&gc_group.material,1);
        gc_command_lifetime++;material_fixture_prepare(0);gc_command_lifetime--;
        gc_group.passes=0;
        /* Explicit queued flags=6 restoration replaces automatic state
         * saving here. Default saving must trip the state-block audit. */
        gc_group.result=gc_group.fail?E_FAIL:material_fixture_try_open_flags(&gc_group.passes,gc_fault==8?6:7);
        material_fixture_captured(NULL,0);gc_command_capture=0;
        REQUIRE(gc_commands.count==commands); /* Preparation must not emit device state. */
        if(SUCCEEDED(gc_group.result))HR(gc_command_emit(CMD_SAVE,0,0,NULL,0,NULL));
    } else
#endif
    gc_send(&gc_group,1);
    *passes=(int)gc_group.passes;
    int ok=SUCCEEDED(gc_group.result);
    if(ok){REQUIRE(gc_group.passes==1);gc_successes++;}
    else gc_failures++;
    if(gc_fault==1 && ok)*passes=0; /* Cached/incorrect reply must lose output. */
    if(gc_fault==2 && !ok){*passes=1;ok=1;}
    GC_PROFILE_STOP(begin_start,GCP_BEGIN);
    return ok;
}
static void __attribute__((thiscall)) gc_pass(void *self,unsigned pass)
{
    if(!gc_mode){ng_begin_pass(self,pass);return;}
    GC_PROFILE_START(pass_start);
    ng_state();REQUIRE(self==&g_method && pass==0 && gc_group.passes==1);
#ifdef PF_GROUP_COMMANDS
    if(gc_mode>=3){gc_command_capture=1;material_fixture_pass_begin(pass);gc_command_capture=0;}
#endif
    GC_PROFILE_STOP(pass_start,GCP_PASS);
#ifdef PF_GROUP_PROFILE
    if(gc_profiling)gc_traversal_start=ticks();
#endif
}
static void __attribute__((thiscall)) gc_end_pass(void *self)
{
    if(!gc_mode){ng_end_pass(self);return;}
#ifdef PF_GROUP_PROFILE
    GC_PROFILE_STOP(gc_traversal_start,GCP_TRAVERSE);
#endif
    GC_PROFILE_START(end_pass_start);
    ng_state();REQUIRE(self==&g_method && gc_group.count);
#ifdef PF_GROUP_COMMANDS
    if(gc_mode>=3) {
        unsigned flags=gc_group.extra;
#ifdef PF_GROUP_BATCH_FIRST
        flags|=gc_batch_first<<1;
#endif
        HR(gc_command_emit(CMD_DRAW,gc_group.count,flags,gc_group.palette,
                           gc_group.count*sizeof(gc_group.palette[0]),NULL));
        gc_command_capture=1;material_fixture_pass_end();gc_command_capture=0;
        memset(gc_group.palette,0xA5,sizeof gc_group.palette);
        GC_PROFILE_STOP(end_pass_start,GCP_END_PASS);return;
    }
#endif
    gc_group.kind=GC_DRAW_END;gc_send(&gc_group,0);
    memset(gc_group.palette,0xA5,sizeof gc_group.palette); /* Publication owns all values. */
    GC_PROFILE_STOP(end_pass_start,GCP_END_PASS);
}
static void __attribute__((thiscall)) gc_end(void *self)
{
    if(!gc_mode){ng_end(self);return;}
    GC_PROFILE_START(end_start);
    ng_state();REQUIRE(self==&g_method);
#ifdef PF_GROUP_COMMANDS
    if(gc_mode>=3) {
        gc_command_capture=1;material_fixture_close();gc_command_capture=0;
        HR(gc_command_emit(CMD_RESTORE,0,0,NULL,0,NULL));
        if(++gc_commands.groups==CMD_GROUPS)gc_command_flush();
        GC_PROFILE_STOP(end_start,GCP_END);
        return;
    }
#endif
    if(gc_group.kind!=GC_DRAW_END){gc_group.kind=GC_END_ONLY;gc_send(&gc_group,0);}
    /* End is ordered in the preceding owned DRAW_END command. */
    GC_PROFILE_STOP(end_start,GCP_END);
}
static void gc_capture_palette(fake_mesh *mesh)
{
    REQUIRE(GetCurrentThreadId()==gc_producer && gc_mode);
    REQUIRE(mesh>=many_meshes && mesh<many_meshes+GROUPS*GROUP_SIZE);
    unsigned index=(unsigned)(mesh-many_meshes),at=gc_group.count++;
    REQUIRE(at<GROUP_SIZE);
    for(unsigned b=0;b<BONES;b++) {
        memcpy(gc_group.palette[at][b],many_pivots[index]+g_bonemap[b]*0x58+0x30,28);
        gc_group.palette[at][b][7]=0;
    }
    if(gc_fault==3 && at)memcpy(gc_group.palette[at],gc_group.palette[0],sizeof gc_group.palette[0]);
#ifdef PF_GROUP_COMMANDS
    if(gc_mode>=3 && !at) {
        gc_command_capture=1;material_fixture_bones(gc_group.palette[0],BONES*32);gc_command_capture=0;
    }
#endif
    gc_captured++;
}
void __attribute__((thiscall)) gc_capture_render(fake_mesh *mesh,fake_method *method,int count)
{
    REQUIRE(count==1 && method==&g_method && method->refs>=2);
    gc_capture_palette(mesh);
    method->refs--; /* Consume the native owning temporary. */
}
void gc_render_bridge(void);
__asm__(".text\n.globl _gc_render_bridge\n_gc_render_bridge:\n"
        "cmpl $0,_gc_mode\njne _gc_capture_render\njmp *_pf_enter_stubs+64\n");
static void *gc_render_entry(void){return gc_render_bridge;}
static void gc_empty_frame(void) {}

#ifdef PF_GROUP_CAPTURE
#include "group_capture.h"
#endif

static void gc_frame(unsigned mode,unsigned phase,unsigned check,unsigned fail)
{
    GC_PROFILE_START(frame_start);
    REQUIRE(gc_group_size && gc_group_size<=GROUP_SIZE);
    gc_mode=mode;gc_extra=phase!=0;gc_epoch=phase>=2;gc_fail=fail;
    GcRequest request={0};request.kind=GC_CLAIM;gc_send(&request,1);
    REQUIRE(bench_scope_owner_idle(gc_producer));
    pose(gc_extra);material_fixture_verify_end(0);
    /* Producer only mutates its own mesh/pivot storage while worker draws. */
    unsigned refs=g_method.refs,captured=gc_captured;
#ifdef PF_GROUP_CAPTURE
    pf_timing_stats scope_before,scope_after;pf_timing_get_stats(&scope_before);
    unsigned bulk_before=gc_bulk_captured,prepares=ng_prepares,finishes=ng_finishes;
#endif
    if(!mode){material_fixture_epoch(gc_epoch);material_fixture_clock(gc_epoch*33);ng_frame_mode(gc_native_optimized);bench_extra_mode(gc_extra);}
    else bench_native_group_mode(0);
    request.kind=GC_FRAME;request.extra=gc_extra;request.check=check;
#ifdef PF_GROUP_BATCH_FIRST
    request.batch_first=mode>=3?gc_batch_first:0;request.recovery=gc_failure_probe;
#endif
    gc_send(&request,1);
    GC_PROFILE_START(groups_start);
    for(unsigned group=0;group<GROUPS;group++) {
        ng_current_group=group;ng_begin_fail=fail && group==7;
        unsigned hooked=!mode && gc_native_optimized;
#ifdef PF_GROUP_CAPTURE
        if(mode>=3 && gc_fast_capture)hooked=1;
#endif
        ng_group_call(ng_target(hooked),ng_renderer,records[group],records[group]+gc_group_size*PF_RECORD_SIZE);
    }
    ng_begin_fail=0;
#ifdef PF_GROUP_COMMANDS
    if(mode>=3)gc_command_flush();
#endif
    GC_PROFILE_STOP(groups_start,GCP_GROUP);
    request.kind=GC_FINISH;gc_send(&request,1);
#ifdef PF_GROUP_COMMANDS
    REQUIRE(!gc_command_references && !gc_commands.count && !gc_command_capture && !gc_command_lifetime);
#endif
    if(!mode)bench_extra_mode(0);
    REQUIRE(!ng_group_abi_errors && g_method.refs==refs && bench_scope_owner_idle(gc_producer));
    if(mode)REQUIRE(gc_captured-captured==(GROUPS-(fail?1:0))*gc_group_size);
#ifdef PF_GROUP_CAPTURE
    if(mode) {
        pf_timing_get_stats(&scope_after);
        REQUIRE(scope_after.scopes-scope_before.scopes==(GROUPS-(fail?1:0))*gc_group_size+GROUPS);
        REQUIRE(scope_after.stale_scopes==scope_before.stale_scopes);
        REQUIRE(ng_prepares-prepares==GROUPS && ng_finishes-finishes==GROUPS-(fail?1:0));
        if(gc_fast_capture && gc_group_size==GROUP_SIZE && !nm_begin_callback && !nm_end_callback)
            REQUIRE(gc_bulk_captured-bulk_before==(GROUPS-(fail?1:0))*(GROUP_SIZE-1));
        else REQUIRE(gc_bulk_captured==bulk_before);
    }
#endif
#ifdef PF_GROUP_BATCH_FIRST
    if(mode && gc_first_observing) {
        unsigned per=gc_batch_first?(gc_group_size+consumer.capacity-1)/consumer.capacity:
            1+(gc_group_size-1+consumer.capacity-1)/consumer.capacity;
        unsigned expected=(GROUPS-(fail?1:0))*per;
        if(gc_failure_probe)expected+=gc_group_size-per;
        REQUIRE(gc_first_draws==expected && gc_first_failures==gc_failure_probe &&
                consumer_recoveries==gc_failure_probe && !consumer_expect_recovery && !gc_first_fail_armed);
    }
#endif
    GC_PROFILE_STOP(frame_start,GCP_FRAME);
}

#ifdef PF_GROUP_BATCH_FIRST
static void gc_first_validate(void)
{
    static unsigned expected[RT*RT],state[1600];unsigned cases=0,recoveries=0;
    const unsigned sizes[]={1,2,consumer.capacity,consumer.capacity+1,GROUP_SIZE};
    for(unsigned phase=0;phase<2;phase++)for(unsigned si=0;si<5;si++) {
        gc_group_size=sizes[si];gc_native_optimized=0;gc_frame(0,phase,1,0);
        memcpy(expected,gc_output,sizeof expected);unsigned words=gc_state_words;memcpy(state,gc_state,words*4);
        gc_native_optimized=1;gc_frame(0,phase,1,0);
        REQUIRE(gc_pixels_equal(expected,gc_output) && words==gc_state_words && !memcmp(state,gc_state,words*4));
        for(unsigned choice=0;choice<3;choice++) {
            gc_batch_first=choice!=0;gc_fast_capture=1;
            gc_frame(choice==1?3:4,phase,1,0);
            REQUIRE(gc_pixels_equal(expected,gc_output) && words==gc_state_words && !memcmp(state,gc_state,words*4));cases++;
        }
    }
    gc_group_size=GROUP_SIZE;gc_batch_first=gc_fast_capture=1;
    for(unsigned phase=0;phase<2;phase++)for(unsigned mode=3;mode<=4;mode++) {
        gc_frame(mode,phase,1,0);memcpy(expected,gc_output,sizeof expected);
        unsigned words=gc_state_words;memcpy(state,gc_state,words*4);
        gc_failure_probe=1;gc_frame(mode,phase,1,0);gc_failure_probe=0;
        REQUIRE(gc_pixels_equal(expected,gc_output) && words==gc_state_words && !memcmp(state,gc_state,words*4));recoveries++;
    }
    printf("FIRST BATCH: %u size/image/state cases; %u first-batch shader failures recover with complete ordered replay; actual device draws checked\n",cases,recoveries);
}
#endif

int main(int argc,char **argv)
{
    REQUIRE(argc==11 && bench_no_game());gc_producer=GetCurrentThreadId();
    gc_fault=(unsigned)atoi(argv[10]);REQUIRE(gc_fault<=GC_MAX_FAULT);
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
    for(unsigned i=0;i<GC_EFFECTS;i++) {
        unsigned technique,block;material_fixture_identity(i);
        material_fixture_native(&gc_effects[i],&technique,&block);
        REQUIRE(gc_effects[i] && technique && block);
    }
    REQUIRE(gc_effects[0]!=gc_effects[1]);material_fixture_identity(0);
#ifdef PF_GROUP_COMMANDS
    bench_filter_mode(1);gc_command_audit(1);
#endif
#ifdef PF_GROUP_BATCH_FIRST
    gc_first_device_observer(1);
#endif
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
        for(unsigned candidate=1;candidate<=
#ifdef PF_GROUP_BATCH_FIRST
            6
#elif defined(PF_GROUP_CAPTURE)
            4
#else
            2
#endif
            ;candidate++) {
            unsigned mode=candidate;
#ifdef PF_GROUP_CAPTURE
            mode=1+(candidate-1)%2;gc_fast_capture=candidate>=3;
#endif
#ifdef PF_GROUP_BATCH_FIRST
            gc_batch_first=candidate>=5;
#endif
#ifdef PF_GROUP_COMMANDS
            mode+=2;
#endif
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
    printf(GC_CASE_NAME " %u image/state cases; lit pixels and state exact, shadow max difference=%u/2^24 (existing tolerance=%u); actual Begin reply, alternating effect identity, copied palette poisoning and native method ownership pass\n",cases,gc_depth_max,PF_DEPTH_TOLERANCE);
#ifdef PF_GROUP_CAPTURE
    for(unsigned mode=3;mode<=4;mode++)for(unsigned phase=0;phase<2;phase++)
    for(unsigned callbacks=1;callbacks<4;callbacks++) {
        nm_begin_callback=callbacks&1?ng_on_begin:NULL;nm_end_callback=callbacks&2?ng_on_end:NULL;
        gc_fast_capture=0;ng_callbacks_begin=ng_callbacks_end=0;ng_callbacks_hash=2166136261u;
        gc_frame(mode,phase,1,0);memcpy(expected,gc_output,sizeof expected);
        unsigned begin=ng_callbacks_begin,end=ng_callbacks_end,hash=ng_callbacks_hash;
        gc_fast_capture=1;ng_callbacks_begin=ng_callbacks_end=0;ng_callbacks_hash=2166136261u;
        gc_frame(mode,phase,1,0);
        REQUIRE(gc_pixels_equal(expected,gc_output) && ng_callbacks_begin==begin && ng_callbacks_end==end && ng_callbacks_hash==hash);
        REQUIRE(begin==((callbacks&1)?GROUPS*(GROUP_SIZE+1):0) && end==((callbacks&2)?GROUPS*(GROUP_SIZE+1):0));
    }
    nm_begin_callback=nm_end_callback=NULL;gc_fast_capture=1;
    puts("CAPTURE BULK: 12 callback refusal pairs; native references, prepare/finish, scope totals and captured palette counts pass");
#endif
#ifdef PF_GROUP_BATCH_FIRST
    gc_first_validate();gc_first_device_observer(0);
#endif
#ifdef PF_GROUP_COMMANDS
    gc_command_audit(0);
    REQUIRE(gc_command_audited && gc_command_callbacks && gc_command_restores);
    printf("COMMAND AUDIT: effects=%u callbacks=%u restored_groups=%u outstanding_refs=%ld scoped_lifetime_calls=%u; no producer state calls or unscoped device references; all audit wrappers removed before timing\n",gc_command_audited,gc_command_callbacks,gc_command_restores,gc_command_references,gc_command_lifetime_calls);
#endif
    for(unsigned round=0;round<12;round++) {
        REQUIRE(bench_no_game());double elapsed[5];
        unsigned columns=3;
#ifdef PF_GROUP_CAPTURE
        columns=4;
#endif
#ifdef PF_GROUP_BATCH_FIRST
        columns=5;
#endif
        for(unsigned offset=0;offset<columns;offset++) {
            unsigned column=(round+offset)%columns,mode=column;
#ifdef PF_GROUP_CAPTURE
            gc_fast_capture=column!=3;if(column==3)mode=2;
#endif
#ifdef PF_GROUP_BATCH_FIRST
            gc_batch_first=column==1 || column==2;if(column==4)mode=2;
#endif
#ifdef PF_GROUP_COMMANDS
            if(mode)mode+=2;
#endif
            /* Warm the selected ownership mode before its measured pair. */
            gc_frame(mode,0,0,0);
            LONGLONG start=ticks();gc_frame(mode,0,0,0);gc_frame(mode,1,0,0);
            elapsed[column]=(ticks()-start)*1000.0/qpf;
        }
        printf("sample,%u,%.4f,%.4f,%.4f,%.4f",round,elapsed[0],elapsed[1],elapsed[2],elapsed[0]-elapsed[2]);
#ifdef PF_GROUP_CAPTURE
        printf(",%.4f",elapsed[3]);gc_fast_capture=1;
#endif
#ifdef PF_GROUP_BATCH_FIRST
        printf(",%.4f",elapsed[4]);gc_batch_first=1;
#endif
        putchar('\n');
    }
#ifdef PF_GROUP_PROFILE
    for(unsigned round=0;round<12;round++)for(unsigned offset=0;offset<2;offset++) {
        unsigned mode=3+(round+offset)%2;
        REQUIRE(bench_no_game());double unprofiled=0;
        for(unsigned order=0;order<2;order++) {
            unsigned enabled=(round+order)%2;
            gc_frame(mode,0,0,0);
            if(enabled)memset(gc_profile_ticks,0,sizeof gc_profile_ticks);
            gc_profiling=enabled;
            LONGLONG start=ticks();gc_frame(mode,0,0,0);gc_frame(mode,1,0,0);
            if(!enabled)unprofiled=(ticks()-start)*1000.0/qpf;
            gc_profiling=0;
        }
        /* End includes full-batch publication. GROUPS is a batch multiple,
         * so the final explicit flush is empty in this fixed workload. */
        REQUIRE(GROUPS%CMD_GROUPS==0 && gc_profile_ticks[GCP_END]>=gc_profile_ticks[GCP_PUBLISH]);
        LONGLONG effects=gc_profile_ticks[GCP_BEGIN]+gc_profile_ticks[GCP_PASS]+
            gc_profile_ticks[GCP_END_PASS]+gc_profile_ticks[GCP_END]-gc_profile_ticks[GCP_PUBLISH];
        LONGLONG other=gc_profile_ticks[GCP_GROUP]-effects-gc_profile_ticks[GCP_TRAVERSE]-gc_profile_ticks[GCP_PUBLISH];
        REQUIRE(other>=0 && gc_profile_ticks[GCP_FRAME]>=gc_profile_ticks[GCP_GROUP]);
        printf("profile,%u,%u,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",round,mode,
            gc_profile_ticks[GCP_FRAME]*1000.0/qpf,effects*1000.0/qpf,
            gc_profile_ticks[GCP_TRAVERSE]*1000.0/qpf,gc_profile_ticks[GCP_PUBLISH]*1000.0/qpf,
            other*1000.0/qpf,(gc_profile_ticks[GCP_FRAME]-gc_profile_ticks[GCP_GROUP])*1000.0/qpf,unprofiled);
    }
#endif
    gc_mode=2;GcRequest end={0};end.kind=GC_CLAIM;gc_send(&end,1);end.kind=GC_STOP;gc_send(&end,1);
    gc_wait(gc_worker);DWORD exit_code;REQUIRE(GetExitCodeThread(gc_worker,&exit_code) && !exit_code && gc_sent==gc_received);
    CloseHandle(gc_worker);CloseHandle(gc_available);for(unsigned i=0;i<2;i++)CloseHandle(gc_slots[i].done);
    REQUIRE(gc_failures==
#ifdef PF_GROUP_BATCH_FIRST
        18
#elif defined(PF_GROUP_CAPTURE)
        12
#else
        6
#endif
        && gc_identity_seen[GC_EFFECTS-2] && gc_identity_seen[GC_EFFECTS-1]);
#ifdef PF_GROUP_COMMANDS
    REQUIRE(!gc_command_references);
    printf("COMMAND BATCHES: %u; maximum_bytes=%u; groups_per_batch=%u; retained references balanced\n",gc_command_batches,gc_command_max_bytes,CMD_GROUPS);
#endif
    printf("RESULTS: successful_begins=%u failed_begins=%u captured_meshes=%u worker_commands=%u; no game or runtime hook\n",gc_successes,gc_failures,gc_captured,gc_sent);
    REQUIRE(bench_no_game());return 0;
}
