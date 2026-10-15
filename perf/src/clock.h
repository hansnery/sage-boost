/* Rows close on whole seconds since the first frame. */
#ifndef PF_CLOCK_H
#define PF_CLOCK_H

typedef struct {
    long long first, freq;
    unsigned row; /* time_s of the open row */
} pf_clock;

void pf_clock_start(pf_clock *c, long long t, long long freq);

/* At a frame's end: 1 if t has reached the second after the open row's;
 * *closed gets that row's time_s and the next row opens at t's second. */
int pf_clock_tick(pf_clock *c, long long t, unsigned *closed);

#endif
