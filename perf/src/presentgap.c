#include "presentgap.h"

static const unsigned bounds[PF_PRESENT_BINS - 1] = {
    8333, 12500, 16667, 20000, 25000, 33334,
    50000, 100000, 200000, 500000, 1000000
};
static pf_present_row row;
static unsigned long long previous;
static int have_previous;

static void clear(void)
{
    unsigned i;
    row.calls = row.gaps = row.p50_us = row.p95_us = 0;
    for (i = 0; i < PF_PRESENT_BINS; i++) row.bins[i] = 0;
}

void pf_present_init(void)
{
    clear();
    previous = 0;
    have_previous = 0;
}

void pf_present_hit(unsigned long long now_us)
{
    unsigned i = 0;
    row.calls++;
    if (have_previous) {
        /* A backwards clock is deliberately overflow, never a short gap. */
        unsigned long long gap = now_us - previous;
        while (i < PF_PRESENT_BINS - 1 && gap > bounds[i]) i++;
        row.bins[i]++;
        row.gaps++;
    }
    previous = now_us;
    have_previous = 1;
}

static unsigned quantile(unsigned percent)
{
    unsigned i, cumulative = 0;
    unsigned rank;
    if (!row.gaps) return 0;
    rank = (unsigned)(((unsigned long long)row.gaps * percent + 99) / 100);
    for (i = 0; i < PF_PRESENT_BINS - 1; i++) {
        cumulative += row.bins[i];
        if (cumulative >= rank) return bounds[i];
    }
    return 0;
}

void pf_present_take(pf_present_row *out)
{
    row.p50_us = quantile(50);
    row.p95_us = quantile(95);
    *out = row;
    clear();
}
