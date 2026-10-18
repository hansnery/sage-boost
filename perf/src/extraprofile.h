#ifndef PF_EXTRAPROFILE_H
#define PF_EXTRAPROFILE_H

#define PF_EP_PASSES 4
#define PF_EP_DEPTH 8
typedef struct {
    unsigned long long us[PF_EP_PASSES], total_us;
    unsigned samples[PF_EP_PASSES], valid, invalid;
} pf_ep_row;
typedef struct {
    void *self;
    unsigned start, pass, outer;
} pf_ep_entry;
typedef struct {
    pf_ep_row row, frame;
    pf_ep_entry stack[PF_EP_DEPTH];
    unsigned start, depth, active, bad;
} pf_ep;

/* Single render thread only; caller filters other threads. Times are unsigned
 * microseconds (subtraction handles a single clock wrap). No clocks, heap or
 * system calls here. Unknown markers never occupy stack entries. */
int pf_ep_label(const char *label);
int pf_ep_tracks(const pf_ep *p, void *self);
void pf_ep_init(pf_ep *p);
void pf_ep_begin(pf_ep *p, unsigned now_us);
void pf_ep_marker_begin(pf_ep *p, void *self, const char *label, unsigned now_us);
void pf_ep_marker_end(pf_ep *p, void *self, unsigned now_us);
void pf_ep_end(pf_ep *p, unsigned now_us, int success);
/* Drain completed frames; an in-progress frame remains intact. */
void pf_ep_take(pf_ep *p, pf_ep_row *out);
#endif
