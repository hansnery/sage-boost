/* Closed-loop POLICY model. No scene, game functions, GPU or presentation.
 * The legacy planner is the real production pf_fps_schedule; its surrounding
 * timing/state adapter is modeled explicitly. Costs are controlled inputs,
 * not paired samples or a replay of any gameplay capture. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fps.h"
#include "pairpace.h"

void *pf_enter(unsigned k, void **slot) { (void)k; (void)slot; return 0; }
void *pf_exit(unsigned k, unsigned char *esp) { (void)k; (void)esp; return 0; }

static unsigned failures, checks;
#define CHECK(test) do { checks++; if (!(test)) { failures++; \
    fprintf(stderr, "FAIL %s:%u: %s\n", __FILE__, __LINE__, #test); } } while (0)

typedef struct {
    const char *name;
    unsigned regular, extra, jitter, spike_every, spike, period;
} workload;

typedef struct {
    unsigned regular, extras, late, max_debt, max_gap, gap_count;
    unsigned gaps[4096], epoch_counts[120];
    unsigned long long last_end;
} result;

static unsigned native_time(unsigned long long us)
{
    return (unsigned)(us / 1000u) * 1000u;
}

static void presented(result *r, unsigned long long now, unsigned long long *last)
{
    if (*last) {
        unsigned gap = (unsigned)(now - *last);
        if (gap > r->max_gap) r->max_gap = gap;
        CHECK(r->gap_count < sizeof r->gaps / sizeof r->gaps[0]);
        if (r->gap_count < sizeof r->gaps / sizeof r->gaps[0])
            r->gaps[r->gap_count++] = gap;
    }
    *last = now;
}

/* The native limiter reads integer milliseconds, and waits from its previous
 * completion. With paired pacing, it can stop earlier at the saved absolute
 * deadline, or not wait when work has already run past the native endpoint.
 * 'broken' disables repayment, or also reanchors every frame. The latter
 * incorrectly accepts repeated overruns as a new normal update schedule. */
static result run(const workload *w, int paired, int broken)
{
    pf_pairpace p;
    result r;
    unsigned sequence = 0, estimate = w->extra;
    unsigned long long now = 0, last = 0;
    memset(&r, 0, sizeof r);
    pf_pair_reset(&p);
    while (now < 60000000ull) {
        unsigned long long start = now;
        unsigned native_start = native_time(start);
        unsigned regular = w->regular, actual = w->extra;
        unsigned delay = 0, planned = 0, elapsed, period = w->period;
        /* Alternating deterministic jitter has zero mean. A burst is actual
         * cost the planner did not know in advance. */
        sequence++;
        if (w->jitter) {
            regular += sequence % 2 ? w->jitter : 0u;
            regular -= sequence % 2 ? 0u : w->jitter;
        }
        if (w->spike_every && sequence % w->spike_every == 0)
            actual += w->spike;
        now += regular;
        if (now < 60000000ull) {
            r.regular++;
            r.epoch_counts[(unsigned)(now / 500000u)]++;
            presented(&r, now, &last);
        }
        elapsed = native_time(now) - native_start;
        /* No call to the wait hook if the regular work already overran. */
        if (elapsed < period) {
            if (paired) {
                if (broken == 2) pf_pair_reset(&p);
                pf_pair_begin(&p, sequence, native_time(now), elapsed, period, 1);
                planned = pf_pair_plan(&p, native_time(now), estimate, estimate, &delay);
                if (planned) {
                    now += delay;
                    planned = pf_pair_claim(&p, native_time(now), estimate);
                }
            } else {
                pf_fps_sched s = {period, period - elapsed, 1u, estimate, estimate,
                                  3000u + estimate / 8u};
                planned = pf_fps_schedule(&s, &delay);
                if (planned) now += delay;
                /* Legacy's second, immediate pre-draw admission check. */
                if (native_time(now) - native_start + estimate + s.margin > period)
                    planned = 0;
            }
            if (planned) {
                now += actual;
                if (now < 60000000ull) {
                    r.extras++;
                    presented(&r, now, &last);
                }
                if (paired && (int)(native_time(now) - p.deadline) > 0) {
                    unsigned debt = native_time(now) - p.deadline;
                    r.late++;
                    if (debt > r.max_debt) r.max_debt = debt;
                }
            }
            /* Model Sleep(0)/clock polling at the next millisecond edge.
             * No fabricated sub-millisecond timer or zero-time rendering. */
            while (native_time(now) - native_start < period) {
                if (paired && !broken && pf_pair_wait_done(&p, native_time(now))) break;
                now = (now / 1000u + 1u) * 1000u;
            }
        }
        r.last_end = now;
        CHECK(now > start);
        CHECK(sequence < 10000);
    }
    return r;
}

static int compare_unsigned(const void *a, const void *b)
{
    unsigned x = *(const unsigned *)a, y = *(const unsigned *)b;
    return (x > y) - (x < y);
}

