/* Keep checked copies/readbacks outside timing. Every checked intermediate
 * frame has distinct storage; a correct final frame cannot conceal a loss. */
static void gc_completion_collect(void)
{
    GcRequest r={0};r.kind=GC_FLUSH;gc_send(&r,1);
    REQUIRE(!gc_completion_pending && !gc_command_references);
}

static unsigned gc_completion_phase(unsigned index)
{
    static const unsigned phases[GC_COMPLETION_MAX]={0,1,2,1,0,2,0,1};
    REQUIRE(index<GC_COMPLETION_MAX);return phases[index];
}

static void gc_completion_validate(void)
{
    static unsigned expected[GC_COMPLETION_MAX][RT*RT],states[GC_COMPLETION_MAX][1600],words[GC_COMPLETION_MAX];
    D3DSURFACE_DESC desc;HR(IDirect3DSurface9_GetDesc(g_rt,&desc));
    unsigned cases=0,batches=0;
    gc_start_style=2;gc_fast_capture=gc_batch_first=gc_native_optimized=1;
    for(unsigned i=0;i<GC_COMPLETION_MAX;i++) {
        HR(IDirect3DDevice9_CreateRenderTarget(g_dev,RT,RT,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&gc_completion_surfaces[i],NULL));
        gc_frame(0,gc_completion_phase(i),1,0);
        memcpy(expected[i],gc_output,sizeof gc_output);words[i]=gc_state_words;
        memcpy(states[i],gc_state,words[i]*4);
    }
    for(unsigned count=2;count<=GC_COMPLETION_MAX;count*=2)for(unsigned choice=0;choice<3;choice++) {
        unsigned mode=choice?choice+2:0;
        gc_completion_deferred=gc_completion_save=1;
        unsigned drains=gc_completion_drains,frames=gc_completion_frames;
        for(unsigned i=0;i<count;i++) {
            if(gc_fault==17 && i==1)gc_completion_enter(mode==0?3:0);
            gc_frame(mode,gc_completion_phase(i),1,0);
        }
        REQUIRE(gc_completion_pending==count && gc_completion_drains==drains);
        gc_completion_collect();
        REQUIRE(gc_completion_drains==drains+1 && gc_completion_frames==frames+count);
        /* Corrupt only a middle result, leaving the last image intact. */
        if(gc_fault==16 && count>=4)memcpy(gc_completion_pixels[1],gc_completion_pixels[0],sizeof gc_output);
        for(unsigned i=0;i<count;i++) {
            REQUIRE(gc_pixels_equal(expected[i],gc_completion_pixels[i]));
            REQUIRE(words[i]==gc_completion_saved_words[i] && !memcmp(states[i],gc_completion_state[i],words[i]*4));
            cases++;
        }
        gc_completion_deferred=gc_completion_save=0;batches++;
    }
    for(unsigned i=0;i<GC_COMPLETION_MAX;i++) {
        IDirect3DSurface9_Release(gc_completion_surfaces[i]);gc_completion_surfaces[i]=NULL;
    }
    printf("COMPLETION: %u intermediate image/state cases in %u batches; depths=2/4/8; one final GPU drain per batch; all images retained before reuse\n",cases,batches);
}

static void gc_completion_measure(void)
{
    REQUIRE(!gc_completion_pending && !gc_completion_save && !gc_command_verify);
    gc_start_style=2;gc_fast_capture=gc_batch_first=gc_native_optimized=1;
    for(unsigned count=2;count<=GC_COMPLETION_MAX;count*=2)for(unsigned round=0;round<12;round++) {
        REQUIRE(bench_no_game());double elapsed[6];
        for(unsigned offset=0;offset<6;offset++) {
            unsigned column=(round+offset)%6,choice=column%3,mode=choice?choice+2:0;
            gc_completion_deferred=0;
            gc_frame(mode,0,0,0);gc_frame(mode,1,0,0);
            gc_completion_deferred=column>=3;
            unsigned drains=gc_completion_drains,frames=gc_completion_frames;
            LONGLONG start=ticks();
            for(unsigned i=0;i<count;i++)gc_frame(mode,i%2,0,0);
            if(gc_completion_deferred)gc_completion_collect();
            elapsed[column]=(ticks()-start)*1000.0/qpf;
            REQUIRE(gc_completion_frames==frames+count && gc_completion_drains==drains+(gc_completion_deferred?1:count));
            REQUIRE(!gc_completion_pending && !gc_command_references);
            gc_completion_deferred=0;
        }
        printf("completion,%u,%u",count,round);
        for(unsigned i=0;i<6;i++)printf(",%.4f",elapsed[i]);
        putchar('\n');
    }
}
