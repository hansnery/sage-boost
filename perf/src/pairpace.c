#include "pairpace.h"

void pf_pair_reset(pf_pairpace *p)
{
    p->deadline = p->period = p->sequence = p->observed = 0;
    p->active = p->used = 0;
}

void pf_pair_begin(pf_pairpace *p, unsigned sequence, unsigned now,
                   unsigned elapsed, unsigned period, int eligible)
{
    unsigned deadline;
    /* A native wait can invoke its hook many times in the same frame. Never
     * reopen an already claimed allowance on a repeated observation. */
    if (p->active && sequence == p->sequence && eligible && period == p->period &&
        now - p->observed < PF_PAIR_STALL_US)
        return;
    /* Native waits outside this range are loading/fast-play/special timing,
     * not the supported normal cadence. Never accumulate their time debt. */
    if (!eligible || period < 10000u || period > 200000u || elapsed >= period) {
        pf_pair_reset(p);
        return;
    }
    deadline = now + period - elapsed;
    if (p->active && sequence == p->sequence + 1u &&
        now - p->observed < PF_PAIR_STALL_US) {
        unsigned next = p->deadline + period;
        /* Forgive long stalls, not small extra-frame overruns. Do not move
         * a deadline into the future beyond this frame's native endpoint. */
        if ((int)(now - next) <= (int)(2u * period) &&
            (int)(next - deadline) <= 0)
            deadline = next;
    }
    p->deadline = deadline;
    p->period = period;
    p->sequence = sequence;
    p->observed = now;
    p->active = 1;
    p->used = 0;
}

static int fits(const pf_pairpace *p, unsigned now, unsigned expected)
{
    int remaining;
    if (!p->active || p->used || !expected || expected > p->period)
        return 0;
    remaining = (int)(p->deadline - now);
    /* All arithmetic below is bounded by the validated native period. */
    return remaining >= -(int)PF_PAIR_BORROW_US &&
           (int)(expected + PF_PAIR_RESERVE_US) <=
               remaining + (int)PF_PAIR_BORROW_US;
}

int pf_pair_plan(const pf_pairpace *p, unsigned now, unsigned expected,
                 unsigned average, unsigned *delay)
{
    int slack;
    unsigned ideal, latest, start;
    if (!delay || !fits(p, now, expected) || !average || average > expected)
        return 0;
    /* Prefer the midpoint, but do not deliberately spend the borrow on
     * pacing. Borrow is only for work that cannot otherwise fit at once. */
    ideal = average < p->period / 2u ? p->period / 2u - average : 0;
    slack = (int)(p->deadline - now) - (int)(expected + PF_PAIR_RESERVE_US);
    latest = slack > 0 ? (unsigned)slack : 0;
    start = ideal < latest ? ideal : latest;
    if (start + average < p->period / 8u)
        return 0; /* Avoid an immediate repeat with no useful separation. */
    *delay = start;
    return 1;
}

int pf_pair_claim(pf_pairpace *p, unsigned now, unsigned expected)
{
    if (!fits(p, now, expected))
        return 0;
    p->used = 1;
    return 1;
}

int pf_pair_wait_done(const pf_pairpace *p, unsigned now)
{
    return p->active && (int)(now - p->deadline) >= 0;
}
