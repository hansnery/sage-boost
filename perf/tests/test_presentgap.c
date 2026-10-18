#include <stdio.h>
#include <stdlib.h>
#include "presentgap.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

int main(void)
{
    const unsigned upper[] = {8333,12500,16667,20000,25000,33334,
                              50000,100000,200000,500000,1000000};
    pf_present_row r;
    unsigned i;
    unsigned long long t;
    pf_present_init();
    pf_present_take(&r);
    CHECK(!r.calls && !r.gaps && !r.p50_us && !r.p95_us);
    pf_present_hit(0);
    pf_present_take(&r);
    CHECK(r.calls == 1 && !r.gaps);
    pf_present_hit(16667);
    pf_present_take(&r);
    CHECK(r.calls == 1 && r.gaps == 1 && r.bins[2] == 1);
    CHECK(r.p50_us == 16667 && r.p95_us == 16667);
    pf_present_take(&r);
    CHECK(!r.calls && !r.gaps);
    for (i = 0; i < 11; i++) {
        pf_present_init();
        pf_present_hit(123);
        pf_present_hit(123 + upper[i]);
        pf_present_hit(124ull + 2ull * upper[i]);
        pf_present_take(&r);
        CHECK(r.calls == 3 && r.gaps == 2);
        CHECK(r.bins[i] == 1 && r.bins[i + 1] == 1);
    }
    pf_present_init();
    t = 500000000000ull; /* Uptime beyond a 32-bit microsecond clock. */
    pf_present_hit(t);
    for (i = 0; i < 100; i++) {
        t += i < 50 ? 16000 : i < 95 ? 33000 : 2000000;
        pf_present_hit(t);
    }
    pf_present_take(&r);
    CHECK(r.calls == 101 && r.gaps == 100);
    CHECK(r.p50_us == 16667 && r.p95_us == 33334 && r.bins[11] == 5);
    pf_present_hit(t + 2000000);
    pf_present_take(&r);
    CHECK(r.gaps == 1 && !r.p50_us && !r.p95_us && r.bins[11] == 1);
    pf_present_init();
    pf_present_hit(100);
    pf_present_hit(100); /* Zero gap belongs to the first bucket. */
    pf_present_hit(99); /* Reversed clock must not look like fast pacing. */
    pf_present_take(&r);
    CHECK(r.bins[0] == 1 && r.bins[11] == 1 && !r.p95_us);
    pf_present_init();
    pf_present_hit(1);
    pf_present_take(&r);
    CHECK(r.calls == 1 && !r.gaps); /* init resets continuity. */
    puts("presentation gaps: all tests passed");
    return 0;
}
