/* Test-only owned device command batches. D3DX stays on the producer.
 * A recorded callback succeeds when admitted, not when the driver executes
 * it. Begin disables automatic saves; queued explicit restoration preserves
 * the fixture's flags=6 state boundary. Native driver-error propagation and
 * arbitrary assets are not qualified. */
#include <d3dx9effect.h>
#include "effect_filter_bench.h"
extern void material_fixture_command_manager(ID3DXEffectStateManager *);
extern HRESULT material_fixture_try_open_flags(unsigned *,unsigned);
extern unsigned material_fixture_restore_state(IDirect3DDevice9 *,unsigned *,unsigned);
enum { CMD_RS,CMD_TSS,CMD_SAMPLER,CMD_TEXTURE,CMD_VS,CMD_PS,CMD_VF,CMD_VI,
       CMD_VB,CMD_PF,CMD_PI,CMD_PB,CMD_TRANSFORM,CMD_MATERIAL,CMD_LIGHT,
       CMD_LIGHT_ENABLE,CMD_NPATCH,CMD_FVF,CMD_SAVE,CMD_DRAW,CMD_RESTORE };
enum { CMD_CAPACITY=2048,CMD_BYTES=262144,CMD_GROUPS=16 };
typedef struct { unsigned kind,a,b,offset,bytes;IUnknown *resource; } GcCommand;
typedef struct {
    unsigned count,bytes,groups;
    GcCommand command[CMD_CAPACITY];
    unsigned char data[CMD_BYTES];
} GcCommandBatch;
static GcCommandBatch gc_commands,gc_command_slots[2];
static LONG gc_command_references;
static unsigned gc_command_capture,gc_command_callbacks,gc_command_batches,gc_command_max_bytes;
static unsigned gc_command_verify,gc_command_restores,gc_command_audited;
static unsigned gc_command_lifetime,gc_command_lifetime_calls;
static void gc_send(GcRequest *,unsigned);

static HRESULT gc_command_emit(unsigned kind,unsigned a,unsigned b,const void *data,unsigned bytes,IUnknown *resource)
{
    REQUIRE(GetCurrentThreadId()==gc_producer);
    unsigned aligned=(bytes+3)&~3u;
    if(gc_commands.count>=(gc_fault==7?1:CMD_CAPACITY) || aligned>CMD_BYTES-gc_commands.bytes) {
        fprintf(stderr,"COMMAND CAPACITY: refused\n");return E_OUTOFMEMORY;
    }
    REQUIRE(!bytes || data);
    GcCommand *c=&gc_commands.command[gc_commands.count++];
    *c=(GcCommand){kind,a,b,gc_commands.bytes,bytes,resource};
    if(bytes)memcpy(gc_commands.data+c->offset,data,bytes);
    gc_commands.bytes+=aligned;
    if(resource){gc_command_lifetime++;IUnknown_AddRef(resource);gc_command_lifetime--;InterlockedIncrement(&gc_command_references);}
    return S_OK;
}
static void gc_command_flush(void)
{
    if(!gc_commands.count)return;
    REQUIRE(gc_commands.groups && gc_commands.groups<=CMD_GROUPS && !gc_command_capture);
    if(gc_commands.bytes>gc_command_max_bytes)gc_command_max_bytes=gc_commands.bytes;
    GcRequest r={0};r.kind=GC_COMMANDS;gc_send(&r,0);gc_command_batches++;
    memset(gc_commands.data,0xA5,gc_commands.bytes);
    memset(gc_commands.command,0xA5,gc_commands.count*sizeof(GcCommand));
    gc_commands.count=gc_commands.bytes=gc_commands.groups=0;
}
static HRESULT WINAPI cmd_qi(ID3DXEffectStateManager *s,REFIID iid,void **out)
{
    static const GUID unknown={0,0,0,{0xc0,0,0,0,0,0,0,0x46}};
    static const GUID manager={0x79aab587,0x6dbc,0x4fa7,{0x82,0xde,0x37,0xfa,0x17,0x81,0xc5,0xce}};
    if(!out)return E_POINTER;
    *out=NULL;
    if(memcmp(iid,&unknown,sizeof unknown) && memcmp(iid,&manager,sizeof manager))return E_NOINTERFACE;
    *out=s;return S_OK;
}
static ULONG WINAPI cmd_ref(ID3DXEffectStateManager *s){(void)s;return 1;}
#define CMD_EMIT(k,a,b,p,n,o) do { (void)s;REQUIRE(gc_command_capture && GetCurrentThreadId()==gc_producer);gc_command_callbacks++;return gc_command_emit(k,a,b,p,n,(IUnknown *)(o)); } while(0)
static HRESULT WINAPI cmd_rs(ID3DXEffectStateManager *s,D3DRENDERSTATETYPE a,DWORD b){CMD_EMIT(CMD_RS,a,b,NULL,0,NULL);}
static HRESULT WINAPI cmd_tss(ID3DXEffectStateManager *s,DWORD a,D3DTEXTURESTAGESTATETYPE b,DWORD v){CMD_EMIT(CMD_TSS,a,b,&v,4,NULL);}
static HRESULT WINAPI cmd_sampler(ID3DXEffectStateManager *s,DWORD a,D3DSAMPLERSTATETYPE b,DWORD v){CMD_EMIT(CMD_SAMPLER,a,b,&v,4,NULL);}
static HRESULT WINAPI cmd_texture(ID3DXEffectStateManager *s,DWORD a,IDirect3DBaseTexture9 *v){CMD_EMIT(CMD_TEXTURE,a,0,NULL,0,v);}
static HRESULT WINAPI cmd_vs(ID3DXEffectStateManager *s,IDirect3DVertexShader9 *v){CMD_EMIT(CMD_VS,0,0,NULL,0,v);}
static HRESULT WINAPI cmd_ps(ID3DXEffectStateManager *s,IDirect3DPixelShader9 *v){CMD_EMIT(CMD_PS,0,0,NULL,0,v);}
#define CMD_CONSTANT(name,kind,type,width,limit) \
static HRESULT WINAPI name(ID3DXEffectStateManager *s,UINT a,const type *v,UINT n) { \
    if(a>limit || n>limit-a){return D3DERR_INVALIDCALL;} \
    CMD_EMIT(kind,a,n,v,n*width,NULL); }