static void print_result(const workload *w, const char *mode, result r)
{
    unsigned median, p95;
    qsort(r.gaps, r.gap_count, sizeof r.gaps[0], compare_unsigned);
    median = r.gap_count ? r.gaps[r.gap_count / 2] : 0;
    p95 = r.gap_count ? r.gaps[(r.gap_count - 1) * 95 / 100] : 0;
    printf("{\"case\":\"%s\",\"mode\":\"%s\",\"duration_us\":60000000,"
           "\"regular_cost_us\":%u,\"extra_cost_us\":%u,\"jitter_us\":%u,"
           "\"spike_every\":%u,\"spike_us\":%u,\"native_period_us\":%u,"
           "\"regular\":%u,\"extras\":%u,\"late_pairs\":%u,\"max_debt_us\":%u,"
           "\"gap_p50_us\":%u,\"gap_p95_us\":%u,\"gap_max_us\":%u}\n",
           w->name, mode, w->regular, w->extra, w->jitter, w->spike_every, w->spike,
           w->period, r.regular, r.extras, r.late, r.max_debt, median, p95, r.max_gap);
}

static void boundaries(void)
{
    pf_pairpace p;
    unsigned delay;
    pf_pair_reset(&p);
    delay = 123;
    CHECK(!pf_pair_plan(&p, 0, 10000, 10000, &delay) && delay == 123);
    CHECK(!pf_pair_wait_done(&p, 0));
    pf_pair_begin(&p, 1, 20000, 20000, 33000, 1);
    CHECK(p.deadline == 33000);
    CHECK(pf_pair_plan(&p, 20000, 12849, 12849, &delay) && delay == 0);
    CHECK(!pf_pair_claim(&p, 25000, 12849)); /* timer wake too late */
    CHECK(pf_pair_claim(&p, 20000, 12849));
    pf_pair_begin(&p, 1, 32000, 32000, 33000, 1);
    CHECK(p.deadline == 33000 && p.used);
    CHECK(!pf_pair_claim(&p, 20000, 12849)); /* at most one */
    CHECK(!pf_pair_plan(&p, 20000, 12849, 12849, &delay));
    CHECK(!pf_pair_wait_done(&p, 32999));
    CHECK(pf_pair_wait_done(&p, 33000));

    /* A 1 ms late completion is repaid, not added to every future frame. */
    pf_pair_begin(&p, 2, 54000, 20000, 33000, 1);
    CHECK(p.deadline == 66000);
    CHECK(pf_pair_claim(&p, 54000, 12849));
    pf_pair_begin(&p, 3, 87000, 20000, 33000, 1);
    CHECK(p.deadline == 99000);
    CHECK(!pf_pair_claim(&p, 87000, 15000));
    CHECK(pf_pair_wait_done(&p, 99000));

    /* Do not reuse debt across a missing frame, rate change or pause. */
    pf_pair_begin(&p, 5, 125000, 20000, 33000, 1);
    CHECK(p.deadline == 138000);
    pf_pair_begin(&p, 6, 158000, 20000, 41000, 1);
    CHECK(p.deadline == 179000 && p.period == 41000);
    pf_pair_begin(&p, 7, 199000, 20000, 41000, 0);
    CHECK(!p.active && !pf_pair_wait_done(&p, 500000));
    pf_pair_begin(&p, 8, 600000, 20000, 33000, 1);
    CHECK(p.deadline == 613000);
    pf_pair_begin(&p, 9, 1600000, 20000, 33000, 1);
    CHECK(p.deadline == 1613000);

    /* A small debt must survive network-period changes. Otherwise changing
     * the scale at each logic tick could erase debt repeatedly and slow play. */
    pf_pair_reset(&p);
    pf_pair_begin(&p, 1, 20000, 20000, 33000, 1);
    pf_pair_begin(&p, 2, 54000, 20000, 41000, 1);
    CHECK(p.deadline == 74000); /* native endpoint is 75000, 1 ms still owed */
    pf_pair_begin(&p, 3, 95000, 20000, 33000, 1);
    CHECK(p.deadline == 107000); /* native endpoint 108000 */
    CHECK(!pf_pair_wait_done(&p, 106999));
    CHECK(pf_pair_wait_done(&p, 107000));

    /* Discontinuous clocks or long accumulated lateness do not trigger a
     * burst of accelerated updates. Start again at the native endpoint. */
    pf_pair_begin(&p, 4, 5000, 2000, 33000, 1);
    CHECK(p.deadline == 36000);
    pf_pair_begin(&p, 5, 200000, 20000, 33000, 1);
    CHECK(p.deadline == 213000);

    /* Sequence and clock wrap use differences, not absolute ordering. */
    pf_pair_reset(&p);
    pf_pair_begin(&p, 0xffffffffu, 0xfffffff0u, 20000, 33000, 1);
    CHECK(p.deadline == 12984);
    CHECK(pf_pair_claim(&p, 0xfffffff0u, 12849));
    CHECK(!pf_pair_wait_done(&p, 0xfffffff0u));
    CHECK(pf_pair_wait_done(&p, 12984));
    pf_pair_begin(&p, 0, 32984, 20000, 33000, 1);
    CHECK(p.deadline == 45984);

    delay = 123;
    CHECK(!pf_pair_plan(&p, 32984, 0xffffffffu, 10000, &delay));
    CHECK(!pf_pair_plan(&p, 32984, 10000, 10001, &delay));
    CHECK(!pf_pair_plan(&p, 32984, 0, 0, &delay));
    CHECK(!pf_pair_plan(&p, 32984, 10000, 10000, NULL));
    CHECK(delay == 123);
    pf_pair_begin(&p, 1, 66000, 33000, 33000, 1);
    CHECK(!p.active);
    pf_pair_begin(&p, 2, 70000, 1000, 9999, 1);
    CHECK(!p.active);
    pf_pair_begin(&p, 3, 80000, 1000, 200001, 1);
    CHECK(!p.active);
}

