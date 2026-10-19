/* Test-only completion-policy control. CPU frame/command ownership remains
 * serialized; only GPU completion may span a bounded batch. No Present. */
enum { GC_COMPLETION_MAX=8 };
static unsigned gc_completion_deferred,gc_completion_save;
static unsigned gc_completion_pending,gc_completion_mode;
static unsigned gc_completion_drains,gc_completion_frames;
static unsigned gc_completion_saved_words[GC_COMPLETION_MAX];
static unsigned gc_completion_state[GC_COMPLETION_MAX][1600];
static unsigned gc_completion_pixels[GC_COMPLETION_MAX][RT*RT];
static IDirect3DSurface9 *gc_completion_surfaces[GC_COMPLETION_MAX];
static void gc_completion_validate(void);
static void gc_completion_measure(void);

static void gc_completion_enter(unsigned mode)
{
    REQUIRE(!gc_completion_pending || mode==gc_completion_mode);
    REQUIRE(gc_completion_pending<GC_COMPLETION_MAX);
    if(!gc_completion_pending)gc_completion_mode=mode;
}

static void gc_completion_finish(const GcRequest *r)
{
    gc_completion_frames++;
    if(!gc_completion_deferred) {
        REQUIRE(!gc_completion_pending && !gc_completion_save);
        drain();gc_completion_drains++;
        if(r->check){read_pixels(gc_output);gc_state_words=gc_snapshot(gc_state);}
        return;
    }
    unsigned index=gc_completion_pending++;
    REQUIRE(index<GC_COMPLETION_MAX && gc_mode==gc_completion_mode);
    if(gc_completion_save) {
        REQUIRE(r->check && gc_completion_surfaces[index]);
        HR(IDirect3DDevice9_StretchRect(g_dev,g_rt,NULL,gc_completion_surfaces[index],NULL,D3DTEXF_NONE));
        gc_completion_saved_words[index]=gc_snapshot(gc_completion_state[index]);
    } else REQUIRE(!r->check);
}

static void gc_completion_flush(void)
{
    REQUIRE(gc_completion_deferred && gc_completion_pending && !gc_active_frame);
    drain();gc_completion_drains++;
    if(gc_completion_save)for(unsigned i=0;i<gc_completion_pending;i++) {
        D3DLOCKED_RECT lr;
        HR(IDirect3DDevice9_GetRenderTargetData(g_dev,gc_completion_surfaces[i],g_sys));
        HR(IDirect3DSurface9_LockRect(g_sys,&lr,NULL,D3DLOCK_READONLY));
        for(unsigned y=0;y<RT;y++)memcpy(gc_completion_pixels[i]+y*RT,(char *)lr.pBits+y*lr.Pitch,RT*4);
        HR(IDirect3DSurface9_UnlockRect(g_sys));
    }
    gc_completion_pending=0;
}
