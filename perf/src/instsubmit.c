#include "instsubmit.h"

static int range(unsigned first, unsigned count)
{
    return count && first < PF_SUBMIT_REGS && count <= PF_SUBMIT_REGS - first;
}

HRESULT pf_submit_capture(pf_submit *s, const pf_submit_desc *d)
{
    if (s->open)
        return E_UNEXPECTED;
    s->valid = s->operation = 0;
    if (!d || !d->dev || !d->game_vs || !d->our_vs || !d->game_decl || !d->our_decl ||
        !d->instance_vb || !d->instance_stream || d->instance_stream >= 16 ||
        !d->input_streams || d->input_streams >= (1u << d->instance_stream) ||
        !range(d->instance_register, d->instance_registers) ||
        !range(d->bone_register, d->bone_registers) || (d->bone_registers & 1) ||
        d->bone_registers > d->instance_registers)
        return E_INVALIDARG;
    s->draw = *d;
    s->operation = PF_SUBMIT_CAPTURE_BONES;
    HRESULT hr = ((pf_const_f_fn)PF_VT(d->dev)[PF_DEV_GET_VS_CONST_F])(
        d->dev, d->bone_register, s->bones, d->bone_registers);
    s->operation = 0;
    s->valid = SUCCEEDED(hr);
    return hr;
}

static pf_submit_result initial(pf_submit *s, unsigned *staged, unsigned count)
{
    pf_submit_result r = {S_OK, 0, 0, 0, 1, count == 0};
    if (s->open) {
        r.error = E_UNEXPECTED;
        r.coherent = r.complete = 0;
        return r;
    }
    if (!s->valid || (count && !staged) || !s->draw.bone_registers ||
        count > s->draw.instance_registers / s->draw.bone_registers) {
        r.error = E_INVALIDARG;
        r.complete = 0;
    }
    return r;
}

static void rejected(pf_submit *s, pf_submit_result *r, HRESULT hr)
{
    if (FAILED(hr) && SUCCEEDED(r->error)) {
        r->error = hr;
        r->failed_operation = s->operation;
    }
}

/* A state write can be retried; a successful draw must never be repeated.
 * A second rejected restore leaves coherent false and forbids replay. */
#define RESTORE(id, expression) do { \
    s->operation = (id); HRESULT restore_hr = (expression); \
    rejected(s, &r, restore_hr); \
    if (FAILED(restore_hr) && FAILED(expression)) r.coherent = 0; \
} while (0)

static void finish(pf_submit *s, pf_submit_result *r, unsigned count)
{
    r->complete = r->committed + r->replayed == count;
    s->operation = s->open = 0;
    if (FAILED(r->error) || !r->coherent || !r->complete)
        s->valid = 0;
}

pf_submit_result pf_submit_pending(pf_submit *s, unsigned *staged, unsigned count)
{
    pf_submit_result r = initial(s, staged, count);
    if (FAILED(r.error) || !count)
        return r;
    const pf_submit_desc *d = &s->draw;
    pf_const_f_fn constants = (pf_const_f_fn)PF_VT(d->dev)[PF_DEV_SET_VS_CONST_F];
    pf_dip_fn draw = (pf_dip_fn)PF_VT(d->dev)[PF_DEV_DRAW_INDEXED];
    s->open = 1;
    for (unsigned i = 0; i < count; i++) {
        s->operation = PF_SUBMIT_PENDING_BONES;
        HRESULT hr = constants(d->dev, d->bone_register,
                               staged + i * d->bone_registers * 4, d->bone_registers);
        rejected(s, &r, hr);
        if (FAILED(hr))
            break;
        s->operation = PF_SUBMIT_PENDING_DRAW;
        hr = draw(d->dev, d->dip[0], (INT)d->dip[1], d->dip[2], d->dip[3], d->dip[4], d->dip[5]);
        rejected(s, &r, hr);
        if (FAILED(hr))
            break;
        r.replayed++;
    }
    RESTORE(PF_SUBMIT_PENDING_RESTORE,
            constants(d->dev, d->bone_register, s->bones, d->bone_registers));
    finish(s, &r, count);
    return r;
}

