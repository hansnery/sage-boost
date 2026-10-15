#include "clock.h"

void pf_clock_start(pf_clock *c, long long t, long long freq)
{
    c->first = t;
    c->freq = freq;
    c->row = 0;
}

int pf_clock_tick(pf_clock *c, long long t, unsigned *closed)
{
    if (t - c->first < (long long)(c->row + 1) * c->freq)
        return 0;
    *closed = c->row;
    c->row = (unsigned)((t - c->first) / c->freq);
    return 1;
}
