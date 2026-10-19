#include "pairpace.h"

void pf_pair_reset(pf_pairpace *p)
{
    p->deadline = p->period = p->sequence = p->observed = 0;
    p->active = p->limited = 0;
    p->quota = p->used = 0;
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
    p->quota = 1;
    p->limited = 0;
}

void pf_pair_limit(pf_pairpace *p, unsigned now, unsigned expected, unsigned limit)
{
    int remaining;
    if (!p->active || p->used || p->limited) return;
    p->limited = 1;
    if (limit < 2 || limit > 3 || !expected || expected > p->period) return;
    remaining = (int)(p->deadline - now);
    while (limit > 1) {
        if ((int)(limit * (expected + PF_PAIR_RESERVE_US)) <= remaining) {
            p->quota = limit;
            return;
        }
        limit--;
    }
}

void pf_pair_limit_sequential(pf_pairpace *p, unsigned now, unsigned expected,
                              unsigned average, unsigned limit)
{
    int remaining;
    if (!p->active || p->used || p->limited) return;
    p->limited = 1;
    if (limit < 2 || limit > 3 || !expected || expected > p->period ||
        !average || average > expected) return;
    remaining = (int)(p->deadline - now);
    while (limit > 1) {
        /* Average only selects opportunities, never admits a draw. After a
         * cheap completion the next draw can fit the unchanged worst-case
         * claim; after a slow completion that opportunity is discarded. */
        if ((int)((limit - 1u) * average + expected + PF_PAIR_RESERVE_US) <= remaining) {
            p->quota = limit;
            return;
        }
        limit--;
    }
}

static int fits(const pf_pairpace *p, unsigned now, unsigned expected)
{
    int remaining;
    if (!p->active || p->used >= p->quota || !expected || expected > p->period)
        return 0;
    remaining = (int)(p->deadline - now);
    if (p->quota > 1)
        return (int)(expected + PF_PAIR_RESERVE_US) <= remaining;
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
    if (p->quota > 1) {
        unsigned pending = p->quota - p->used;
        int target = (int)(p->period * (p->used + 1u) / (p->quota + 1u)) -
                     (int)average - (int)(now - p->observed);
        int remaining = (int)(p->deadline - now);
        /* If a draw was slower than expected, do not reserve time for draws
         * which no longer fit. Never move the native deadline to fit them. */
        while (pending > 1 && (int)(pending * (expected + PF_PAIR_RESERVE_US)) > remaining)
            pending--;
        slack = remaining - (int)(pending * (expected + PF_PAIR_RESERVE_US));
        ideal = target > 0 ? (unsigned)target : 0;
        latest = slack > 0 ? (unsigned)slack : 0;
        *delay = ideal < latest ? ideal : latest;
        return 1;
    }
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

int pf_pair_plan_sequential(const pf_pairpace *p, unsigned now, unsigned expected,
                            unsigned average, unsigned *delay)
{
    unsigned pending, ideal, latest;
    int remaining, target, slack;
    if (p->quota <= 1)
        return pf_pair_plan(p, now, expected, average, delay);
    if (!delay || !fits(p, now, expected) || !average || average > expected)
        return 0;
    pending = p->quota - p->used;
    remaining = (int)(p->deadline - now);
    while (pending > 1 &&
           (int)((pending - 1u) * average + expected + PF_PAIR_RESERVE_US) > remaining)
        pending--;
    slack = remaining - (int)((pending - 1u) * average + expected + PF_PAIR_RESERVE_US);
    target = (int)(p->period * (p->used + 1u) / (p->quota + 1u)) -
             (int)average - (int)(now - p->observed);
    ideal = target > 0 ? (unsigned)target : 0;
    /* Fine-clock pacing can cross the native clock's unknown fractional
     * phase. Preserve this millisecond in addition to the claim reserve;
     * otherwise a cheap first draw can lose an affordable second claim. */
    latest = slack > 1000 ? (unsigned)slack - 1000u : 0;
    *delay = ideal < latest ? ideal : latest;
    return 1;
}

int pf_pair_claim(pf_pairpace *p, unsigned now, unsigned expected)
{
    if (!fits(p, now, expected))
        return 0;
    p->used++;
    return 1;
}

int pf_pair_wait_done(const pf_pairpace *p, unsigned now)
{
    return p->active && (int)(now - p->deadline) >= 0;
}
