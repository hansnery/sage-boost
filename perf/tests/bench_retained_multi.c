/* Offline amortization screen; no runtime hooks or game entry point. */
#ifndef PF_MULTI_EMBED
#define PF_RETAINED_EMBED
#include "bench_retained.c"
#endif

static void (*multi_validate)(void);

static void multi_pose(unsigned phase)
{
    pose(0);
    for (unsigned i = 0; i < GROUPS * GROUP_SIZE; i++)
        for (unsigned p = 0; p < PIVOTS; p++) {
            float *dst = (float *)(many_pivots[i] + p * 0x58 + 0x30);
            dst[4] += 0.02f * phase;
            dst[5] += (0.04f / 3.0f) * phase;
            dst[6] += (0.02f / 3.0f) * phase;
        }
}

typedef struct { double regular, extras, cycle, complete; } multi_time;

static multi_time multi_cycle(unsigned extras, int retained, unsigned (*pixels)[RT*RT])
{
    multi_time result = {0};
    unsigned refs = g_method.refs;
    multi_pose(0);
    drain();
    pair_optimized = retained;
    dispatch_bulk_now = !retained && baseline_bulk_enabled;
    retain_now = retained;
    long long start = ticks();
    frame_draw(0, 0, NULL);
    result.regular = (ticks() - start) * 1000.0 / qpf;
    retain_now = 0;
    for (unsigned e = 0; e < extras; e++) {
        multi_pose(e + 1);
        long long before = ticks();
        frame_draw(1, retained, pixels ? pixels[e] : NULL);
        result.extras += (ticks() - before) * 1000.0 / qpf;
    }
    result.cycle = (ticks() - start) * 1000.0 / qpf;
    drain();
    result.complete = (ticks() - start) * 1000.0 / qpf;
    REQUIRE(g_method.refs == refs);
    return result;
}

static unsigned compare_pixels(const unsigned *a, const unsigned *b, int matching)
{
    unsigned different = 0, bad = 0, covered = 0;
    for (unsigned i = 0; i < RT*RT; i++) {
        unsigned delta = 0;
        different += a[i] != b[i];
        covered += a[i] != (shadow_mode ? PF_DEPTH_CLEAR : 0);
        if (shadow_mode) delta = pf_depth_diff(a[i], b[i]);
        else for (unsigned shift = 0; shift < 32; shift += 8) {
            unsigned x = (a[i] >> shift) & 255, y = (b[i] >> shift) & 255;
            unsigned d = x > y ? x-y : y-x;
            if (d > delta) delta = d;
        }
        bad += delta > (shadow_mode ? PF_DEPTH_TOLERANCE : 2);
    }
    REQUIRE(covered > 100);
    if (matching) REQUIRE(!bad);
    else REQUIRE(bad > 100); /* reject stale poses beyond ordinary tolerance */
    return different;
}

static int multi_benchmark(int argc, char **argv)
{
    static unsigned baseline[3][RT*RT], replay[3][RT*RT], stale[RT*RT];
    REQUIRE(bench_no_game());
    setup(argc, argv);
    bench_retained_kind(1);
    pf_timing_set_detail(1);
    REQUIRE(!bench_replay(GROUPS, records[0], records[0]+sizeof records[0]));
    REQUIRE(!bench_replay(0, records[0], records[0]+sizeof records[0]));
    if (multi_validate) multi_validate();
    multi_cycle(3, 0, baseline);
    multi_cycle(3, 1, replay);
    for (unsigned e = 0; e < 3; e++) {
        unsigned changed = compare_pixels(baseline[e], replay[e], 1);
        multi_pose(e); /* preceding pose must fail, for every extra */
        frame_draw(1, 1, stale);
        unsigned stale_changed = compare_pixels(baseline[e], stale, 0);
        printf("MULTI_CONTROL,phase,%u,different,%u,stale_changed,%u\n", e+1, changed, stale_changed);
    }
    puts("MULTI_COLUMNS,extras,round,base_regular,retained_regular,base_extras,retained_extras,base_cycle,retained_cycle,base_complete,retained_complete");
    for (unsigned extras = 1; extras <= 3; extras += 2) {
        double savings[ROUNDS], completion[ROUNDS];
        for (unsigned r = 0; r < ROUNDS+WARMUP; r++) {
            multi_time times[2] = {0};
            REQUIRE(bench_no_game());
            for (unsigned i = 0; i < 2; i++) {
                unsigned m = i ^ (r & 1);
                for (unsigned rep = 0; rep < FRAMES_PER_SAMPLE; rep++) {
                    if (prepare_sample) prepare_sample(r, rep);
                    multi_time t = multi_cycle(extras, m, NULL);
                    times[m].regular += t.regular / FRAMES_PER_SAMPLE;
                    times[m].extras += t.extras / FRAMES_PER_SAMPLE;
                    times[m].cycle += t.cycle / FRAMES_PER_SAMPLE;
                    times[m].complete += t.complete / FRAMES_PER_SAMPLE;
                }
            }
            REQUIRE(bench_no_game());
            if (r < WARMUP) continue;
            unsigned at = r-WARMUP;
            savings[at] = times[0].cycle-times[1].cycle;
            completion[at] = times[0].complete-times[1].complete;
            printf("MULTI_SAMPLE,%u,%u,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",extras,at,
                   times[0].regular,times[1].regular,times[0].extras,times[1].extras,
                   times[0].cycle,times[1].cycle,times[0].complete,times[1].complete);
        }
        qsort(savings, ROUNDS, sizeof(double), compare_double);
        qsort(completion, ROUNDS, sizeof(double), compare_double);
        printf("MULTI_SUMMARY,extras,%u,cpu_saved,%.4f,completion_saved,%.4f,completion_p10,%.4f\n",
               extras,savings[ROUNDS/2],completion[ROUNDS/2],completion[2]);
    }
    bench_retained_clear();
    REQUIRE(!bench_replay(0, records[0], records[0]+sizeof records[0]));
    IDirect3DStateBlock9_Release(outer);
    IDirect3DQuery9_Release(fence);
    return 0;
}

#ifndef PF_MULTI_EMBED
int main(int argc, char **argv) { return multi_benchmark(argc, argv); }
#endif
