#include "extraprofile.h"

static void clear_row(pf_ep_row *r)
{
    unsigned i;
    for (i = 0; i < PF_EP_PASSES; i++) r->us[i] = r->samples[i] = 0;
    r->total_us = 0;
    r->valid = r->invalid = 0;
}

int pf_ep_label(const char *label)
{
    static const char *const names[PF_EP_PASSES] = {
        "UpdateShadowMap", "UpdateWaterReflection", "RenderViews", "RenderUI"
    };
    unsigned i;
    if (!label) return -1;
    for (i = 0; i < PF_EP_PASSES; i++) {
        const char *a = label, *b = names[i];
        while (*a && *a == *b) { a++; b++; }
        if (*a == *b) return (int)i;
    }
    return -1;
}

int pf_ep_tracks(const pf_ep *p, void *self)
{
    unsigned i;
    if (!p->active || p->bad) return 0;
    for (i = 0; i < p->depth; i++)
        if (p->stack[i].self == self) return 1;
    return 0;
}

void pf_ep_init(pf_ep *p)
{
    clear_row(&p->row);
    clear_row(&p->frame);
    p->start = p->depth = p->active = p->bad = 0;
}

void pf_ep_begin(pf_ep *p, unsigned now_us)
{
    if (p->active) p->row.invalid++;
    clear_row(&p->frame);
    p->start = now_us;
    p->depth = p->bad = 0;
    p->active = 1;
}

void pf_ep_marker_begin(pf_ep *p, void *self, const char *label, unsigned now_us)
{
    unsigned i, outer = 1;
    int pass;
    if (!p->active || p->bad) return;
    pass = pf_ep_label(label);
    if (pass < 0) return;
    if (p->depth == PF_EP_DEPTH) { p->bad = 1; return; }
    for (i = 0; i < p->depth; i++) {
        /* Reusing a live scope identity makes End ambiguous. */
        if (p->stack[i].self == self) { p->bad = 1; return; }
        if (p->stack[i].pass == (unsigned)pass) outer = 0;
    }
    p->stack[p->depth++] = (pf_ep_entry){self, now_us, (unsigned)pass, outer};
}

void pf_ep_marker_end(pf_ep *p, void *self, unsigned now_us)
{
    pf_ep_entry *e;
    if (!pf_ep_tracks(p, self)) return;
    e = &p->stack[p->depth - 1];
    if (e->self != self) { p->bad = 1; return; }
    if (e->outer) {
        p->frame.us[e->pass] += (unsigned)(now_us - e->start);
        p->frame.samples[e->pass]++;
    }
    p->depth--;
}

void pf_ep_end(pf_ep *p, unsigned now_us, int success)
{
    unsigned i;
    if (!p->active) return;
    if (!success || p->bad || p->depth) p->row.invalid++;
    else {
        p->row.valid++;
        p->row.total_us += (unsigned)(now_us - p->start);
        for (i = 0; i < PF_EP_PASSES; i++) {
            p->row.us[i] += p->frame.us[i];
            p->row.samples[i] += p->frame.samples[i];
        }
    }
    clear_row(&p->frame);
    p->active = p->bad = p->depth = 0;
}

void pf_ep_take(pf_ep *p, pf_ep_row *out)
{
    *out = p->row;
    clear_row(&p->row);
}
