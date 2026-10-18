#ifndef PF_PRESENTGAP_H
#define PF_PRESENTGAP_H

#define PF_PRESENT_BINS 12
typedef struct {
    unsigned calls, gaps, bins[PF_PRESENT_BINS];
    unsigned p50_us, p95_us;
} pf_present_row;

/* Main render thread only. Measures completed flip-enabled EndScene calls,
 * not GPU presentation completion or physical display refresh. */
void pf_present_init(void);
void pf_present_hit(unsigned long long now_us);
/* Drain counters, preserving the prior timestamp across CSV row boundaries.
 * Quantiles are bucket upper bounds; 0 means no gaps or overflow (>1 s). */
void pf_present_take(pf_present_row *out);
#endif