pf_submit_result pf_submit_batch(pf_submit *s, unsigned *staged, unsigned count)
{
    pf_submit_result r = initial(s, staged, count);
    if (FAILED(r.error) || !count)
        return r;
    const pf_submit_desc *d = &s->draw;
    void *dev = d->dev;
    void **vt = PF_VT(dev);
    pf_set_freq_fn freq = (pf_set_freq_fn)vt[PF_DEV_SET_STREAM_FREQ];
    pf_const_f_fn constants = (pf_const_f_fn)vt[PF_DEV_SET_VS_CONST_F];
    pf_ptr_fn shader = (pf_ptr_fn)vt[PF_DEV_SET_VS], decl = (pf_ptr_fn)vt[PF_DEV_SET_DECL];
    pf_set_stream_fn stream = (pf_set_stream_fn)vt[PF_DEV_SET_STREAM];
    pf_dip_fn draw = (pf_dip_fn)vt[PF_DEV_DRAW_INDEXED];
    unsigned regs = count * d->bone_registers, saved = 0;
    s->open = 1;
#define PREPARE(id, expression) do { \
    s->operation = (id); HRESULT prepare_hr = (expression); \
    rejected(s, &r, prepare_hr); if (FAILED(prepare_hr)) goto restore; \
} while (0)
    PREPARE(PF_SUBMIT_SAVE,
            ((pf_const_f_fn)vt[PF_DEV_GET_VS_CONST_F])(dev, d->instance_register, s->saved, regs));
    saved = 1;
    PREPARE(PF_SUBMIT_SHADER, shader(dev, d->our_vs));
    PREPARE(PF_SUBMIT_DECL, decl(dev, d->our_decl));
    PREPARE(PF_SUBMIT_STREAM, stream(dev, d->instance_stream, d->instance_vb, d->instance_offset, 4));
    for (unsigned i = 0; i < d->instance_stream; i++)
        if (d->input_streams >> i & 1)
            PREPARE(PF_SUBMIT_INPUT_FREQ, freq(dev, i, PF_STREAM_INDEXED | count));
    PREPARE(PF_SUBMIT_INSTANCE_FREQ, freq(dev, d->instance_stream, PF_STREAM_INSTANCE | 1));
    PREPARE(PF_SUBMIT_CONSTANTS, constants(dev, d->instance_register, staged, regs));
    PREPARE(PF_SUBMIT_DRAW, draw(dev, d->dip[0], (INT)d->dip[1], d->dip[2], d->dip[3], d->dip[4], d->dip[5]));
    r.committed = count;
restore:
#undef PREPARE
    for (unsigned i = 0; i <= d->instance_stream; i++)
        if (d->input_streams >> i & 1 || i == d->instance_stream)
            RESTORE(i == d->instance_stream ? PF_SUBMIT_RESTORE_INSTANCE_FREQ : PF_SUBMIT_RESTORE_INPUT_FREQ,
                    freq(dev, i, 1));
    /* D3D9 may retain offset/stride when a null stream is unbound. */
    if (!d->previous_vb)
        RESTORE(PF_SUBMIT_SEED_NULL_STREAM,
                stream(dev, d->instance_stream, d->instance_vb, d->previous_offset, d->previous_stride));
    RESTORE(PF_SUBMIT_RESTORE_STREAM,
            stream(dev, d->instance_stream, d->previous_vb, d->previous_offset, d->previous_stride));
    RESTORE(PF_SUBMIT_RESTORE_DECL, decl(dev, d->game_decl));
    RESTORE(PF_SUBMIT_RESTORE_SHADER, shader(dev, d->game_vs));
    if (saved)
        RESTORE(PF_SUBMIT_RESTORE_CONSTANTS, constants(dev, d->instance_register, s->saved, regs));
    if (!r.committed && r.coherent) {
        for (unsigned i = 0; i < count; i++) {
            s->operation = PF_SUBMIT_REPLAY_BONES;
            HRESULT hr = constants(dev, d->bone_register,
                                   staged + i * d->bone_registers * 4, d->bone_registers);
            rejected(s, &r, hr);
            if (FAILED(hr))
                break;
            s->operation = PF_SUBMIT_REPLAY_DRAW;
            hr = draw(dev, d->dip[0], (INT)d->dip[1], d->dip[2], d->dip[3], d->dip[4], d->dip[5]);
            rejected(s, &r, hr);
            if (FAILED(hr))
                break;
            r.replayed++;
        }
        RESTORE(PF_SUBMIT_RESTORE_BONES,
                constants(dev, d->bone_register, s->bones, d->bone_registers));
    }
    finish(s, &r, count);
    return r;
}
#undef RESTORE
