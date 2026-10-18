/* Offline retained-batch experiment. No game launch or runtime hooks.
 * Reuse the dispatch fixture's actual shaders, instancing and hook path. */
#define PF_DISPATCH_EMBED
#include "bench_dispatch.c"
#include "bench_guard.h"

extern int bench_retain(unsigned);
extern int bench_replay(unsigned, const unsigned char *, const unsigned char *);
extern void bench_retained_clear(void);
extern void bench_retained_kind(int);
extern void bench_retained_counts(unsigned *, unsigned *);
extern void bench_extra_mode(int);

static int retain_now;
static IDirect3DStateBlock9 *outer;
static double pair_completed_ms;
/* Optional full-effect fixture; the original setup-only benchmark keeps
 * these NULL. Callbacks execute inside the measured regular/extra paths. */
static void (*material_setup)(void);
static void (*material_begin)(unsigned);
static void (*material_end)(void);
static void (*pose_adjust)(float *, unsigned);
static void (*validate_retained)(void);
static unsigned (*cached_regular)(void);
static unsigned (*cached_extra)(void);
static unsigned validate_markers;
static void (*validate_accounting)(void);
static int dispatch_only, dispatch_bulk_now;
static int dispatch_bulk_enabled=1, pair_optimized;
static unsigned dispatch_first_member=2;
static void (*frame_mode)(int);
static void (*frame_check)(int);
static const char *comparison_name;
static int (*dispatch_scope_begin)(void),(*dispatch_scope_end)(unsigned);
static void (*prepare_sample)(unsigned, unsigned);
static void (*setup_complete)(void);
static int (*dispatch_available)(void);

static void retained_workload(void)
{
    unsigned group, i;
    for (group = 0; group < GROUPS; group++) {
        /* Distinct synthetic opacity for each group, shared by both paths.
         * A full state block must restore this as well as geometry/buffers. */
        float opacity[4] = {0.5f + (group % 8) / 16.0f, 0, 0, 0};
        if (material_begin) material_begin(group);
        else HR(IDirect3DDevice9_SetVertexShaderConstantF(g_dev, g_v->roles[PF_ROLE_OPAC], opacity, 1));
        current_records = records[group];
        *(unsigned char **)(frame_ptr + PF_FRAME_GROUP_END) = current_records + sizeof records[0];
        for (i = 0; i < GROUP_SIZE;) {
            if(dispatch_bulk_now && i==dispatch_first_member &&
               (!dispatch_available || dispatch_available())) {
                REQUIRE(dispatch_scope_begin && dispatch_scope_end && dispatch_scope_begin());
                unsigned n=bench_inst_bulk(current_records+i*PF_RECORD_SIZE,
                                           current_records+sizeof records[0],&g_method);
                REQUIRE(n==GROUP_SIZE-i && dispatch_scope_end(n));
                i+=n;
                continue;
            }
            one_member(i);
            if (!i && retain_now) REQUIRE(bench_retain(group));
            i++;
        }
        if (material_end) material_end();
    }
}

static void setup(int argc, char **argv)
{
    setup_dispatch(argc, argv);
    if (material_setup) material_setup();
    if (setup_complete) setup_complete();
    pf_timing_set_next(PF_DISPLAY_DRAW, retained_workload);
    HR(IDirect3DDevice9_CreateStateBlock(g_dev, D3DSBT_ALL, &outer));
}

/* Fresh world-space palette positions; no stale-pose replay is permitted. */
static void pose(unsigned phase)
{
    unsigned i, p;
    for (i = 0; i < GROUPS * GROUP_SIZE; i++) {
        unsigned src = i % MESHES;
        for (p = 0; p < PIVOTS; p++) {
            float *dst = (float *)(many_pivots[i] + p * 0x58 + 0x30);
            const float *base = (const float *)(g_pivots[src] + p * 0x58 + 0x30);
            dst[4] = base[4] + (phase ? 0.06f : 0);
            dst[5] = base[5] + (phase ? 0.04f : 0);
            dst[6] = base[6] + (phase ? 0.02f : 0);
            if (pose_adjust) pose_adjust(dst,i);
        }
    }
}

