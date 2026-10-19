/* Experimental paired-frame admission/cadence. Pure integer policy shared
 * by the runtime adapter and its closed-loop model. */
#ifndef PF_PAIRPACE_H
#define PF_PAIRPACE_H

#define PF_PAIR_BORROW_US 4000u
/* Native timeGetTime is quantized. Keep a full millisecond for its unknown
 * fractional phase, plus 250 us for admission-to-draw overhead. */
#define PF_PAIR_RESERVE_US 1250u
#define PF_PAIR_STALL_US 250000u

typedef struct {
    unsigned deadline, period, sequence, observed;
    unsigned quota, used;
    int active, limited;
} pf_pairpace;

void pf_pair_reset(pf_pairpace *p);
/* Once per regular frame which reaches the native limiter. sequence must
 * also count regular frames with no wait. elapsed is native-clock elapsed
 * in this frame; now uses the SAME wrapping clock, in microseconds. The
 * runtime adapter must use native milliseconds * 1000 for both, reserving
 * its fine clock for pacing only. A missing observation or ineligible scene
 * starts a fresh epoch. Small debt survives native-period changes, so their
 * network adjustment cannot repeatedly erase borrowed time. */
void pf_pair_begin(pf_pairpace *p, unsigned sequence, unsigned now,
                   unsigned elapsed, unsigned period, int eligible);
/* Select once per frame, before any claim. Default one retains beta.14.
 * Multiple extras require all costs plus reserves to fit without borrowing;
 * repeated calls cannot enlarge the allowance. Invalid limits keep one. */
void pf_pair_limit(pf_pairpace *p, unsigned now, unsigned expected, unsigned limit);
/* Returns 1 and a delay before one extra draw, or 0 without changing delay.
 * expected is a conservative complete extra-draw cost; average predicts
 * presentation time. Calling plan does not consume the frame's allowance. */
int pf_pair_plan(const pf_pairpace *p, unsigned now, unsigned expected,
                 unsigned average, unsigned *delay);
/* Recheck after pacing and consume one draw before invoking it. */
int pf_pair_claim(pf_pairpace *p, unsigned now, unsigned expected);
/* End the native wait only when the absolute schedule has been reached.
 * A caller may shorten the native wait; it must never extend it here. */
int pf_pair_wait_done(const pf_pairpace *p, unsigned now);

#endif
