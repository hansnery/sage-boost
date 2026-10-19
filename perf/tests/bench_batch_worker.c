/* Offline owned-packet screen. By default the worker owns initialization;
 * PF_WINDOW_DEVICE_OWNER keeps the window/device creation on the producer.
 * No game callback or borrowed engine object crosses the queue. */
#ifdef PF_WINDOW_DEVICE_OWNER
#define PF_FIXTURE_DEVICE_FLAGS (D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED)
#include "material_packet.h"
static unsigned stale_material_values;
#endif
#define PF_MATERIAL_MAIN unused_material_benchmark_main
#include "bench_material_queue.c"
#ifdef PF_NATIVE_SOURCE_WORKER
#include "source_packet.h"
#endif
#ifdef PF_NATIVE_POSE_WORKER
#include "pose_packet.h"
static unsigned stale_pose_values;
static float worker_palettes[GROUPS*GROUP_SIZE][BONES][8];
#endif

#define WORKER_SLOTS 2
#define WORKER_JOBS 8
#define WORKER_STATE_WORDS 1300
#ifdef PF_NATIVE_SOURCE_WORKER
#define WORKER_ORACLES 4
#else
#define WORKER_ORACLES 3
#endif
typedef struct {
    unsigned sequence,epoch,clock,extra,check,oracle;
#ifdef PF_NATIVE_SOURCE_WORKER
    unsigned source_phase;
    source_matrix_command sources[GROUPS*3];
#endif
#ifdef PF_WINDOW_DEVICE_OWNER
    fixture_material_values materials[GROUPS];
#endif
    float palettes[GROUPS*GROUP_SIZE][BONES][8];
} render_packet;
enum { SLOT_FREE, SLOT_WRITING, SLOT_READY, SLOT_READING, SLOT_DONE };
typedef struct {
    volatile LONG state;
    HANDLE done;
    unsigned sequence,checksum,fault_alias,direct,has_checksum;
    long long published,render_ticks,queue_ticks;
    unsigned rejected,pixel_hash;
    render_packet input;
} packet_slot;
static packet_slot slots[WORKER_SLOTS];
static render_packet scratch;
static float templates[2][GROUPS*GROUP_SIZE][BONES][8];
static HANDLE available,initialized,consume_gate;
static volatile LONG stopping;
static unsigned submitted,collected;
static unsigned integrity_checks=1;
static int worker_argc;
static char **worker_argv;
static DWORD render_thread;
#ifdef PF_WINDOW_DEVICE_OWNER
static DWORD window_thread;
extern int bench_scope_transfer_owner(DWORD);
extern int bench_inst_transfer_owner(DWORD);
static HWND owner_window;
static WNDPROC owner_previous_proc;
static unsigned owner_messages;
#define OWNER_MESSAGE (WM_APP+90)

static LRESULT CALLBACK owner_window_proc(HWND window,UINT message,WPARAM w,LPARAM l)
{
    REQUIRE(GetCurrentThreadId()==window_thread);
    if(message==OWNER_MESSAGE) {
        REQUIRE(w==++owner_messages);
        return 0;
    }
    return CallWindowProcW(owner_previous_proc,window,message,w,l);
}

static void pump_owner_messages(void)
{
    REQUIRE(GetCurrentThreadId()==window_thread);
    MSG message;
    while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)) {
        TranslateMessage(&message);DispatchMessageW(&message);
    }
}

