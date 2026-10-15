/* The water reflection skip scheduler. */
#include <stdio.h>

#include "reflect.h"

static int g_failures;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            g_failures++;                                 \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

static void run(unsigned interval, const long long *t, const int *want, unsigned n)
{
    pf_reflect r;
    unsigned i;
    pf_reflect_init(&r, interval, 250);
    for (i = 0; i < n; i++) {
        int got = pf_reflect_next(&r, t[i]);
        CHECK(got == want[i], "interval %u, call %u at %lld: %d, want %d", interval, i, t[i], got,
              want[i]);
    }
}

int main(void)
{
    static const long long t[] = { 0, 33, 66, 100, 133, 166, 500, 533, 566 };
    static const int every[] = { 2, 1, 1, 1, 1, 1, 2, 1, 1 };
    static const int half[] = { 2, 0, 1, 0, 1, 0, 2, 0, 1 };
    static const int third[] = { 2, 0, 0, 1, 0, 0, 2, 0, 0 };
    static const int fourth[] = { 2, 0, 0, 0, 1, 0, 2, 0, 0 };
    static const long long edge_t[] = { 0, 249, 499, 749 };
    static const int edge_want[] = { 2, 0, 2, 2 };

    printf("schedules\n");
    run(1, t, every, 9);
    run(2, t, half, 9);
    run(3, t, third, 9);
    run(4, t, fourth, 9);
    printf("stall boundary\n");
    run(4, edge_t, edge_want, 4);

    printf("held at full rate\n");
    {
        pf_reflect r;
        pf_reflect_init(&r, 2, 250);
        CHECK(pf_reflect_next(&r, 0) == PF_REFLECT_FORCED, "first");
        pf_reflect_hold(&r, 33);
        pf_reflect_hold(&r, 66);
        CHECK(pf_reflect_next(&r, 100) == PF_REFLECT_SKIP, "the interval resumes after a hold");
        CHECK(pf_reflect_next(&r, 133) == PF_REFLECT_RUN, "then runs");
        pf_reflect_hold(&r, 500);
        CHECK(pf_reflect_next(&r, 533) == PF_REFLECT_SKIP, "a hold counts as the call's time");
    }

    printf("frames whose render was skipped (catch-up)\n");
    {
        pf_reflect r;
        pf_reflect_init(&r, 2, 250);
        pf_reflect_frame_skipped(&r);
        CHECK(pf_reflect_next(&r, 0) == PF_REFLECT_FORCED, "before the first call: still forced");
        pf_reflect_frame_skipped(&r);
        CHECK(pf_reflect_next(&r, 66) == PF_REFLECT_RUN,
              "interval 2: drawn, skipped, then redrawn (the map is never 2 frames old)");
        CHECK(pf_reflect_next(&r, 100) == PF_REFLECT_SKIP, "then the interval goes on");
        pf_reflect_init(&r, 3, 250);
        CHECK(pf_reflect_next(&r, 0) == PF_REFLECT_FORCED, "interval 3: first");
        pf_reflect_frame_skipped(&r);
        CHECK(pf_reflect_next(&r, 66) == PF_REFLECT_SKIP, "one frame skipped, one reused");
        CHECK(pf_reflect_next(&r, 100) == PF_REFLECT_RUN, "then redrawn, 2 frames after");
        pf_reflect_init(&r, 1, 250);
        pf_reflect_next(&r, 0);
        pf_reflect_frame_skipped(&r);
        CHECK(pf_reflect_next(&r, 66) == PF_REFLECT_RUN, "interval 1: every drawn frame");
    }
    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: reflect tests passed\n");
    return 0;
}
