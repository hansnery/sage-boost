/* Offline native-group adapter to the production checked submission object.
 * Eligibility and bridge policy remain here until runtime integration. */
#include "../src/instsubmit.h"
#define PF_BENCH_INST_SOURCE "../build/inst_recovery_base.c"
#define PF_NATIVE_LEADER_EXTRA_GATE g.skinned
static void recovery_capture_begin(void);
static int recovery_cancel_pending(void);
#define PF_NATIVE_LEADER_CAPTURE_BEGIN recovery_capture_begin
#define PF_NATIVE_LEADER_CANCEL recovery_cancel_pending
#include "inst_native_leader_engine.c"

static pf_submit recovery;
static const void *recovery_model, *recovery_method, *recovery_vs;
static void (*recovery_begin)(void), (*recovery_end)(void);
static void recovery_capture_begin(void) { recovery.valid=0; }

void bench_recovery_watch(void (*begin)(void), void (*end)(void))
{ recovery_begin=begin; recovery_end=end; }
unsigned bench_recovery_operation(void) { return recovery.operation; }
unsigned bench_recovery_open(void) { return recovery.open; }

static int recovery_matches(void)
{
    return recovery.valid && g.skinned && g.model==recovery_model &&
           g.method==recovery_method && g.ea_vs==recovery_vs;
}

static int stage(const unsigned char *mesh)
{
#ifdef PF_NATIVE_STAGE_REJECT
    if (g.mode==M_INST && PF_NATIVE_STAGE_REJECT(mesh)) return 0;
#endif
    if (leader_enabled && g.skinned && g.mode==M_LEADER && !g.count) {
        const pf_inst_variant *v=&pf_inst_variants[g.variant];
        recovery.valid=0;
        if (v->roles[PF_ROLE_BONES]<0 || !g_can_read_consts ||
            g.stride!=g.nbones*2 || g.stride>MAX_REGS)
            return 0;
        pf_submit_desc d={
            .dev=g.dev,.game_vs=g.ea_vs,.our_vs=g_ours[g.variant],
            .game_decl=g.decl->game,.our_decl=g.decl->ours,
            .instance_vb=g_ivb,.previous_vb=g.prev_vb,
            .instance_stream=g.decl->inst_stream,.input_streams=g.decl->streams,
            .instance_offset=g_ivb_off[g.stride]*4,
            .previous_offset=g.prev_off,.previous_stride=g.prev_stride,
            .instance_register=v->ir_reg,.instance_registers=v->ir_count,
            .bone_register=(unsigned)v->roles[PF_ROLE_BONES],.bone_registers=g.stride
        };
        for (unsigned i=0;i<6;i++) d.dip[i]=g.dip[i];
        HRESULT hr=pf_submit_capture(&recovery,&d);
        if (FAILED(hr)) {
            fail_session("recovery palette capture failed",(unsigned)hr);
            return 0;
        }
        recovery_model=g.model; recovery_method=g.method; recovery_vs=g.ea_vs;
    }
    return unchecked_stage(mesh);
}

static int recovery_cancel_pending(void)
{
    unsigned n=g.count;
    if (!n) return 1;
    if (!recovery_matches()) return 0;
    g.count=0;
    if (recovery_begin) recovery_begin();
    pf_submit_result r=pf_submit_pending(&recovery,g_stage,n);
    if (recovery_end) recovery_end();
    if (FAILED(r.error) || !r.coherent || !r.complete)
        fail_session("pending ordinary replay failed",(unsigned)r.error);
    return r.coherent && r.complete;
}

static int draw_batch(int counted)
{
    if (!leader_enabled || !recovery_matches())
        return unchecked_draw_batch(counted);
    unsigned n=g.count;
    if (!n) return 1;
    int timed=counted && g.shadow && !g_extra;
    LONGLONG t0=timed?qpc():0;
    g.count=0;
    if (recovery_begin) recovery_begin();
    pf_submit_result r=pf_submit_batch(&recovery,g_stage,n);
    if (recovery_end) recovery_end();
    if (FAILED(r.error) || !r.coherent || !r.complete)
        fail_session("checked batch rejected an operation",(unsigned)r.error);
    if (r.committed && counted) {
        add(REASON(meshes),r.committed); add(REASON(draws),1);
        if (g.bonelist) add(REASON(bonelist),r.committed);
        if (!g_extra) g_cat_meshes[g.cat]+=r.committed;
    }
    if (timed) g_shadow_ticks+=qpc()-t0;
    return r.coherent && r.complete;
}
