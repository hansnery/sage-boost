/* Owned GPU consumer, independent of timing.c/inst.c/fps.c globals.
 * The fixture supplies material effects and immutable resource identities.
 * Actual draws use the same checked transaction engine as the plugin. */
#include "instsubmit.h"
#include "declcache.h"

static struct {
    DWORD owner;
    IDirect3DVertexShader9 *shader;
    IDirect3DVertexBuffer9 *instances;
    pf_decl_cache declarations;
    pf_decl_entry *declaration;
    pf_submit submit;
    unsigned capacity,frames[2],groups[2],meshes[2],draws[2];
} consumer;

static unsigned consumer_integer_bits(unsigned value)
{
    if(!value)return 0;
    unsigned exponent=0;
    while(value>>(exponent+1))exponent++;
    return ((127+exponent)<<23)|((value<<(23-exponent))&0x7fffff);
}

static void packet_consumer_init(void)
{
    REQUIRE(!consumer.owner && !consumer.shader && !consumer.instances);
    consumer.owner=GetCurrentThreadId();
    consumer.capacity=g_v->ir_count/(BONES*2);
    REQUIRE(consumer.capacity>=2 && consumer.capacity<=128);
    HR(IDirect3DDevice9_CreateVertexShader(g_dev,(const DWORD *)g_v->code,&consumer.shader));
    HR(IDirect3DDevice9_CreateVertexBuffer(g_dev,consumer.capacity*4,D3DUSAGE_WRITEONLY,0,
        D3DPOOL_MANAGED,&consumer.instances,NULL));
    unsigned *values=NULL;
    HR(IDirect3DVertexBuffer9_Lock(consumer.instances,0,0,(void **)&values,0));
    for(unsigned i=0;i<consumer.capacity;i++)values[i]=consumer_integer_bits(i*BONES*2);
    HR(IDirect3DVertexBuffer9_Unlock(consumer.instances));
    consumer.declaration=pf_decl_cache_get(&consumer.declarations,g_dev,g_decl);
    REQUIRE(consumer.declaration && consumer.declaration->inst_stream==1);
}

static void packet_consumer_close(void)
{
    REQUIRE(consumer.owner==GetCurrentThreadId() && !consumer.submit.open);
    pf_decl_cache_clear(&consumer.declarations);consumer.declaration=NULL;
    IDirect3DVertexBuffer9_Release(consumer.instances);consumer.instances=NULL;
    IDirect3DVertexShader9_Release(consumer.shader);consumer.shader=NULL;
    consumer.submit.valid=0;consumer.owner=0;
}

static void packet_consumer_draw(const float *palettes,unsigned extra,unsigned *pixels)
{
    REQUIRE(consumer.owner==GetCurrentThreadId() && extra<2);
    HR(IDirect3DDevice9_SetVertexShader(g_dev,g_ea));
    HR(IDirect3DDevice9_SetVertexDeclaration(g_dev,g_decl));
    HR(IDirect3DDevice9_SetStreamSource(g_dev,0,g_vb,0,40));
    HR(IDirect3DDevice9_SetIndices(g_dev,g_ib));
    HR(IDirect3DDevice9_BeginScene(g_dev));
    HR(IDirect3DDevice9_Clear(g_dev,0,NULL,D3DCLEAR_TARGET|(shadow_mode?D3DCLEAR_ZBUFFER:0),
        shadow_mode?0xffffffffu:0,1,0));
    if(extra)HR(IDirect3DStateBlock9_Capture(outer));
    for(unsigned group=0;group<GROUPS;group++) {
        material_fixture_begin(group);
        if(!group && consumer_probe_pending) {
            if(consumer_shared_extra)bench_extra_mode(1);
            REQUIRE(SetEvent(consumer_inside));
            REQUIRE(WaitForSingleObject(consumer_continue,10000)==WAIT_OBJECT_0);
        }
        unsigned *bones=(unsigned *)(palettes+group*GROUP_SIZE*BONES*8);
        material_fixture_bones(bones,BONES*32);
        pf_submit_desc descriptor={0};
        descriptor.dev=g_dev;descriptor.game_vs=g_ea;descriptor.our_vs=consumer.shader;
        descriptor.game_decl=g_decl;descriptor.our_decl=consumer.declaration->ours;
        descriptor.instance_vb=consumer.instances;descriptor.instance_stream=consumer.declaration->inst_stream;
        descriptor.input_streams=consumer.declaration->streams;
        descriptor.instance_register=g_v->ir_reg;descriptor.instance_registers=g_v->ir_count;
        descriptor.bone_register=g_v->roles[PF_ROLE_BONES];descriptor.bone_registers=BONES*2;
        descriptor.dip[0]=D3DPT_TRIANGLELIST;descriptor.dip[3]=(GRID+1)*(GRID+1);
        descriptor.dip[5]=GRID*GRID*2;
        HR(IDirect3DDevice9_GetStreamSource(g_dev,descriptor.instance_stream,
            (IDirect3DVertexBuffer9 **)&descriptor.previous_vb,&descriptor.previous_offset,&descriptor.previous_stride));
        HR(pf_submit_capture(&consumer.submit,&descriptor));
        /* Preserve the original first-unit draw and follower ordering. */
        pf_submit_result result=pf_submit_pending(&consumer.submit,bones,1);
        REQUIRE(SUCCEEDED(result.error) && result.complete && result.coherent && result.replayed==1);
        consumer.draws[extra]++;
        for(unsigned first=1;first<GROUP_SIZE;) {
            unsigned count=GROUP_SIZE-first;
            if(count>consumer.capacity)count=consumer.capacity;
            result=pf_submit_batch(&consumer.submit,bones+first*BONES*8,count);
            REQUIRE(SUCCEEDED(result.error) && result.complete && result.coherent && result.committed==count);
            consumer.draws[extra]++;first+=count;
        }
        if(descriptor.previous_vb)IDirect3DVertexBuffer9_Release((IDirect3DVertexBuffer9 *)descriptor.previous_vb);
        material_fixture_end();consumer.groups[extra]++;consumer.meshes[extra]+=GROUP_SIZE;
    }
    if(extra)HR(IDirect3DStateBlock9_Apply(outer));
    HR(IDirect3DDevice9_EndScene(g_dev));consumer.frames[extra]++;
    check_state();
    if(pixels)read_pixels(pixels);
}