CMD_CONSTANT(cmd_vf,CMD_VF,FLOAT,16,256)
CMD_CONSTANT(cmd_vi,CMD_VI,INT,16,16)
CMD_CONSTANT(cmd_vb,CMD_VB,WINBOOL,4,16)
CMD_CONSTANT(cmd_pf,CMD_PF,FLOAT,16,224)
CMD_CONSTANT(cmd_pi,CMD_PI,INT,16,16)
CMD_CONSTANT(cmd_pb,CMD_PB,WINBOOL,4,16)
static HRESULT WINAPI cmd_transform(ID3DXEffectStateManager *s,D3DTRANSFORMSTATETYPE a,const D3DMATRIX *v){CMD_EMIT(CMD_TRANSFORM,a,0,v,sizeof *v,NULL);}
static HRESULT WINAPI cmd_material(ID3DXEffectStateManager *s,const D3DMATERIAL9 *v){CMD_EMIT(CMD_MATERIAL,0,0,v,sizeof *v,NULL);}
static HRESULT WINAPI cmd_light(ID3DXEffectStateManager *s,DWORD a,const D3DLIGHT9 *v){CMD_EMIT(CMD_LIGHT,a,0,v,sizeof *v,NULL);}
static HRESULT WINAPI cmd_light_enable(ID3DXEffectStateManager *s,DWORD a,WINBOOL b){CMD_EMIT(CMD_LIGHT_ENABLE,a,b,NULL,0,NULL);}
static HRESULT WINAPI cmd_npatch(ID3DXEffectStateManager *s,FLOAT v){CMD_EMIT(CMD_NPATCH,0,0,&v,4,NULL);}
static HRESULT WINAPI cmd_fvf(ID3DXEffectStateManager *s,DWORD a){CMD_EMIT(CMD_FVF,a,0,NULL,0,NULL);}
static ID3DXEffectStateManagerVtbl gc_command_vt={cmd_qi,cmd_ref,cmd_ref,cmd_transform,cmd_material,cmd_light,
    cmd_light_enable,cmd_rs,cmd_texture,cmd_tss,cmd_sampler,cmd_npatch,cmd_fvf,cmd_vs,cmd_vf,cmd_vi,cmd_vb,cmd_ps,cmd_pf,cmd_pi,cmd_pb};