static void frame_draw(int extra, int retained, unsigned *pixels)
{
    unsigned group, draws, meshes, cached_groups=0;
    unsigned before_v[256*4], after_v[256*4], before_p[32*4], after_p[32*4];
    pf_timing_stats before_scopes,after_scopes;
    if(validate_markers) pf_timing_get_stats(&before_scopes);
    bench_extra_mode(extra);
    g_stock_draws = 0;
    HR(IDirect3DDevice9_SetVertexShader(g_dev, g_ea));
    HR(IDirect3DDevice9_SetVertexDeclaration(g_dev, g_decl));
    HR(IDirect3DDevice9_SetStreamSource(g_dev, 0, g_vb, 0, 40));
    HR(IDirect3DDevice9_SetIndices(g_dev, g_ib));
    HR(IDirect3DDevice9_BeginScene(g_dev));
    HR(IDirect3DDevice9_Clear(g_dev, 0, NULL, D3DCLEAR_TARGET |
        (shadow_mode ? D3DCLEAR_ZBUFFER : 0), shadow_mode ? 0xffffffffu : 0, 1, 0));
    if(frame_mode) frame_mode(pair_optimized);
    bench_retained_counts(&draws,&meshes);
    if (extra || cached_regular || frame_mode) {
        HR(IDirect3DStateBlock9_Capture(outer));
        HR(IDirect3DDevice9_GetVertexShaderConstantF(g_dev,0,(float *)before_v,256));
        HR(IDirect3DDevice9_GetPixelShaderConstantF(g_dev,0,(float *)before_p,32));
    }
    if (retained && !dispatch_only) {
        if (cached_extra) cached_groups=cached_extra();
        else {
            for (group = 0; group < GROUPS; group++)
                REQUIRE(bench_replay(group, records[group], records[group] + sizeof records[0]));
            cached_groups=GROUPS;
        }
    } else if (!extra && retain_now && cached_regular) cached_groups=cached_regular();
    else ((void (*)(void))pf_enter_stubs[PF_DISPLAY_DRAW])();
    if (extra || cached_regular || frame_mode) {
        HR(IDirect3DStateBlock9_Apply(outer));
        HR(IDirect3DDevice9_GetVertexShaderConstantF(g_dev,0,(float *)after_v,256));
        HR(IDirect3DDevice9_GetPixelShaderConstantF(g_dev,0,(float *)after_p,32));
        REQUIRE(!memcmp(before_v,after_v,sizeof before_v));
        REQUIRE(!memcmp(before_p,after_p,sizeof before_p));
    }
    HR(IDirect3DDevice9_EndScene(g_dev));
    REQUIRE(g_stock_draws == GROUPS-cached_groups);
    bench_retained_counts(&draws,&meshes);
    REQUIRE(draws==2*cached_groups && meshes==GROUP_SIZE*cached_groups);
    REQUIRE(pf_inst_active());
    check_state();
    if(frame_check) frame_check(extra);
    if(validate_markers) {
        pf_timing_get_stats(&after_scopes);
        REQUIRE(after_scopes.scopes-before_scopes.scopes==(extra?0:GROUPS*GROUP_SIZE));
        REQUIRE(after_scopes.stale_scopes==before_scopes.stale_scopes);
    }
    if (pixels) read_pixels(pixels);
    bench_extra_mode(0);
}

static void pair(int retained, double *regular, double *extra, double *total,
                 unsigned *pixels)
{
    long long a, b, c, d;
    unsigned refs = g_method.refs;
    pose(0);
    drain();
    pair_optimized=retained;
    dispatch_bulk_now=dispatch_only && retained && dispatch_bulk_enabled;
    retain_now = retained && !dispatch_only;
    a = ticks();
    frame_draw(0, 0, NULL);
    b = ticks();
    pose(1); /* Identical simulated pose-production cost on both paths. */
    retain_now = 0;
    c = ticks();
    frame_draw(1, retained, NULL);
    d = ticks();
    drain();
    pair_completed_ms=(ticks()-a)*1000.0/qpf;
    *regular = (b-a) * 1000.0 / qpf;
    *extra = (d-c) * 1000.0 / qpf;
    *total = (d-a) * 1000.0 / qpf;
    REQUIRE(g_method.refs == refs);
    if (pixels) read_pixels(pixels);
}