/* Same native sequence of changing periods on two independently driven
 * clients. Every frame samples a different fractional millisecond phase.
 * Clock choice and repayment are checked against a separate sum-of-periods
 * oracle, not against fields copied from the controller. */
static void changing_periods(void)
{
    pf_pairpace p;
    unsigned i, sums = 0;
    unsigned long long now = 123456000ull;
    const unsigned origin = native_time(now);
    pf_pair_reset(&p);
    for (i = 1; i <= 6000; i++) {
        unsigned period = i % 12 < 6 ? 33000u : 41000u;
        unsigned start = native_time(now), delay;
        unsigned regular = i % 2 ? 21854u : 17854u;
        now += regular;
        sums += period;
        pf_pair_begin(&p, i, native_time(now), native_time(now) - start, period, 1);
        CHECK(p.deadline == origin + sums);
        if (pf_pair_plan(&p, native_time(now), 12849, 12849, &delay)) {
            now += delay;
            if (pf_pair_claim(&p, native_time(now), 12849)) now += 12849;
        }
        while (native_time(now) - start < period &&
               !pf_pair_wait_done(&p, native_time(now)))
            now = (now / 1000u + 1u) * 1000u;
        CHECK((int)(native_time(now) - (origin + sums)) >= 0);
        CHECK(native_time(now) - (origin + sums) <= PF_PAIR_BORROW_US);
    }
}

int main(void)
{
    static const workload cases[] = {
        {"cheap", 8000, 4000, 0, 0, 0, 33000},
        {"aggregate_anchor_10819", 19854, 10819, 0, 0, 0, 33000},
        {"aggregate_anchor_12849", 19854, 12849, 0, 0, 0, 33000},
        {"anchor_with_2ms_jitter", 19854, 12849, 2000, 0, 0, 33000},
        {"anchor_with_unpredicted_spikes", 19854, 12849, 0, 60, 10000, 33000},
        {"heavier_aggregate_anchor", 23284, 12261, 0, 0, 0, 33000},
        {"overload", 26000, 15000, 0, 0, 0, 33000},
        {"no_native_wait", 35000, 15000, 0, 0, 0, 33000},
        {"network_slow_period", 19854, 12849, 0, 0, 0, 41000},
        {"repayment_control", 21000, 12500, 0, 0, 0, 33000},
    };
    result cheap = {0}, anchored = {0};
    unsigned i;
    boundaries();
    changing_periods();
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        result legacy = run(&cases[i], 0, 0), paired = run(&cases[i], 1, 0);
        print_result(&cases[i], "legacy_admission_model", legacy);
        print_result(&cases[i], "paired_cadence_model", paired);
        if (i < 7 || i == 9) {
            CHECK(paired.regular >= 1817 && paired.regular <= 1819);
        }
        if (i == 1 || i == 2 || i == 3) {
            CHECK(legacy.extras == 0);
            CHECK(paired.extras >= 1817);
        }
        if (i == 0) cheap = paired;
        if (i == 2) anchored = paired;
        if (i == 6 || i == 7) CHECK(paired.extras == 0);
        if (i == 8) CHECK(paired.regular >= 1462 && paired.regular <= 1464);
    }
    /* Different rendering costs do not change regular-update cadence in
     * this model. This does not execute any multiplayer/network code. */
    for (i = 0; i < 120; i++) {
        int d = (int)cheap.epoch_counts[i] - (int)anchored.epoch_counts[i];
        CHECK(d >= -1 && d <= 1);
    }
    {
        result good = run(&cases[9], 1, 0), bad = run(&cases[9], 1, 1);
        result reanchored = run(&cases[9], 1, 2);
        print_result(&cases[9], "fault_no_repayment", bad);
        print_result(&cases[9], "fault_reanchor_every_frame", reanchored);
        CHECK(good.regular >= 1817);
        CHECK(bad.extras < 10); /* retaining debt without paying it starves extras */
        CHECK(reanchored.regular < 1795); /* accepting debt then forgetting it slows logic */
    }
    printf("{\"summary\":true,\"checks\":%u,\"failures\":%u}\n", checks, failures);
    return failures ? 1 : 0;
}