static ID3DXEffectStateManager gc_command_manager={&gc_command_vt};
#undef CMD_EMIT
#undef CMD_CONSTANT

static void gc_command_replay(const GcCommandBatch *batch)
{
    unsigned before[272],after[272],words=0;
    REQUIRE(batch->count<=CMD_CAPACITY && batch->bytes<=CMD_BYTES && batch->groups<=CMD_GROUPS);
    for(unsigned i=0;i<batch->count;i++) {
        const GcCommand *c=&batch->command[i];
        REQUIRE(c->offset<=batch->bytes && c->bytes<=batch->bytes-c->offset);
        const void *v=batch->data+c->offset;unsigned a=c->a,b=c->b;HRESULT hr=S_OK;
        switch(c->kind) {
        case CMD_RS:hr=IDirect3DDevice9_SetRenderState(g_dev,a,b);break;
        case CMD_TSS:hr=IDirect3DDevice9_SetTextureStageState(g_dev,a,b,*(const DWORD *)v);break;
        case CMD_SAMPLER:hr=IDirect3DDevice9_SetSamplerState(g_dev,a,b,*(const DWORD *)v);break;
        case CMD_TEXTURE:hr=IDirect3DDevice9_SetTexture(g_dev,a,(IDirect3DBaseTexture9 *)c->resource);break;
        case CMD_VS:hr=IDirect3DDevice9_SetVertexShader(g_dev,(IDirect3DVertexShader9 *)c->resource);break;
        case CMD_PS:hr=IDirect3DDevice9_SetPixelShader(g_dev,(IDirect3DPixelShader9 *)c->resource);break;
        case CMD_VF:hr=IDirect3DDevice9_SetVertexShaderConstantF(g_dev,a,v,b);break;
        case CMD_VI:hr=IDirect3DDevice9_SetVertexShaderConstantI(g_dev,a,v,b);break;
        case CMD_VB:hr=IDirect3DDevice9_SetVertexShaderConstantB(g_dev,a,v,b);break;
        case CMD_PF:hr=IDirect3DDevice9_SetPixelShaderConstantF(g_dev,a,v,b);break;
        case CMD_PI:hr=IDirect3DDevice9_SetPixelShaderConstantI(g_dev,a,v,b);break;
        case CMD_PB:hr=IDirect3DDevice9_SetPixelShaderConstantB(g_dev,a,v,b);break;
        case CMD_TRANSFORM:hr=IDirect3DDevice9_SetTransform(g_dev,a,v);break;
        case CMD_MATERIAL:hr=IDirect3DDevice9_SetMaterial(g_dev,v);break;
        case CMD_LIGHT:hr=IDirect3DDevice9_SetLight(g_dev,a,v);break;
        case CMD_LIGHT_ENABLE:hr=IDirect3DDevice9_LightEnable(g_dev,a,b);break;
        case CMD_NPATCH:hr=IDirect3DDevice9_SetNPatchMode(g_dev,*(const float *)v);break;
        case CMD_FVF:hr=IDirect3DDevice9_SetFVF(g_dev,a);break;
        case CMD_SAVE:
            if(gc_command_verify)words=material_fixture_restore_state(g_dev,before,272);
            hr=bench_filter_begin();break;
        case CMD_DRAW: {
            IDirect3DVertexShader9 *shader=NULL;
            HR(IDirect3DDevice9_GetVertexShader(g_dev,&shader));REQUIRE(shader);
            REQUIRE(c->bytes==a*BONES*8*sizeof(float));
            REQUIRE(b<4);
            packet_consumer_group_submit(v,a,b&1,shader,b>>1);IDirect3DVertexShader9_Release(shader);break;
        }
        case CMD_RESTORE:
            if(gc_fault!=6)hr=bench_filter_end();
            if(gc_command_verify) {
                unsigned n=material_fixture_restore_state(g_dev,after,272);
                for(unsigned j=0;j<words && j<n;j++)if(before[j]!=after[j])
                    fprintf(stderr,"COMMAND RESTORE: word=%u before=%08X after=%08X mode=%u\n",j,before[j],after[j],gc_mode);
                REQUIRE(words==n && !memcmp(before,after,n*4));gc_command_restores++;
            }
            break;
        default:REQUIRE(!"unknown device command");
        }
        HR(hr);
        if(c->resource){IUnknown_Release(c->resource);InterlockedDecrement(&gc_command_references);}
    }
}