static int retained_benchmark(int argc, char **argv)
{
    static unsigned a[RT*RT], b[RT*RT], old[RT*RT];
    double regular[2], extra[2], total[2], completed[2], saved[ROUNDS], saved_complete[ROUNDS];
    unsigned r, m, i, covered = 0, moved = 0;
    REQUIRE(bench_no_game());
    setup(argc, argv);
    if(validate_accounting) validate_accounting();
    REQUIRE(!bench_replay(288,records[0],records[0]+sizeof records[0]));
    REQUIRE(!bench_replay(0,records[0],records[0]+sizeof records[0]));
    bench_retained_kind(argc > 4 && !strcmp(argv[4],"narrow"));
    printf("state mask: %s\n", argc > 4 ? argv[4] : "all");
    pf_timing_set_detail(1);
    pair(0, &regular[0], &extra[0], &total[0], a);
    pair(1, &regular[1], &extra[1], &total[1], b);
    {
        unsigned different=0, bad=0, maximum=0, ca=0,cb=0;
        for (i=0; i<RT*RT; i++) {
            unsigned delta=0;
            different += a[i]!=b[i];
            ca += a[i] != (shadow_mode ? PF_DEPTH_CLEAR : 0);
            cb += b[i] != (shadow_mode ? PF_DEPTH_CLEAR : 0);
            if (a[i]!=b[i] && different < 4) printf("PIXEL %u: %08X/%08X\n",i,a[i],b[i]);
            if (shadow_mode) delta=pf_depth_diff(a[i],b[i]);
            else for (unsigned shift=0; shift<32; shift+=8) {
                unsigned x=(a[i]>>shift)&255, y=(b[i]>>shift)&255;
                unsigned d=x>y?x-y:y-x;
                if(d>delta) delta=d;
            }
            if(delta>maximum) maximum=delta;
            bad += delta > (shadow_mode ? PF_DEPTH_TOLERANCE : 2);
        }
        printf("OUTPUT: %u differing, %u over tolerance, maximum %u, covered %u/%u\n",different,bad,maximum,ca,cb);
        REQUIRE(!bad);
    }
    /* Positive control: old bones must produce a different picture/map. */
    pose(0); frame_draw(1,1,old);
    for (i=0; i<RT*RT; i++) {
        covered += shadow_mode ? a[i] != PF_DEPTH_CLEAR : a[i] != 0;
        moved += old[i] != a[i];
    }
    REQUIRE(covered > 100 && moved > 100);
    if (validate_retained) validate_retained();
    printf("RETAINED %s: %u groups/%u meshes, %u covered, %u changed by fresh pose; pixels/depth within tolerance\n",
           shadow_mode?"shadow":"lit", GROUPS, GROUPS*GROUP_SIZE, covered, moved);
    puts("round,baseline_regular_ms,retained_regular_ms,baseline_extra_ms,retained_extra_ms,baseline_pair_ms,retained_pair_ms,saved_pair_ms,baseline_complete_ms,retained_complete_ms,saved_complete_ms");
    for(r=0; r<ROUNDS+WARMUP; r++) {
        REQUIRE(bench_no_game());
        for(i=0; i<2; i++) {
            m=i^(r&1); regular[m]=extra[m]=total[m]=completed[m]=0;
            for(unsigned rep=0; rep<FRAMES_PER_SAMPLE; rep++) {
                if(prepare_sample) prepare_sample(r,rep);
                double x,y,z; pair(m,&x,&y,&z,NULL);
                regular[m]+=x/FRAMES_PER_SAMPLE; extra[m]+=y/FRAMES_PER_SAMPLE; total[m]+=z/FRAMES_PER_SAMPLE;
                completed[m]+=pair_completed_ms/FRAMES_PER_SAMPLE;
            }
        }
        REQUIRE(bench_no_game());
        if(r<WARMUP) continue;
        saved[r-WARMUP]=total[0]-total[1];
        saved_complete[r-WARMUP]=completed[0]-completed[1];
        printf("%u,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",r-WARMUP,
               regular[0],regular[1],extra[0],extra[1],total[0],total[1],total[0]-total[1],
               completed[0],completed[1],completed[0]-completed[1]);
    }
    qsort(saved,ROUNDS,sizeof(double),compare_double);
    qsort(saved_complete,ROUNDS,sizeof(double),compare_double);
    printf("SUMMARY %s %s: net paired savings median %.4f ms (p10 %.4f, p90 %.4f), completion %.4f; %s, no FPS claim\n",
           comparison_name?comparison_name:(dispatch_only?"bulk-only":"retained"),shadow_mode?"shadow":"lit",saved[ROUNDS/2],saved[2],saved[18],saved_complete[ROUNDS/2],
           dispatch_only?"no retained cache":"capture cost included");
    bench_retained_clear();
    REQUIRE(!bench_replay(0,records[0],records[0]+sizeof records[0]));
    IDirect3DStateBlock9_Release(outer);
    IDirect3DQuery9_Release(fence);
    return 0;
}

#ifndef PF_RETAINED_EMBED
int main(int argc, char **argv) { return retained_benchmark(argc, argv); }
#endif
