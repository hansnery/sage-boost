/* Offline experiment only. Snapshots borrow the fixture's stable meshes,
 * methods and declaration-cache entries; this is NOT a runtime cache. */
#define COBJMACROS
#include <d3d9.h>
#include "inst_dispatch_engine.c"

#define RETAINED_GROUPS 288
static group retained[RETAINED_GROUPS];
static IDirect3DStateBlock9 *blocks[RETAINED_GROUPS];
static int narrow;
static unsigned replay_draws, replay_meshes;
static HRESULT (*record_material_mask)(IDirect3DDevice9 *);
static void (*replay_marker_begin)(void),(*replay_marker_end)(void);
static int (*replay_bulk_begin)(void),(*replay_bulk_end)(unsigned);

void bench_retained_bulk_markers(int (*begin)(void),int (*end)(unsigned))
{
    replay_bulk_begin=begin;
    replay_bulk_end=end;
}

void bench_retained_markers(void (*begin)(void),void (*end)(void))
{
    replay_marker_begin=begin;
    replay_marker_end=end;
}

void bench_retained_material_mask(HRESULT (*record)(IDirect3DDevice9 *))
{
    record_material_mask = record;
}

void bench_retained_kind(int value) { narrow = value; }
void bench_retained_counts(unsigned *draws, unsigned *meshes)
{
    *draws=replay_draws; *meshes=replay_meshes;
    replay_draws=replay_meshes=0;
}

/* Fixture-specific state mask, NOT an inferred mask for an arbitrary game
 * material. It includes every state the synthetic workload changes. */
static HRESULT create_narrow(IDirect3DDevice9 *dev, IDirect3DStateBlock9 **out)
{
    IDirect3DVertexShader9 *vs=0;
    IDirect3DPixelShader9 *ps=0;
    IDirect3DVertexDeclaration9 *decl=0;
    IDirect3DVertexBuffer9 *vb[2]={0};
    IDirect3DIndexBuffer9 *ib=0;
    float vc[256*4], pc[32*4];
    const pf_inst_variant *variant=&pf_inst_variants[g.variant];
    unsigned bone_first=(unsigned)variant->roles[PF_ROLE_BONES];
    unsigned bone_end=bone_first + 2*variant->ea_bones;
    UINT offset[2], stride[2], frequency[2];
    static const D3DRENDERSTATETYPE states[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,
        D3DRS_ZFUNC,D3DRS_CULLMODE,D3DRS_COLORWRITEENABLE,D3DRS_ALPHABLENDENABLE,
        D3DRS_ALPHATESTENABLE};
    DWORD values[sizeof states/sizeof states[0]];
    HRESULT hr = E_FAIL;
    unsigned i;
    if (!g.skinned || variant->roles[PF_ROLE_BONES] < 0 || bone_end > 256 ||
        g.decl->inst_stream != 1 || g.decl->streams != 1)
        return E_FAIL;
#define GET(call) do { if(FAILED(hr=(call))) goto done; } while(0)
    GET(IDirect3DDevice9_GetVertexShader(dev,&vs));
    GET(IDirect3DDevice9_GetPixelShader(dev,&ps));
    GET(IDirect3DDevice9_GetVertexDeclaration(dev,&decl));
    GET(IDirect3DDevice9_GetIndices(dev,&ib));
    GET(IDirect3DDevice9_GetVertexShaderConstantF(dev,0,vc,256));
    GET(IDirect3DDevice9_GetPixelShaderConstantF(dev,0,pc,32));
    for(i=0;i<2;i++) {
        GET(IDirect3DDevice9_GetStreamSource(dev,i,&vb[i],&offset[i],&stride[i]));
        GET(IDirect3DDevice9_GetStreamSourceFreq(dev,i,&frequency[i]));
    }
    for(i=0;i<sizeof states/sizeof states[0];i++)
        GET(IDirect3DDevice9_GetRenderState(dev,states[i],&values[i]));
    GET(IDirect3DDevice9_BeginStateBlock(dev));
    /* These valid setters are recording only. End even after a failure. */
#define SET(call) do { HRESULT s=(call); if(FAILED(s)) hr=s; } while(0)
    SET(IDirect3DDevice9_SetVertexShader(dev,vs));
    SET(IDirect3DDevice9_SetPixelShader(dev,ps));
    SET(IDirect3DDevice9_SetVertexDeclaration(dev,decl));
    SET(IDirect3DDevice9_SetIndices(dev,ib));
    /* This fixture uses quaternion skinned vs_2_0 and ps_2_0. Every bone
     * is uploaded afresh; only non-palette vertex registers are retained. */
    if(bone_first) SET(IDirect3DDevice9_SetVertexShaderConstantF(dev,0,vc,bone_first));
    if(bone_end<256) SET(IDirect3DDevice9_SetVertexShaderConstantF(dev,bone_end,vc+bone_end*4,256-bone_end));
    SET(IDirect3DDevice9_SetPixelShaderConstantF(dev,0,pc,32));
    for(i=0;i<2;i++) {
        SET(IDirect3DDevice9_SetStreamSource(dev,i,vb[i],offset[i],stride[i]));
        SET(IDirect3DDevice9_SetStreamSourceFreq(dev,i,frequency[i]));
    }
    for(i=0;i<sizeof states/sizeof states[0];i++)
        SET(IDirect3DDevice9_SetRenderState(dev,states[i],values[i]));
    if(record_material_mask) SET(record_material_mask(dev));
    SET(IDirect3DDevice9_EndStateBlock(dev,out));
done:
    if(vs) IDirect3DVertexShader9_Release(vs);
    if(ps) IDirect3DPixelShader9_Release(ps);
    if(decl) IDirect3DVertexDeclaration9_Release(decl);
    if(ib) IDirect3DIndexBuffer9_Release(ib);
    for(i=0;i<2;i++) if(vb[i]) IDirect3DVertexBuffer9_Release(vb[i]);
    return hr;
#undef GET
#undef SET
}