/* Audit all 119 device entries plus state-block Capture/Apply during
 * correctness only. General registers
 * and flags are restored before the original call; no FP control changes. */
void *gc_device_original[121];
static void *gc_device_vtable,*gc_device_thunks;
static void **gc_stateblock_vtable;
void gc_device_note(unsigned slot)
{
    if(GetCurrentThreadId()==gc_producer && gc_command_capture) {
        /* Native D3D9 resource references can also retain their device.
         * Permit only IUnknown lifetime calls inside explicitly scoped
         * source assignment or our queued-resource AddRef. State methods
         * and unscoped lifetime calls still fail this audit. */
        if((slot==1 || slot==2) && gc_command_lifetime) {
            gc_command_lifetime_calls++;return;
        }
        fprintf(stderr,"COMMAND DEVICE AUDIT: forbidden producer call %u\n",slot);
        REQUIRE(!gc_command_capture);
    }
}
void gc_device_bridge(void);
__asm__(".text\n.globl _gc_device_bridge\n_gc_device_bridge:\n"
        "pushfl\npushal\npushl 36(%esp)\ncall _gc_device_note\naddl $4,%esp\n"
        "popal\npopfl\npushl %eax\nmovl 4(%esp),%eax\n"
        "movl _gc_device_original(,%eax,4),%eax\nxchgl %eax,(%esp)\nret $4\n");
static void gc_command_audit(unsigned enable)
{
    DWORD old;
    REQUIRE(sizeof(IDirect3DDevice9Vtbl)==119*sizeof(void *));
    if(enable) {
        gc_device_vtable=*(void **)g_dev;memcpy(gc_device_original,gc_device_vtable,119*sizeof(void *));
        gc_stateblock_vtable=*(void ***)outer;
        gc_device_original[119]=gc_stateblock_vtable[4];gc_device_original[120]=gc_stateblock_vtable[5];
        gc_device_thunks=VirtualAlloc(NULL,121*16,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);REQUIRE(gc_device_thunks);
        for(unsigned i=0;i<121;i++) {
            unsigned char *p=(unsigned char *)gc_device_thunks+i*16;
            p[0]=0x68;memcpy(p+1,&i,4);p[5]=0xe9;
            unsigned relative=(unsigned)gc_device_bridge-(unsigned)(p+10);memcpy(p+6,&relative,4);
        }
        REQUIRE(VirtualProtect(gc_device_thunks,121*16,PAGE_EXECUTE_READ,&old));
        REQUIRE(FlushInstructionCache(GetCurrentProcess(),gc_device_thunks,121*16));
    }
    /* The state-block implementation's shared table covers cached blocks
     * created by D3DX before this audit starts, not just our outer block. */
    REQUIRE(VirtualProtect(gc_stateblock_vtable+4,2*sizeof(void *),PAGE_READWRITE,&old));
    for(unsigned i=0;i<2;i++)gc_stateblock_vtable[4+i]=enable?(unsigned char *)gc_device_thunks+(119+i)*16:gc_device_original[119+i];
    DWORD ignored;REQUIRE(VirtualProtect(gc_stateblock_vtable+4,2*sizeof(void *),old,&ignored));
    /* Native D3D9 calls private entries beyond its public 119-slot ABI
     * (the observed driver uses slot 134). Preserve that extended table. */
    REQUIRE(VirtualProtect(gc_device_vtable,119*sizeof(void *),PAGE_READWRITE,&old));
    for(unsigned i=0;i<119;i++)((void **)gc_device_vtable)[i]=enable?(unsigned char *)gc_device_thunks+i*16:gc_device_original[i];
    REQUIRE(VirtualProtect(gc_device_vtable,119*sizeof(void *),old,&ignored));
    if(!enable) {
        REQUIRE(*(void **)g_dev==gc_device_vtable && !memcmp(gc_device_vtable,gc_device_original,119*sizeof(void *)) &&
                gc_stateblock_vtable[4]==gc_device_original[119] &&
                gc_stateblock_vtable[5]==gc_device_original[120]);
        /* Leave the tiny unreachable thunk allocation until process exit:
         * a driver-owned background call could still be finishing a thunk. */
    }
}
