#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <windows.h>
#include "extraprofile.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static const char *const labels[] = {
    "UpdateShadowMap", "UpdateWaterReflection", "RenderViews", "RenderUI"
};
static int ids[12];

static void tests(void)
{
    pf_ep p;
    pf_ep_row r;
    unsigned i;
    pf_ep_init(&p);
    CHECK(pf_ep_label(0) == -1 && pf_ep_label("") == -1);
    CHECK(pf_ep_label("RenderView") == -1 && pf_ep_label("RenderViewsExtra") == -1);
    for (i = 0; i < 4; i++) CHECK(pf_ep_label(labels[i]) == (int)i);
    pf_ep_marker_begin(&p, ids, labels[0], 0); /* Outside a draw ignored. */
    pf_ep_end(&p, 1, 0);
    pf_ep_begin(&p, 10);
    for (i = 0; i < 4; i++) {
        pf_ep_marker_begin(&p, &ids[i], labels[i], 20 + i * 10);
        CHECK(pf_ep_tracks(&p, &ids[i]));
        pf_ep_marker_begin(&p, &ids[10], "Mesh", 21 + i * 10);
        CHECK(!pf_ep_tracks(&p, &ids[10]));
        pf_ep_marker_end(&p, &ids[10], 22 + i * 10);
        pf_ep_marker_end(&p, &ids[i], 25 + i * 10);
    }
    pf_ep_end(&p, 70, 1);
    pf_ep_take(&p, &r);
    CHECK(r.valid == 1 && r.invalid == 0 && r.total_us == 60);
    for (i = 0; i < 4; i++) CHECK(r.samples[i] == 1 && r.us[i] == 5);
    pf_ep_take(&p, &r);
    CHECK(!r.valid && !r.invalid && !r.total_us && !r.samples[0]);

    /* Same-pass nesting counts only the outer interval. Different passes
     * remain inclusive and must not be summed as disjoint frame costs. */
    pf_ep_begin(&p, 0);
    pf_ep_marker_begin(&p, ids, labels[0], 2);
    pf_ep_marker_begin(&p, ids + 1, labels[2], 3);
    pf_ep_marker_begin(&p, ids + 2, labels[0], 4);
    pf_ep_marker_end(&p, ids + 2, 6);
    pf_ep_marker_end(&p, ids + 1, 8);
    pf_ep_marker_end(&p, ids, 12);
    pf_ep_end(&p, 15, 1);
    pf_ep_take(&p, &r);
    CHECK(r.valid == 1 && r.us[0] == 10 && r.samples[0] == 1);
    CHECK(r.us[2] == 5 && r.samples[2] == 1 && r.total_us == 15);

    /* Any error discards even previously completed samples. */
    for (i = 0; i < 5; i++) {
        unsigned j;
        pf_ep_begin(&p, 0);
        pf_ep_marker_begin(&p, ids, labels[0], 1);
        pf_ep_marker_end(&p, ids, 2);
        if (i == 0) { /* Out of order recognized End. */
            pf_ep_marker_begin(&p, ids, labels[0], 3);
            pf_ep_marker_begin(&p, ids + 1, labels[1], 4);
            pf_ep_marker_end(&p, ids, 5);
        } else if (i == 1) { /* Abandoned recognized scope. */
            pf_ep_marker_begin(&p, ids, labels[0], 3);
        } else if (i == 2) { /* Bounded stack overflow. */
            for (j = 0; j < 9; j++)
                pf_ep_marker_begin(&p, ids + j, labels[0], 3 + j);
            CHECK(p.depth == PF_EP_DEPTH && !pf_ep_tracks(&p, ids));
        } else if (i == 3) { /* Duplicate scope identity. */
            pf_ep_marker_begin(&p, ids, labels[0], 3);
            pf_ep_marker_begin(&p, ids, labels[1], 4);
        }
        pf_ep_end(&p, 20, i != 4); /* Safety failure after balanced scopes. */
        pf_ep_take(&p, &r);
        CHECK(!r.valid && r.invalid == 1 && !r.total_us && !r.samples[0] && !r.us[0]);
        CHECK(!p.active && !p.depth && !pf_ep_tracks(&p, ids));
    }
    /* Begin abandons the prior draw; take does not disturb the new draw. */
    pf_ep_begin(&p, 1);
    pf_ep_marker_begin(&p, ids, labels[0], 2);
    pf_ep_begin(&p, UINT_MAX - 10);
    pf_ep_marker_begin(&p, ids, labels[0], UINT_MAX - 5);
    pf_ep_take(&p, &r);
    CHECK(r.invalid == 1 && !r.valid && pf_ep_tracks(&p, ids));
    pf_ep_marker_end(&p, ids, 4);
    pf_ep_end(&p, 9, 1);
    pf_ep_end(&p, 10, 1); /* Double exit is inert. */
    pf_ep_take(&p, &r);
    CHECK(r.valid == 1 && r.invalid == 0 && r.total_us == 20);
    CHECK(r.samples[0] == 1 && r.us[0] == 10 && !r.samples[1]);
    /* Exactly eight nested entries fit and recover after prior errors. */
    pf_ep_begin(&p, 0);
    for (i = 0; i < 8; i++) pf_ep_marker_begin(&p, ids + i, labels[0], i);
    for (i = 8; i; i--) pf_ep_marker_end(&p, ids + i - 1, 16 - i);
    pf_ep_end(&p, 20, 1);
    pf_ep_take(&p, &r);
    CHECK(r.valid == 1 && !r.invalid && r.samples[0] == 1 && r.us[0] == 15);
}

static void benchmark(void)
{
    const unsigned frames = 1000, markers = 7000;
    static const char *const unknown[] = {"Mesh", "RenderOneObject", "RenderBatch", "Update"};
    LARGE_INTEGER freq, begin, end;
    pf_ep p;
    pf_ep_row r;
    unsigned f, j;
    unsigned long long elapsed;
    CHECK(QueryPerformanceFrequency(&freq) && QueryPerformanceCounter(&begin));
    pf_ep_init(&p);
    for (f = 0; f < frames; f++) {
        pf_ep_begin(&p, 0);
        pf_ep_marker_begin(&p, ids, labels[2], 1);
        for (j = 0; j < markers; j++) {
            /* Integration gates the clock with these lookups. */
            if (pf_ep_label(unknown[j % 4]) >= 0)
                pf_ep_marker_begin(&p, ids + 1, unknown[j % 4], 2);
            if (pf_ep_tracks(&p, ids + 1)) pf_ep_marker_end(&p, ids + 1, 3);
        }
        pf_ep_marker_end(&p, ids, 4);
        pf_ep_end(&p, 5, 1);
    }
    CHECK(QueryPerformanceCounter(&end));
    pf_ep_take(&p, &r);
    CHECK(r.valid == frames && r.samples[2] == frames);
    elapsed = (unsigned long long)(end.QuadPart - begin.QuadPart) * 1000000 / freq.QuadPart;
    printf("extra profiler benchmark: %u unknown markers/frame, %u frames, %llu us total, %llu us/frame (informational)\n",
           markers, frames, elapsed, elapsed / frames);
}

int main(void)
{
    tests();
    benchmark();
    puts("OK: extra pass labels, nesting, malformed/abandoned/failed draws, bounded stack, clock wrap, row drain");
    return 0;
}
