/* Correctness-only device observation; preserve extended native vtables.
 * The injected preparation failure is consumed by the real transaction
 * engine and replayed as ordinary draws before this fixture accepts it. */
static pf_ptr_fn gc_first_shader_next;
static pf_dip_fn gc_first_draw_next;
static unsigned gc_first_observing,gc_first_fail_armed,gc_first_failures,gc_first_draws;
static HRESULT WINAPI gc_first_shader(void *dev,void *shader)
{
    if(gc_first_fail_armed && consumer.submit.open &&
       consumer.submit.operation==PF_SUBMIT_SHADER && shader==consumer.shader) {
        REQUIRE(GetCurrentThreadId()==consumer.owner);
        gc_first_fail_armed=0;gc_first_failures++;return E_FAIL;
    }
    return gc_first_shader_next(dev,shader);
}
static HRESULT WINAPI gc_first_draw(void *dev,unsigned type,int base,unsigned first,
                                   unsigned vertices,unsigned start,unsigned primitives)
{
    HRESULT hr=gc_first_draw_next(dev,type,base,first,vertices,start,primitives);
    if(SUCCEEDED(hr))gc_first_draws++;
    return hr;
}
static void gc_first_device_observer(unsigned enable)
{
    void **vt=PF_VT(g_dev);DWORD old,ignored;
    if(enable){gc_first_shader_next=(pf_ptr_fn)vt[PF_DEV_SET_VS];gc_first_draw_next=(pf_dip_fn)vt[PF_DEV_DRAW_INDEXED];}
    else REQUIRE(vt[PF_DEV_SET_VS]==gc_first_shader && vt[PF_DEV_DRAW_INDEXED]==gc_first_draw);
    REQUIRE(VirtualProtect(vt+PF_DEV_SET_VS,sizeof(void *),PAGE_READWRITE,&old));
    vt[PF_DEV_SET_VS]=enable?(void *)gc_first_shader:(void *)gc_first_shader_next;
    REQUIRE(VirtualProtect(vt+PF_DEV_SET_VS,sizeof(void *),old,&ignored));
    REQUIRE(VirtualProtect(vt+PF_DEV_DRAW_INDEXED,sizeof(void *),PAGE_READWRITE,&old));
    vt[PF_DEV_DRAW_INDEXED]=enable?(void *)gc_first_draw:(void *)gc_first_draw_next;
    REQUIRE(VirtualProtect(vt+PF_DEV_DRAW_INDEXED,sizeof(void *),old,&ignored));
    if(!enable)REQUIRE(vt[PF_DEV_SET_VS]==(void *)gc_first_shader_next && vt[PF_DEV_DRAW_INDEXED]==(void *)gc_first_draw_next);
    gc_first_observing=enable;
}
