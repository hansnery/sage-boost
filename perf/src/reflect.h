/* Which water reflection updates run when ReflectionInterval > 1. */
#ifndef PF_REFLECT_H
#define PF_REFLECT_H

enum { PF_REFLECT_SKIP, PF_REFLECT_RUN, PF_REFLECT_FORCED };

typedef struct {
    unsigned interval;
    int started;
    unsigned skipped;    /* skipped in a row since the last run */
    long long last_call; /* clock of the previous call */
    long long stall;     /* a gap at least this long forces a run */
} pf_reflect;

void pf_reflect_init(pf_reflect *r, unsigned interval, long long stall);

/* One call per reflection update. RUN: due; FORCED: the first call, or one
 * after a stall (loading, alt-tab, a device reset); SKIP: reuse the last
 * reflection. */
int pf_reflect_next(pf_reflect *r, long long now);
/* A game frame whose render was skipped (catch-up): it counts as a frame that
 * reused the stored pass, so the next render redraws on schedule and the pass
 * shown is never more than interval - 1 game frames old. */
void pf_reflect_frame_skipped(pf_reflect *r);
/* A call that runs whatever the schedule says; the next one counts from it. */
void pf_reflect_hold(pf_reflect *r, long long now);

#endif