static DWORD wait_owner(HANDLE handle,DWORD milliseconds)
{
    REQUIRE(GetCurrentThreadId()==window_thread);
    ULONGLONG end=GetTickCount64()+milliseconds;
    for(;;) {
        ULONGLONG now=GetTickCount64();
        if(now>=end)return WAIT_TIMEOUT;
        DWORD status=MsgWaitForMultipleObjectsEx(1,&handle,(DWORD)(end-now),QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        pump_owner_messages();
        if(status!=WAIT_OBJECT_0+1)return status;
    }
}

static void verify_window_owner(void)
{
    D3DDEVICE_CREATION_PARAMETERS creation;
    HR(IDirect3DDevice9_GetCreationParameters(g_dev,&creation));
    REQUIRE(creation.BehaviorFlags & D3DCREATE_MULTITHREADED);
    owner_window=creation.hFocusWindow;
    REQUIRE(owner_window && GetWindowThreadProcessId(owner_window,NULL)==window_thread);
    HR(IDirect3DDevice9_TestCooperativeLevel(g_dev));
    owner_previous_proc=(WNDPROC)SetWindowLongPtrW(owner_window,GWLP_WNDPROC,(LONG_PTR)owner_window_proc);
    REQUIRE(owner_previous_proc);
    printf("DEVICE OWNER: window/create/cooperative-level thread=%lu flags=%08lX; worker drawing follows; Reset/destruction unqualified\n",
           (unsigned long)window_thread,(unsigned long)creation.BehaviorFlags);
}
#endif
#ifdef PF_NATIVE_SOURCE_WORKER
static float source_seed[2][3][16];
static int wrong_source_setter,stale_source_values;
#endif
static unsigned reference[WORKER_ORACLES][RT*RT],reference_state[WORKER_ORACLES][WORKER_STATE_WORDS],state_words[WORKER_ORACLES];

static unsigned packet_hash(const void *bytes,unsigned n)
{
    const unsigned char *p=bytes;unsigned h=2166136261u;
    for(unsigned i=0;i<n;i++)h=(h^p[i])*16777619u;
    return h;
}

static void deterministic_state(void)
{
    static const float zero[256*4]={0};
    HR(IDirect3DDevice9_SetVertexShaderConstantF(g_dev,0,zero,256));
    HR(IDirect3DDevice9_SetPixelShaderConstantF(g_dev,0,zero,32));
    HR(IDirect3DDevice9_SetRenderState(g_dev,D3DRS_CULLMODE,D3DCULL_CW));
}

static unsigned backend_state(unsigned out[WORKER_STATE_WORDS])
{
    unsigned n=0;DWORD value;
    drain();
    HR(IDirect3DDevice9_GetVertexShaderConstantF(g_dev,0,(float *)(out+n),256));n+=1024;
    HR(IDirect3DDevice9_GetPixelShaderConstantF(g_dev,0,(float *)(out+n),32));n+=128;
    static const D3DRENDERSTATETYPE keys[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,
        D3DRS_ZFUNC,D3DRS_CULLMODE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAREF,
        D3DRS_ALPHAFUNC,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,
        D3DRS_COLORWRITEENABLE,D3DRS_STENCILENABLE};
    for(unsigned i=0;i<sizeof keys/sizeof *keys;i++) {
        HR(IDirect3DDevice9_GetRenderState(g_dev,keys[i],&value));out[n++]=value;
    }
    for(unsigned slot=0;slot<8;slot++)for(unsigned key=1;key<=13;key++) {
        HR(IDirect3DDevice9_GetSamplerState(g_dev,slot,key,&value));out[n++]=value;
    }
    for(unsigned slot=0;slot<2;slot++) {
        UINT freq;HR(IDirect3DDevice9_GetStreamSourceFreq(g_dev,slot,&freq));out[n++]=freq;
    }
    REQUIRE(n<=WORKER_STATE_WORDS);return n;
}

static void render_packet_owned(const render_packet *p,unsigned *pixels,unsigned direct)
{
    REQUIRE(GetCurrentThreadId()==render_thread);
    if(p->check)deterministic_state();
#ifdef PF_NATIVE_POSE_WORKER
    const float *palettes=(const float*)p->palettes;
    if(direct) {
        native_pose_fill((const float*)templates[p->extra!=0],(float*)worker_palettes,GROUPS*GROUP_SIZE*BONES);
        palettes=(const float*)worker_palettes;
    }
    for(unsigned i=0;i<GROUPS*GROUP_SIZE;i++)for(unsigned b=0;b<BONES;b++)
        memcpy(many_pivots[i]+g_bonemap[b]*0x58+0x30,palettes+(i*BONES+b)*8,28);
#else
    if(direct)pose(p->extra);
    else for(unsigned i=0;i<GROUPS*GROUP_SIZE;i++)for(unsigned b=0;b<BONES;b++)
        memcpy(many_pivots[i]+g_bonemap[b]*0x58+0x30,p->palettes[i][b],28);
#endif
    material_fixture_epoch(p->epoch);
#ifdef PF_WINDOW_DEVICE_OWNER
    material_fixture_captured(p->materials,GROUPS);
#else
    material_fixture_clock(p->clock);
#endif
#ifdef PF_NATIVE_SOURCE_WORKER
    material_fixture_source_packets(p->sources,GROUPS,wrong_source_setter);
#endif
    pair_optimized=0;retain_now=0;dispatch_bulk_now=1;
    unsigned refs=g_method.refs;
    frame_draw(p->extra,0,pixels);drain();
#ifdef PF_WINDOW_DEVICE_OWNER
    material_fixture_captured(NULL,0);
#endif
#ifdef PF_NATIVE_SOURCE_WORKER
    material_fixture_source_packets(NULL,0,0);
#endif
    REQUIRE(g_method.refs==refs);
}

static void initialize_worker(void)
{
    const char *crt=worker_argv[2],*slash=strrchr(crt,'/'),*backslash=strrchr(crt,'\\');
    if(backslash && (!slash || backslash>slash))slash=backslash;
    REQUIRE(slash && (unsigned)(slash-crt)+1+sizeof "d3dx9_27.dll"<=sizeof material_runtime);
    memcpy(material_runtime,crt,(size_t)(slash-crt)+1);
    strcpy(material_runtime+(slash-crt)+1,"d3dx9_27.dll");
    material_setup=effect_setup;material_begin=material_fixture_begin;
    material_end=material_fixture_end;pose_adjust=spread;
    baseline_bulk_enabled=1;dispatch_scope_begin=bench_scope_bulk_begin;
    dispatch_scope_end=bench_scope_bulk_end;
    setup(worker_argc,worker_argv);pf_timing_set_detail(1);
#ifdef PF_NATIVE_SOURCE_WORKER
    material_fixture_source_seed(source_seed[0]);
    memcpy(source_seed[1],source_seed[0],sizeof source_seed[0]);
    source_seed[1][0][12]+=.25f;source_seed[1][1][12]+=.125f;
    source_seed[1][2][0]*=.8f;source_seed[1][2][5]*=.9f;source_seed[1][2][12]+=.1f;
#endif
    for(unsigned phase=0;phase<2;phase++) {
        pose(phase);
        for(unsigned i=0;i<GROUPS*GROUP_SIZE;i++)for(unsigned b=0;b<BONES;b++) {
            memcpy(templates[phase][i][b],many_pivots[i]+g_bonemap[b]*0x58+0x30,28);
            templates[phase][i][b][7]=0;
        }
    }
    for(unsigned c=0;c<WORKER_ORACLES;c++) {
        deterministic_state();pose(c!=0);
#ifdef PF_NATIVE_POSE_WORKER
        native_pose_fill((const float*)templates[c!=0],(float*)worker_palettes,GROUPS*GROUP_SIZE*BONES);
        for(unsigned i=0;i<GROUPS*GROUP_SIZE;i++)for(unsigned b=0;b<BONES;b++)
            memcpy(many_pivots[i]+g_bonemap[b]*0x58+0x30,worker_palettes[i][b],28);
#endif
        material_fixture_epoch(c>=2);material_fixture_clock(c>=2?33:0);
#ifdef PF_NATIVE_SOURCE_WORKER
        /* Independent reference uses ordinary SetValue, not the packet
         * sink or transposed replay path being checked. */
        material_fixture_source_reference(c==3?source_seed[1]:NULL);
#endif
        pair_optimized=0;retain_now=0;dispatch_bulk_now=1;
        frame_draw(c!=0,0,reference[c]);state_words[c]=backend_state(reference_state[c]);
    }
#ifdef PF_NATIVE_SOURCE_WORKER
    material_fixture_source_reference(NULL);
    unsigned changed_sources=0;
    for(unsigned i=0;i<RT*RT;i++)changed_sources+=reference[2][i]!=reference[3][i];
    REQUIRE(changed_sources>100);
    printf("SOURCE ORACLE: changed matrices alter %u pixels, hash=%08X\n",changed_sources,packet_hash(reference[3],sizeof reference[3]));
#endif
    unsigned covered=0,moved=0,material=0;
    for(unsigned i=0;i<RT*RT;i++) {
        covered+=reference[0][i]!=(shadow_mode?PF_DEPTH_CLEAR:0);
        moved+=reference[0][i]!=reference[1][i];material+=reference[1][i]!=reference[2][i];
    }
    REQUIRE(covered>1000 && moved>100 && material>100);
    printf("ORACLE: %s covered=%u fresh_pose=%u changed_material=%u hashes=%08X/%08X/%08X\n",
        shadow_mode?"shadow":"lit",covered,moved,material,
        packet_hash(reference[0],sizeof reference[0]),packet_hash(reference[1],sizeof reference[1]),
        packet_hash(reference[2],sizeof reference[2]));
#ifdef PF_NATIVE_POSE_WORKER
    /* The native calculation must actually affect the rendered picture;
     * a no-op decoder would only manufacture unrelated producer work. */
    static unsigned raw_pose[RT*RT];unsigned decoded_changed=0;
    deterministic_state();pose(0);material_fixture_epoch(0);material_fixture_clock(0);
    pair_optimized=0;retain_now=0;dispatch_bulk_now=1;
    frame_draw(0,0,raw_pose);
    for(unsigned i=0;i<RT*RT;i++)decoded_changed+=raw_pose[i]!=reference[0][i];
    REQUIRE(decoded_changed>100);
    printf("POSE ORACLE: native decoded endpoints change %u pixels versus raw templates\n",decoded_changed);
#endif
}

static DWORD WINAPI worker_main(void *unused)
{
    (void)unused;render_thread=GetCurrentThreadId();
#ifdef PF_WINDOW_DEVICE_OWNER
    REQUIRE(render_thread!=window_thread);
    REQUIRE(bench_scope_transfer_owner(window_thread));
    REQUIRE(bench_inst_transfer_owner(window_thread));
#else
    initialize_worker();
#endif
    REQUIRE(SetEvent(initialized));unsigned consumed=0;
    for(;;) {
        DWORD wait=MsgWaitForMultipleObjects(1,&available,FALSE,10000,QS_ALLINPUT);
        if(wait==WAIT_OBJECT_0+1) {
            MSG msg;while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            continue;
        }
        REQUIRE(wait==WAIT_OBJECT_0);
        if(InterlockedCompareExchange(&stopping,0,0))break;
        packet_slot *slot=&slots[consumed%WORKER_SLOTS];
        REQUIRE(InterlockedCompareExchange(&slot->state,SLOT_READING,SLOT_READY)==SLOT_READY);
        REQUIRE(WaitForSingleObject(consume_gate,10000)==WAIT_OBJECT_0);
        REQUIRE(slot->sequence==++consumed);
        const render_packet *p=slot->fault_alias?&scratch:&slot->input;
        long long begin=ticks();slot->queue_ticks=begin-slot->published;
        unsigned bytes=slot->direct?offsetof(render_packet,palettes):sizeof *p;
        slot->rejected=slot->has_checksum && packet_hash(p,bytes)!=slot->checksum;
        if(!slot->rejected) {
            REQUIRE(p->sequence==slot->sequence);
            static unsigned pixels[RT*RT],state[WORKER_STATE_WORDS];
            render_packet_owned(p,p->check?pixels:NULL,slot->direct);
            if(p->check) {
                REQUIRE(p->oracle<WORKER_ORACLES);
                unsigned n=backend_state(state);
                REQUIRE(!memcmp(pixels,reference[p->oracle],sizeof pixels));
                unsigned differences=0;
                for(unsigned i=0;i<n && i<state_words[p->oracle];i++)if(state[i]!=reference_state[p->oracle][i]) {
                    if(differences++<8)printf("STATE DIFFERENCE: oracle=%u word=%u expected=%08X actual=%08X\n",
                                              p->oracle,i,reference_state[p->oracle][i],state[i]);
                }
                REQUIRE(n==state_words[p->oracle] && !memcmp(state,reference_state[p->oracle],n*4));
                slot->pixel_hash=packet_hash(pixels,sizeof pixels);
            }
            if(slot->has_checksum)REQUIRE(packet_hash(p,bytes)==slot->checksum);
        }
        slot->render_ticks=ticks()-begin;
#ifdef PF_WINDOW_DEVICE_OWNER
        REQUIRE(PostMessageW(owner_window,OWNER_MESSAGE,consumed,0));
#endif
        REQUIRE(InterlockedExchange(&slot->state,SLOT_DONE)==SLOT_READING);
        REQUIRE(SetEvent(slot->done));
    }
    drain();bench_retained_clear();IDirect3DStateBlock9_Release(outer);
    IDirect3DQuery9_Release(fence);
    printf("WORKER: consumed=%u thread=%lu; GPU work drained before every completion\n",consumed,(unsigned long)render_thread);
    return 0;
}

static void prepare(unsigned extra,unsigned epoch,unsigned clock,unsigned check,unsigned oracle,unsigned direct)
{
    scratch.sequence=submitted+1;scratch.extra=extra;scratch.epoch=epoch;scratch.clock=clock;
    scratch.check=check;scratch.oracle=oracle;
#ifdef PF_WINDOW_DEVICE_OWNER
    for(unsigned group=0;group<GROUPS;group++)
        material_fixture_capture(group,stale_material_values?0:epoch,100,clock,&scratch.materials[group]);
#endif
#ifdef PF_NATIVE_SOURCE_WORKER
    scratch.source_phase=check?(oracle==3):(epoch&1);
    unsigned phase=stale_source_values?0:scratch.source_phase;
    for(unsigned group=0;group<GROUPS;group++)native_source_capture(source_seed[phase],&scratch.sources[group*3]);
#endif
    if(!direct) {
#ifdef PF_NATIVE_POSE_WORKER
        native_pose_fill((const float*)templates[stale_pose_values?0:extra!=0],(float*)scratch.palettes,GROUPS*GROUP_SIZE*BONES);
#else
        memcpy(scratch.palettes,templates[extra!=0],sizeof scratch.palettes);
#endif
    }
}

static int next_free(void)
{
    return InterlockedCompareExchange(&slots[submitted%WORKER_SLOTS].state,0,0)==SLOT_FREE;
}

static void publish(unsigned fault_alias,unsigned direct)
{
    packet_slot *slot=&slots[submitted%WORKER_SLOTS];
    REQUIRE(InterlockedCompareExchange(&slot->state,SLOT_WRITING,SLOT_FREE)==SLOT_FREE);
    REQUIRE(scratch.sequence==submitted+1);
    unsigned bytes=direct?offsetof(render_packet,palettes):sizeof scratch;
    memcpy(&slot->input,&scratch,bytes);
    slot->direct=direct;slot->has_checksum=integrity_checks || scratch.check || fault_alias;
    slot->checksum=slot->has_checksum?packet_hash(&slot->input,bytes):0;
    slot->sequence=++submitted;slot->fault_alias=fault_alias;slot->rejected=0;slot->pixel_hash=0;
    slot->published=ticks();
    REQUIRE(InterlockedExchange(&slot->state,SLOT_READY)==SLOT_WRITING);
    REQUIRE(ReleaseSemaphore(available,1,NULL));
}

static void collect(unsigned expect_rejected,long long *worker,long long *queue)
{
    packet_slot *slot=&slots[collected%WORKER_SLOTS];
#ifdef PF_WINDOW_DEVICE_OWNER
    REQUIRE(wait_owner(slot->done,10000)==WAIT_OBJECT_0);
#else
    REQUIRE(WaitForSingleObject(slot->done,10000)==WAIT_OBJECT_0);
#endif
    REQUIRE(slot->sequence==collected+1 && slot->rejected==expect_rejected);
    if(worker)*worker+=slot->render_ticks;
    if(queue)*queue+=slot->queue_ticks;
    REQUIRE(InterlockedCompareExchange(&slot->state,SLOT_FREE,SLOT_DONE)==SLOT_DONE);collected++;
}

static void ownership_controls(void)
{
    for(unsigned c=0;c<WORKER_ORACLES;c++) {
        REQUIRE(ResetEvent(consume_gate));prepare(c!=0,c>=2,c>=2?33:0,1,c,0);publish(0,0);
        memset(&scratch,0xA5,sizeof scratch);REQUIRE(SetEvent(consume_gate));collect(0,NULL,NULL);
    }
    REQUIRE(ResetEvent(consume_gate));prepare(0,0,0,1,0,0);publish(1,0);
    memset(&scratch,0xA5,sizeof scratch);REQUIRE(SetEvent(consume_gate));collect(1,NULL,NULL);
    REQUIRE(ResetEvent(consume_gate));
    prepare(0,0,0,1,0,0);publish(0,0);prepare(1,0,0,1,1,0);publish(0,0);
    REQUIRE(!next_free()); /* Both slots owned: a third publication is refused. */
    memset(&scratch,0xA5,sizeof scratch);REQUIRE(SetEvent(consume_gate));
    collect(0,NULL,NULL);collect(0,NULL,NULL);REQUIRE(next_free());
    printf("OWNERSHIP: %u exact image/state comparisons survive scratch overwrite; deliberate borrowed-buffer control rejected; two-slot backpressure and recovery pass\n",WORKER_ORACLES+2);
}

static void worker_sample(unsigned mode,unsigned round,double result[5])
{
    unsigned pipeline=mode==2,direct=mode==0;
    long long preparation=0,waits=0,worker=0,queue=0,begin=ticks();
    for(unsigned i=0;i<WORKER_JOBS;i++) {
        if(pipeline && i>=WORKER_SLOTS) {
            long long start=ticks();collect(0,&worker,&queue);waits+=ticks()-start;
        }
        long long start=ticks();prepare(i&1,(round+i/2)&1,(round*4+i/2)*33,0,0,direct);publish(0,direct);
        preparation+=ticks()-start;
        if(!pipeline){start=ticks();collect(0,&worker,&queue);waits+=ticks()-start;}
    }
    if(pipeline)for(unsigned i=0;i<WORKER_SLOTS;i++) {
        long long start=ticks();collect(0,&worker,&queue);waits+=ticks()-start;
    }
    long long complete=ticks()-begin;
    REQUIRE(submitted==collected);
    double scale=1000.0/qpf/(WORKER_JOBS/2);
    result[0]=preparation*scale;result[1]=waits*scale;result[2]=complete*scale;
    result[3]=worker*scale;result[4]=queue*scale;
}

int main(int argc,char **argv)
{
#ifdef PF_NATIVE_SOURCE_WORKER
#ifdef PF_NATIVE_POSE_WORKER
    REQUIRE((argc==9 || argc==10) && bench_no_game());
    unsigned fault_arg=9;
    REQUIRE((argv[8][0]=='0' || argv[8][0]=='1') && !argv[8][1]);
    native_pose_init(argv[5],argv[7],(unsigned)(argv[8][0]-'0'));
#else
    REQUIRE((argc==7 || argc==8) && bench_no_game());
    unsigned fault_arg=7;
#endif
    REQUIRE(!strcmp(argv[4],"checked") || !strcmp(argv[4],"owned"));
    integrity_checks=!strcmp(argv[4],"checked");
    if((unsigned)argc>fault_arg) {
#ifdef PF_NATIVE_POSE_WORKER
        stale_pose_values=!strcmp(argv[fault_arg],"stale-pose");
#ifdef PF_WINDOW_DEVICE_OWNER
        stale_material_values=!strcmp(argv[fault_arg],"stale-material");
        if(!strcmp(argv[fault_arg],"device-flags"))fixture_device_flags&=~D3DCREATE_MULTITHREADED;
        else if(!stale_material_values)
#endif
        REQUIRE(stale_pose_values || !strcmp(argv[fault_arg],"wrong-setter") || !strcmp(argv[fault_arg],"stale-source"));
#else
        REQUIRE(!strcmp(argv[fault_arg],"wrong-setter") || !strcmp(argv[fault_arg],"stale-source"));
#endif
        wrong_source_setter=!strcmp(argv[fault_arg],"wrong-setter");stale_source_values=!strcmp(argv[fault_arg],"stale-source");
    }
#else
    REQUIRE((argc==4 || argc==5) && bench_no_game());
    if(argc==5){REQUIRE(!strcmp(argv[4],"checked") || !strcmp(argv[4],"owned"));integrity_checks=!strcmp(argv[4],"checked");}
#endif
    REQUIRE(!strcmp(argv[3],"lit") || !strcmp(argv[3],"shadow"));
    worker_argc=argc;worker_argv=argv;
    available=CreateSemaphoreW(NULL,0,WORKER_SLOTS,NULL);
    initialized=CreateEventW(NULL,TRUE,FALSE,NULL);consume_gate=CreateEventW(NULL,TRUE,TRUE,NULL);
    REQUIRE(available && initialized && consume_gate);
    for(unsigned i=0;i<WORKER_SLOTS;i++){slots[i].done=CreateEventW(NULL,FALSE,FALSE,NULL);REQUIRE(slots[i].done);}
#ifdef PF_WINDOW_DEVICE_OWNER
    window_thread=GetCurrentThreadId();initialize_worker();verify_window_owner();
#endif
    HANDLE worker=CreateThread(NULL,0,worker_main,NULL,0,NULL);REQUIRE(worker);
#ifdef PF_WINDOW_DEVICE_OWNER
    REQUIRE(wait_owner(initialized,60000)==WAIT_OBJECT_0);
#else
    REQUIRE(WaitForSingleObject(initialized,60000)==WAIT_OBJECT_0);
#endif
    REQUIRE(render_thread!=GetCurrentThreadId());
#ifdef PF_NATIVE_SOURCE_WORKER
    native_source_capture_init(argv[5],argv[6],material_runtime);
#endif
    ownership_controls();
    printf("PACKET: %u bytes, %u owned slots; timed checksums=%u, all copies included; worker owns drawing after initialization\n",(unsigned)sizeof(render_packet),WORKER_SLOTS,integrity_checks);
#ifdef PF_NATIVE_SOURCE_WORKER
#ifdef PF_NATIVE_POSE_WORKER
    puts("LIMITS: native matrix capture and 43,200 warm vector decodes per draw; decoded next endpoints feed geometry; synthetic channels/cursors, bulk-after-second, fixed resources; no simulation, interpolation/hierarchy composition, terrain/reflections/Present or gameplay FPS");
#else
    puts("LIMITS: three native matrix sources on producer; immutable palette templates, synthetic single-pass geometry, bulk-after-second prototype; no live engine lifetimes, terrain/reflections/Present, artificial logic workload or gameplay FPS");
#endif
#else
    puts("LIMITS: immutable fixture templates, synthetic single-pass geometry, bulk-after-second prototype; no native capture, terrain/reflections/Present, artificial logic workload or gameplay FPS");
#endif
    puts("round,direct_prepare_ms,serial_prepare_ms,pipeline_prepare_ms,direct_wait_ms,serial_wait_ms,pipeline_wait_ms,direct_complete_ms,serial_complete_ms,pipeline_complete_ms,direct_worker_ms,serial_worker_ms,pipeline_worker_ms,direct_queue_ms,serial_queue_ms,pipeline_queue_ms,saved_vs_direct_ms,saved_vs_serial_ms");
    double saving[21],serial_saving[21];
    static const unsigned order[6][3]={{0,1,2},{2,1,0},{1,2,0},{0,2,1},{1,0,2},{2,0,1}};
    for(unsigned r=0;r<24;r++) {
        double result[3][5];REQUIRE(bench_no_game());
        for(unsigned j=0;j<3;j++){unsigned mode=order[r%6][j];worker_sample(mode,r,result[mode]);}
        if(r<3)continue;
        saving[r-3]=result[0][2]-result[2][2];serial_saving[r-3]=result[1][2]-result[2][2];
        printf("sample,%u",r-3);
        for(unsigned col=0;col<5;col++)for(unsigned mode=0;mode<3;mode++)printf(",%.4f",result[mode][col]);
        printf(",%.4f,%.4f\n",saving[r-3],serial_saving[r-3]);
    }
    qsort(saving,21,sizeof(double),compare_double);
    qsort(serial_saving,21,sizeof(double),compare_double);
    printf("SUMMARY: %s completion_saving_vs_direct=%.4f vs_serial_copy=%.4f ms per regular/extra pair\n",argv[3],saving[10],serial_saving[10]);
    ownership_controls(); /* Recheck output and ownership after sustained reuse. */
    REQUIRE(submitted==collected);InterlockedExchange(&stopping,1);REQUIRE(ReleaseSemaphore(available,1,NULL));
#ifdef PF_WINDOW_DEVICE_OWNER
    REQUIRE(wait_owner(worker,10000)==WAIT_OBJECT_0);
    REQUIRE(owner_messages==submitted);
    REQUIRE(bench_scope_transfer_owner(render_thread));
    REQUIRE(bench_inst_transfer_owner(render_thread));
    HR(IDirect3DDevice9_TestCooperativeLevel(g_dev));
    REQUIRE((WNDPROC)SetWindowLongPtrW(owner_window,GWLP_WNDPROC,(LONG_PTR)owner_previous_proc)==owner_window_proc);
    printf("WINDOW OWNER: %u ordered worker messages dispatched on producer; no Direct3D calls in window procedure\n",owner_messages);
#else
    REQUIRE(WaitForSingleObject(worker,10000)==WAIT_OBJECT_0);
#endif
    DWORD code;REQUIRE(GetExitCodeThread(worker,&code) && !code);CloseHandle(worker);
    for(unsigned i=0;i<WORKER_SLOTS;i++)CloseHandle(slots[i].done);
    CloseHandle(available);CloseHandle(initialized);CloseHandle(consume_gate);
#ifdef PF_NATIVE_SOURCE_WORKER
    unsigned captured=native_source_capture_calls();REQUIRE(captured==submitted*GROUPS*3);
    printf("CAPTURE: %u native matrix callbacks, provider storage poisoned before publication; copied typed setters only\n",captured);
    native_source_capture_close();
#endif
#ifdef PF_NATIVE_POSE_WORKER
    /* Four reference renders, plus one generated palette for every job,
     * including the two deliberately rejected borrowed packets. */
    native_pose_close(submitted+WORKER_ORACLES);
#endif
    REQUIRE(bench_no_game());return 0;
}
