#include "reflect.h"

void pf_reflect_init(pf_reflect *r, unsigned interval, long long stall)
{
    r->interval = interval ? interval : 1;
    r->started = 0;
    r->skipped = 0;
    r->last_call = 0;
    r->stall = stall;
}

int pf_reflect_next(pf_reflect *r, long long now)
{
    int run;

    if (!r->started || now - r->last_call >= r->stall)
        run = PF_REFLECT_FORCED;
    else if (r->skipped + 1 >= r->interval)
        run = PF_REFLECT_RUN;
    else
        run = PF_REFLECT_SKIP;
    r->started = 1;
    r->last_call = now;
    r->skipped = run == PF_REFLECT_SKIP ? r->skipped + 1 : 0;
    return run;
}

void pf_reflect_frame_skipped(pf_reflect *r)
{
    if (r->started)
        r->skipped++;
}

void pf_reflect_hold(pf_reflect *r, long long now)
{
    r->started = 1;
    r->last_call = now;
    r->skipped = 0;
}