int bench_retain(unsigned index)
{
    HRESULT hr=S_OK;
    if (index >= RETAINED_GROUPS || g.mode != M_LEADER || g.dips != 1 ||
        g.capture_reason || !g.dev || !g.decl || !g.cap)
        return 0;
    if (!blocks[index])
        hr = narrow ? create_narrow((IDirect3DDevice9 *)g.dev,&blocks[index]) :
             IDirect3DDevice9_CreateStateBlock((IDirect3DDevice9 *)g.dev,
                                              D3DSBT_ALL, &blocks[index]);
    if (FAILED(hr)) return 0;
    if (FAILED(IDirect3DStateBlock9_Capture(blocks[index]))) return 0;
    retained[index] = g;
    return 1;
}

/* The fixture owns this entire replay region and restores its entry state
 * once at the end. No game draw is allowed between these chunks. */
static int retained_draw(void)
{
    const pf_inst_variant *v=&pf_inst_variants[g.variant];
    IDirect3DDevice9 *dev=(IDirect3DDevice9 *)g.dev;
    unsigned n=g.count, stream;
    g.count=0;
    if(!n) return 1;
    for(stream=0;stream<g.decl->inst_stream;stream++)
        if(g.decl->streams>>stream&1)
            if(FAILED(IDirect3DDevice9_SetStreamSourceFreq(dev,stream,PF_STREAM_INDEXED|n)))
                return 0;
    if(FAILED(IDirect3DDevice9_SetVertexShaderConstantF(dev,v->ir_reg,
              (const float *)g_stage,n*g.stride)) ||
       FAILED(IDirect3DDevice9_DrawIndexedPrimitive(dev,g.dip[0],(INT)g.dip[1],
              g.dip[2],g.dip[3],g.dip[4],g.dip[5]))) return 0;
    replay_draws++; replay_meshes+=n;
    return 1;
}

int bench_replay(unsigned index, const unsigned char *first, const unsigned char *end)
{
    unsigned attempted=0;
    if (index >= RETAINED_GROUPS || !blocks[index] || first >= end ||
        (end - first) % PF_RECORD_SIZE || g_failed)
        return 0;
    g = retained[index];
    g.mode = M_INST;
    g.armed = 0;
    g.count = 0;
    if (FAILED(IDirect3DStateBlock9_Apply(blocks[index]))) return 0;
    {
        IDirect3DDevice9 *dev=(IDirect3DDevice9 *)g.dev;
        unsigned stream=g.decl->inst_stream;
        if(FAILED(IDirect3DDevice9_SetVertexShader(dev,g_ours[g.variant])) ||
           FAILED(IDirect3DDevice9_SetVertexDeclaration(dev,g.decl->ours)) ||
           FAILED(IDirect3DDevice9_SetStreamSource(dev,stream,g_ivb,g_ivb_off[g.stride]*4,4)) ||
           FAILED(IDirect3DDevice9_SetStreamSourceFreq(dev,stream,PF_STREAM_INSTANCE|1))) return 0;
    }
    if(replay_bulk_begin && !replay_bulk_begin()) return 0;
    for (; first < end; first += PF_RECORD_SIZE) {
        const unsigned char *mesh = *(const unsigned char *const *)first;
        int valid;
        if(replay_marker_begin) replay_marker_begin();
        valid=mesh && *(const void *const *)(mesh + PF_MESH_MODEL)==g.model &&
              *(const unsigned *)(mesh + PF_MESH_BASE_VERTEX)==g.base_vertex && stage(mesh);
        if(valid && (g.count==g.cap || first+PF_RECORD_SIZE==end)) valid=retained_draw();
        if(replay_marker_end) replay_marker_end();
        attempted++;
        if(!valid) {
            if(replay_bulk_end) replay_bulk_end(attempted);
            return 0;
        }
    }
    if(replay_bulk_end && !replay_bulk_end(attempted)) return 0;
    if (!retained_draw()) return 0;
    g.mode = M_IDLE;
    return 1;
}

void bench_retained_clear(void)
{
    unsigned i;
    for (i = 0; i < RETAINED_GROUPS; i++) {
        if (blocks[i]) IDirect3DStateBlock9_Release(blocks[i]);
        blocks[i] = 0;
    }
}
