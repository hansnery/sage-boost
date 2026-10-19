/* Optional frame-start protocol. The producer publishes owned commands;
 * the GPU owner claims, starts, executes and finishes each frame in FIFO
 * order. Final completion still waits for the GPU. */
static unsigned gc_start_style,gc_start_probe,gc_start_overlaps;
static unsigned gc_frame_id,gc_active_frame,gc_completed_frame;
static HANDLE gc_start_arrived,gc_start_release;
static void gc_frame(unsigned,unsigned,unsigned,unsigned);

static void gc_start_begin(const GcRequest *r)
{
    REQUIRE(!gc_active_frame && r->frame_id && r->frame_id==gc_completed_frame+1 && r->claim<=2);
    if(r->claim) {
        REQUIRE(gc_worker_mode());
        if(gc_fault!=14)consumer.owner=GetCurrentThreadId();
    }
    REQUIRE(consumer.owner==GetCurrentThreadId());
    if(r->probe) {
        REQUIRE(r->claim==2 && SetEvent(gc_start_arrived));
        REQUIRE(WaitForSingleObject(gc_start_release,10000)==WAIT_OBJECT_0);
    }
    gc_active_frame=r->frame_id;
}

static void gc_start_capture_done(void)
{
    if(!gc_start_probe || gc_mode!=4 || gc_start_style!=2 || ng_current_group)return;
    REQUIRE(gc_group.count==gc_group_size && gc_commands.groups==1 && gc_commands.count);
    gc_wait(gc_start_arrived);
    REQUIRE(WaitForSingleObject(gc_start_release,0)==WAIT_TIMEOUT);
    gc_start_overlaps++;REQUIRE(SetEvent(gc_start_release));
}

static void gc_start_validate(void)
{
    static unsigned expected[RT*RT],state[1600];unsigned cases=0,recoveries=0;
    gc_fast_capture=gc_batch_first=1;
    for(unsigned fail=0;fail<2;fail++)for(unsigned phase=0;phase<3;phase++) {
        gc_start_style=0;gc_frame(3,phase,1,fail);memcpy(expected,gc_output,sizeof expected);
        unsigned words=gc_state_words;memcpy(state,gc_state,words*4);
        for(unsigned style=1;style<=2;style++) {
            gc_start_style=style;gc_start_probe=style==2;
            gc_frame(4,phase,1,fail);gc_start_probe=0;
            REQUIRE(gc_pixels_equal(expected,gc_output) && words==gc_state_words && !memcmp(state,gc_state,words*4));cases++;
        }
    }
    for(unsigned phase=0;phase<2;phase++)for(unsigned style=1;style<=2;style++) {
        gc_start_style=0;gc_frame(3,phase,1,0);memcpy(expected,gc_output,sizeof expected);
        unsigned words=gc_state_words;memcpy(state,gc_state,words*4);
        gc_start_style=style;gc_failure_probe=1;gc_frame(4,phase,1,0);gc_failure_probe=0;
        REQUIRE(gc_pixels_equal(expected,gc_output) && words==gc_state_words && !memcmp(state,gc_state,words*4));recoveries++;
    }
    REQUIRE(gc_start_overlaps==6 && !gc_active_frame && gc_completed_frame==gc_frame_id);
    gc_start_style=2;
    printf("FRAME START: %u image/state cases; %u held-worker capture probes; %u shader-failure recovery cases; frame sequence and owner checked through GPU completion\n",cases,gc_start_overlaps,recoveries);
}
