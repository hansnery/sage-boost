/* Offline checked-submission candidate. No runtime hook or installed ASI
 * uses this file. Rigid recovery and persistent/device-loss failures remain
 * unqualified. Bone snapshots keep effect parameter state untouched. */
#define PF_BENCH_INST_SOURCE "../build/inst_recovery_base.c"
#define PF_NATIVE_LEADER_EXTRA_GATE g.skinned
static void recovery_capture_begin(void);
#define PF_NATIVE_LEADER_CAPTURE_BEGIN recovery_capture_begin
#include "inst_native_leader_engine.c"

static unsigned recovery_bones[MAX_REGS * 4], recovery_valid;
static const void *recovery_model, *recovery_method, *recovery_vs;
static unsigned recovery_op, recovery_open;
static unsigned recovery_break;
static void (*recovery_begin)(void), (*recovery_end)(void);
static void recovery_capture_begin(void) { recovery_valid=0; }

void bench_recovery_watch(void (*begin)(void), void (*end)(void))
{ recovery_begin=begin; recovery_end=end; }
unsigned bench_recovery_operation(void) { return recovery_op; }
unsigned bench_recovery_open(void) { return recovery_open; }
void bench_recovery_break(unsigned mode) { recovery_break=mode; }

static int stage(const unsigned char *mesh)
{
    if (leader_enabled && g.skinned && g.mode==M_LEADER && !g.count) {
        const pf_inst_variant *v=&pf_inst_variants[g.variant];
        recovery_valid=0;
        if (v->roles[PF_ROLE_BONES]<0 || !g_can_read_consts ||
            g.stride!=g.nbones*2 || g.stride>MAX_REGS)
            return 0;
        recovery_op=15; /* Before any leader is suppressed. */
        HRESULT hr=((pf_const_f_fn)PF_VT(g.dev)[PF_DEV_GET_VS_CONST_F])(
            g.dev,(unsigned)v->roles[PF_ROLE_BONES],recovery_bones,g.stride);
        recovery_op=0;
        if (FAILED(hr)) {
            fail_session("recovery palette capture failed",(unsigned)hr);
            return 0;
        }
        recovery_model=g.model; recovery_method=g.method; recovery_vs=g.ea_vs;
        recovery_valid=1;
    }
    return unchecked_stage(mesh);
}

static int draw_batch(int counted)
{
    if (!leader_enabled || !recovery_valid || !g.skinned ||
        g.model!=recovery_model || g.method!=recovery_method || g.ea_vs!=recovery_vs)
        return unchecked_draw_batch(counted);
    unsigned n=g.count, regs=n*g.stride;
    if (!n) return 1;
    void *dev=g.dev;
    void **vt=PF_VT(dev);
    const pf_inst_variant *v=&pf_inst_variants[g.variant];
    unsigned inst=g.decl->inst_stream;
    pf_set_freq_fn freq=(pf_set_freq_fn)vt[PF_DEV_SET_STREAM_FREQ];
    pf_const_f_fn constants=(pf_const_f_fn)vt[PF_DEV_SET_VS_CONST_F];
    pf_ptr_fn shader=(pf_ptr_fn)vt[PF_DEV_SET_VS],decl=(pf_ptr_fn)vt[PF_DEV_SET_DECL];
    pf_set_stream_fn stream=(pf_set_stream_fn)vt[PF_DEV_SET_STREAM];
    pf_dip_fn draw=(pf_dip_fn)vt[PF_DEV_DRAW_INDEXED];
    unsigned saved=0,committed=0,failed=0,coherent=1;
    int timed=counted && g.shadow && !g_extra;
    LONGLONG t0=timed?qpc():0;
    HRESULT hr=S_OK,first_error=S_OK;
    g.count=0; /* The local n owns every staged unit until resolved. */
    recovery_open=1;
    if (recovery_begin) recovery_begin();
#define PREPARE(id,expr) do { recovery_op=(id); hr=(expr); \
    if (FAILED(hr)) { failed=1; first_error=hr; goto restore; } } while(0)
    PREPARE(1,((pf_const_f_fn)vt[PF_DEV_GET_VS_CONST_F])(dev,v->ir_reg,g_saved,regs));
    saved=1;
    PREPARE(2,shader(dev,g_ours[g.variant]));
    PREPARE(3,decl(dev,g.decl->ours));
    PREPARE(4,stream(dev,inst,g_ivb,g_ivb_off[g.stride]*4,4));
    for (unsigned s=0;s<inst;s++) if (g.decl->streams>>s&1)
        PREPARE(5,freq(dev,s,PF_STREAM_INDEXED|n));
    PREPARE(6,freq(dev,inst,PF_STREAM_INSTANCE|1));
    PREPARE(7,constants(dev,v->ir_reg,g_stage,regs));
    PREPARE(8,draw(dev,g.dip[0],(INT)g.dip[1],g.dip[2],g.dip[3],g.dip[4],g.dip[5]));
    committed=1;
restore:
#undef PREPARE
    /* State writes are idempotent. Retry a rejected restoration once; do
     * not retry a draw that may already have succeeded. Persistent errors
     * leave coherent false, forbid replay and fail this offline candidate. */
#define RESTORE(id,expr) do { recovery_op=(id); hr=(expr); \
    if (FAILED(hr)) { if (!failed) first_error=hr; failed=1; \
        hr=(expr); if (FAILED(hr)) coherent=0; } } while(0)
    for (unsigned s=0;s<=inst;s++) if (g.decl->streams>>s&1 || s==inst)
        RESTORE(s==inst?10:9,freq(dev,s,1));
    /* Native D3D9 retains offset/stride when unbinding a null stream. Set
     * the original values through a live buffer first, then unbind it. */
    if (!g.prev_vb && recovery_break!=4)
        RESTORE(19,stream(dev,inst,g_ivb,g.prev_off,g.prev_stride));
    RESTORE(11,stream(dev,inst,g.prev_vb,g.prev_off,g.prev_stride));
    RESTORE(12,decl(dev,g.decl->game));
    RESTORE(13,shader(dev,g.ea_vs));
    if (saved) RESTORE(14,constants(dev,v->ir_reg,g_saved,regs));
    /* Deliberately broken behavior for independent-oracle controls only. */
    if (recovery_break==3 && failed && committed) committed=0;
    if (!committed && coherent && recovery_break!=2) {
        /* Original shader + each staged bone palette reproduces the draw
         * without mutating D3DX source values or replaying native callbacks. */
        for (unsigned i=0;i<n;i++) {
            recovery_op=16;
            hr=constants(dev,(unsigned)v->roles[PF_ROLE_BONES],g_stage+i*g.stride*4,g.stride);
            if (FAILED(hr)) { first_error=hr; coherent=0; break; }
            recovery_op=17;
            hr=draw(dev,g.dip[0],(INT)g.dip[1],g.dip[2],g.dip[3],g.dip[4],g.dip[5]);
            if (FAILED(hr)) { first_error=hr; coherent=0; break; }
        }
        RESTORE(18,constants(dev,(unsigned)v->roles[PF_ROLE_BONES],recovery_bones,g.stride));
    }
#undef RESTORE
    recovery_op=0;
    if (recovery_end) recovery_end();
    recovery_open=0;
    if (failed || !coherent)
        fail_session("checked batch rejected an operation",(unsigned)first_error);
    if (committed && counted) {
        add(REASON(meshes),n); add(REASON(draws),1);
        if (g.bonelist) add(REASON(bonelist),n);
        if (!g_extra) g_cat_meshes[g.cat]+=n;
    }
    if (timed) g_shadow_ticks+=qpc()-t0;
    return coherent;
}
