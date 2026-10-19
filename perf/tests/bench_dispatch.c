/* Offline dispatch screening, not a saved-game replay or an installable
 * optimization. Reuse the existing GPU correctness fixture and real plugin
 * code. The outer game loop/event are independently modeled here, using the
 * local game's CRT string functions; no EA code or asset is embedded. */
#define main fixture_main
#include "test_inst.c"
#undef main
#include "timing.h"

#define GROUP_SIZE 25
#define GROUPS 288
#define ROUNDS 21
#define WARMUP 4
#define FRAMES_PER_SAMPLE 8

extern unsigned bench_inst_bulk(const unsigned char *, const unsigned char *, void *);
static unsigned char records[GROUPS][GROUP_SIZE * PF_RECORD_SIZE];
static unsigned char *current_records;
/* Separate identities/palettes span several MB instead of repeating nine
 * hot fixture meshes. They still share one synthetic model and material. */
static fake_mesh many_meshes[GROUPS * GROUP_SIZE];
static fake_container many_containers[GROUPS * GROUP_SIZE];
static unsigned char many_trees[GROUPS * GROUP_SIZE][0x18];
static unsigned char many_pivots[GROUPS * GROUP_SIZE][PIVOTS * 0x58];
static unsigned char frame[0x60];
static unsigned char *const frame_ptr = frame + 0x40;
static char *(__cdecl *local_copy)(char *, const char *, size_t);
static unsigned char *(__cdecl *local_mbcopy)(unsigned char *, const unsigned char *);
static unsigned char *(__cdecl *local_mbcat)(unsigned char *, const unsigned char *);
static IDirect3DQuery9 *fence;
static double qpf;
static int bulk_mode;
static int shadow_mode;
static unsigned bulk_members;

#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "benchmark refused: %s:%d: %s\n", \
    __FILE__, __LINE__, #c); exit(1); } } while (0)
#define HR(c) REQUIRE(SUCCEEDED(c))

static unsigned fixture_batches(unsigned meshes)
{
    unsigned capacity=g_v->ir_count/(2*BONES);
    REQUIRE(capacity>=2 && BONES<=g_v->ea_bones);
    return (meshes+capacity-1)/capacity;
}

/* Real entry stub register/stack contract, with synthetic game records. */
extern void bench_mesh(fake_mesh *, void *, void *, void *, fake_method *);
__asm__(".text\n"
        "_bench_mesh:\n"
        "pushl %ebp\n pushl %ebx\n pushl %edi\n"
        "movl 16(%esp), %ecx\n movl 20(%esp), %ebx\n"
        "movl 24(%esp), %edi\n movl 28(%esp), %ebp\n"
        "movl 32(%esp), %eax\n pushl $1\n pushl %eax\n"
        "call *_pf_enter_stubs+64\n"
        "popl %edi\n popl %ebx\n popl %ebp\n ret\n");
_Static_assert(PF_INST_MESH == 16, "assembly stub index");

typedef void (__attribute__((thiscall)) *event_begin_fn)(void *, const char *, const char *, unsigned);
typedef void (__attribute__((thiscall)) *event_end_fn)(void *);

/* Models the inspected constructor's bounded copies with PIX callbacks
 * disabled. The original call/SEH/GetModelName instructions are not replayed. */
static void __attribute__((thiscall)) event_begin(void *self, const char *label,
                                                 const char *group, unsigned unused)
{
    char *s = self;
    (void)unused;
    local_copy(s, label, 256);
    s[255] = 0;
    memcpy(s + 256, "SceneAnalyst", 13);
    local_copy(s + 268, group, 52);
    s[319] = 0;
}
static void __attribute__((thiscall)) event_end(void *self) { (void)self; }

static void one_member(unsigned i)
{
    char label[256], object[320];
    local_mbcopy((unsigned char *)label, (const unsigned char *)"Rendering mesh\tFXShader\t");
    local_mbcat((unsigned char *)label, (const unsigned char *)"BENCH_SYNTHETIC_UNIT_MODEL");
    ((event_begin_fn)pf_enter_stubs[PF_EVENT_BEGIN])(object, label, "MeshFXShader", 0);
    g_method.refs++;
    bench_mesh(*(fake_mesh **)(current_records + i * PF_RECORD_SIZE), current_records,
               current_records + i * PF_RECORD_SIZE, frame_ptr, &g_method);
    ((event_end_fn)pf_enter_stubs[PF_EVENT_END])(object);
}

static void render_workload(void)
{
    unsigned group, i;
    for (group = 0; group < GROUPS; group++) {
        current_records = records[group];
        *(unsigned char **)(frame_ptr + PF_FRAME_GROUP_END) = current_records + sizeof records[0];
        for (i = 0; i < GROUP_SIZE;) {
            unsigned consumed = 0;
            if (bulk_mode && i == 2)
                consumed = bench_inst_bulk(current_records + i * PF_RECORD_SIZE,
                                          current_records + sizeof records[0], &g_method);
            if (consumed)
                i += consumed, bulk_members += consumed;
            else
                one_member(i++);
        }
    }
}

static long long ticks(void)
{
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

static void drain(void)
{
    HRESULT hr;
    ULONGLONG timeout = GetTickCount64() + 10000;
    HR(IDirect3DQuery9_Issue(fence, D3DISSUE_END));
    do {
        hr = IDirect3DQuery9_GetData(fence, NULL, 0, D3DGETDATA_FLUSH);
        REQUIRE(hr == S_OK || hr == S_FALSE);
        REQUIRE(GetTickCount64() < timeout);
        if (hr == S_FALSE) SwitchToThread();
    } while (hr == S_FALSE);
}

static void read_pixels(unsigned *pixels)
{
    unsigned y;
    D3DLOCKED_RECT lr;
    HR(IDirect3DDevice9_GetRenderTargetData(g_dev, g_rt, g_sys));
    HR(IDirect3DSurface9_LockRect(g_sys, &lr, NULL, D3DLOCK_READONLY));
    for (y = 0; y < RT; y++)
        memcpy(pixels + y * RT, (char *)lr.pBits + y * lr.Pitch, RT * 4);
    HR(IDirect3DSurface9_UnlockRect(g_sys));
}

static void check_state(void)
{
    IDirect3DVertexShader9 *vs = NULL;
    IDirect3DVertexDeclaration9 *decl = NULL;
    IDirect3DVertexBuffer9 *vb = NULL;
    UINT f0, f1, offset, stride;
    HR(IDirect3DDevice9_GetVertexShader(g_dev, &vs));
    HR(IDirect3DDevice9_GetVertexDeclaration(g_dev, &decl));
    HR(IDirect3DDevice9_GetStreamSourceFreq(g_dev, 0, &f0));
    HR(IDirect3DDevice9_GetStreamSourceFreq(g_dev, 1, &f1));
    HR(IDirect3DDevice9_GetStreamSource(g_dev, 0, &vb, &offset, &stride));
    REQUIRE(vs == g_ea && decl == g_decl && vb == g_vb && !offset && stride == 40);
    REQUIRE(f0 == 1 && f1 == 1 && pf_inst_active());
    IDirect3DVertexShader9_Release(vs);
    IDirect3DVertexDeclaration9_Release(decl);
    IDirect3DVertexBuffer9_Release(vb);
}

static void sample(int bulk, int detail, double *submit, double *complete, unsigned *pixels)
{
    long long start, submitted, done;
    unsigned refs = g_method.refs;
    pf_inst_counts c;
    pf_timing_stats before, after;
    bulk_mode = bulk;
    pf_timing_set_detail(detail);
    pf_inst_take(&c);
    pf_timing_get_stats(&before);
    g_stock_draws = 0;
    bulk_members = 0;
    HR(IDirect3DDevice9_SetVertexShader(g_dev, g_ea));
    HR(IDirect3DDevice9_SetVertexDeclaration(g_dev, g_decl));
    HR(IDirect3DDevice9_SetStreamSource(g_dev, 0, g_vb, 0, 40));
    HR(IDirect3DDevice9_SetIndices(g_dev, g_ib));
    drain();
    start = ticks();
    HR(IDirect3DDevice9_BeginScene(g_dev));
    HR(IDirect3DDevice9_Clear(g_dev, 0, NULL,
        D3DCLEAR_TARGET | (shadow_mode ? D3DCLEAR_ZBUFFER : 0),
        shadow_mode ? 0xFFFFFFFFu : 0, 1.0f, 0));
    ((void (*)(void))pf_enter_stubs[PF_DISPLAY_DRAW])();
    HR(IDirect3DDevice9_EndScene(g_dev));
    submitted = ticks();
    drain();
    done = ticks();
    *submit = (submitted - start) * 1000.0 / qpf;
    *complete = (done - start) * 1000.0 / qpf;
    pf_inst_take(&c);
    pf_timing_get_stats(&after);
    REQUIRE(g_method.refs == refs && g_stock_draws == GROUPS);
    REQUIRE(bulk_members == (bulk ? GROUPS * (GROUP_SIZE - 2) : 0));
    REQUIRE(c.meshes == GROUPS * (GROUP_SIZE - 1) && c.groups == GROUPS && c.leaders == GROUPS);
    REQUIRE(c.draws == GROUPS * fixture_batches(GROUP_SIZE-1) && c.mismatch == 0 && c.verify == 0);
    REQUIRE(SH(c, meshes) == (shadow_mode ? c.meshes : 0));
    REQUIRE(after.scopes - before.scopes == (detail ? GROUPS * (bulk ? 2 : GROUP_SIZE) : 0));
    REQUIRE(after.stale_scopes == before.stale_scopes);
    check_state();
    if (pixels) read_pixels(pixels);
}

static int compare_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void setup_dispatch(int argc, char **argv)
{
    static pf_site sites[PF_SITE_COUNT];
    HMODULE crt;
    char backend[MAX_PATH];
    LARGE_INTEGER freq;
    unsigned i;
    const char *crt_path = argc > 2 ? argv[2] : "C:/Jogos/AgeoftheRing/rotwk/msvcr71.dll";
    shadow_mode = argc > 3 && !strcmp(argv[3], "shadow");
    REQUIRE(fixture_main(argc, argv) == 0);
    REQUIRE(g_dev && g_ea && g_decl && g_rt && g_sys && g_vb && g_ib && !g_failures);
    REQUIRE(GetModuleFileNameA(GetModuleHandleA("d3d9.dll"), backend, sizeof backend));
    printf("D3D9 backend: %s\n", backend);
    crt = LoadLibraryA(crt_path);
    REQUIRE(crt);
    local_copy = (void *)GetProcAddress(crt, "strncpy");
    local_mbcopy = (void *)GetProcAddress(crt, "_mbscpy");
    local_mbcat = (void *)GetProcAddress(crt, "_mbscat");
    REQUIRE(local_copy && local_mbcopy && local_mbcat);
    QueryPerformanceFrequency(&freq);
    qpf = (double)freq.QuadPart;
    HR(IDirect3DDevice9_CreateQuery(g_dev, D3DQUERYTYPE_EVENT, &fence));
    sites[PF_EVENT_BEGIN].probe = sites[PF_EVENT_END].probe = 1;
    sites[PF_EVENT_BEGIN].arg_bytes = 12;
    sites[PF_INST_MESH].arg_bytes = 8;
#ifdef PF_RUNTIME_BULK
    /* The live installer uses a Draw entry probe and an indirect DIP call.
     * A zero-initialized definition wraps Draw's return and substitutes a
     * fixed DIP target instead; both hide differences in the native ABI. */
    const unsigned native_sites[] = {PF_EVENT_BEGIN, PF_EVENT_END, PF_DRAW,
                                     PF_INST_MESH, PF_INST_DIP};
    for (unsigned k = 0; k < sizeof native_sites / sizeof native_sites[0]; k++)
        sites[native_sites[k]] = pf_sites[native_sites[k]];
#endif
    REQUIRE(pf_timing_open(sites, NULL, NULL));
    pf_timing_set_next(PF_EVENT_BEGIN, event_begin);
    pf_timing_set_next(PF_EVENT_END, event_end);
    pf_timing_set_next(PF_INST_MESH, fake_render);
    pf_timing_set_next(PF_DISPLAY_DRAW, render_workload);
    pf_timing_set_inst(pf_inst_enter, NULL);
    pf_inst_init(0);
    pf_inst_set_shadow(NULL, 1);
    build_scene();
    setup_constants();
    HR(IDirect3DDevice9_SetRenderTarget(g_dev, 0, g_rt));
    IDirect3DPixelShader9 *lit_ps = diffuse_ps();
    REQUIRE(lit_ps);
    HR(IDirect3DDevice9_SetPixelShader(g_dev, lit_ps));
    IDirect3DPixelShader9_Release(lit_ps);
    HR(IDirect3DDevice9_SetRenderState(g_dev, D3DRS_ZENABLE, FALSE));
    HR(IDirect3DDevice9_SetRenderState(g_dev, D3DRS_CULLMODE, D3DCULL_NONE));
    if (shadow_mode) {
        const unsigned char *code;
        unsigned size = 0;
        IDirect3DPixelShader9 *ps = NULL;
        for (i = 0; i < PF_INST_VARIANTS; i++)
            if (pf_inst_variants[i].ea_index == 5) g_v = &pf_inst_variants[i];
        REQUIRE(g_v->family == 3);
        code = find_shader(2, &size);
        REQUIRE(code && size == 208);
        IDirect3DVertexShader9_Release(g_ea); g_ea = NULL;
        IDirect3DSurface9_Release(g_rt); g_rt = NULL;
        IDirect3DSurface9_Release(g_sys); g_sys = NULL;
        HR(IDirect3DDevice9_CreateVertexShader(g_dev, (const DWORD *)find_ea(g_v), &g_ea));
        HR(IDirect3DDevice9_CreatePixelShader(g_dev, (const DWORD *)code, &ps));
        HR(IDirect3DDevice9_CreateRenderTarget(g_dev, RT, RT, D3DFMT_R32F,
            D3DMULTISAMPLE_NONE, 0, FALSE, &g_rt, NULL));
        HR(IDirect3DDevice9_CreateOffscreenPlainSurface(g_dev, RT, RT, D3DFMT_R32F,
            D3DPOOL_SYSTEMMEM, &g_sys, NULL));
        HR(IDirect3DDevice9_CreateDepthStencilSurface(g_dev, RT, RT, D3DFMT_D24S8,
            D3DMULTISAMPLE_NONE, 0, TRUE, &g_sds, NULL));
        HR(IDirect3DDevice9_SetRenderTarget(g_dev, 0, g_rt));
        HR(IDirect3DDevice9_SetDepthStencilSurface(g_dev, g_sds));
        HR(IDirect3DDevice9_SetPixelShader(g_dev, ps));
        IDirect3DPixelShader9_Release(ps);
        HR(IDirect3DDevice9_SetRenderState(g_dev, D3DRS_ZENABLE, TRUE));
        HR(IDirect3DDevice9_SetRenderState(g_dev, D3DRS_ZWRITEENABLE, TRUE));
        HR(IDirect3DDevice9_SetRenderState(g_dev, D3DRS_ZFUNC, D3DCMP_LESSEQUAL));
        setup_shadow_constants();
        g_shadow_flag = 1;
        pf_inst_set_shadow(&g_shadow_flag, 1);
    }
    for (i = 0; i < GROUP_SIZE * GROUPS; i++) {
        unsigned src = i % MESHES;
        many_meshes[i] = g_mesh[src];
        many_containers[i] = g_cont[src];
        memcpy(many_trees[i], g_htree[src], sizeof many_trees[i]);
        memcpy(many_pivots[i], g_pivots[src], sizeof many_pivots[i]);
        *(fake_container **)(many_meshes[i].b + PF_MESH_CONTAINER) = &many_containers[i];
        many_containers[i].htree = many_trees[i];
        *(unsigned char **)(many_trees[i] + 0x14) = many_pivots[i];
        *(fake_mesh **)(records[i / GROUP_SIZE] + (i % GROUP_SIZE) * PF_RECORD_SIZE) = &many_meshes[i];
    }
    *(int *)(frame_ptr + PF_FRAME_PASS_COUNT) = 1;
    printf("PALETTE: %u bones, %u registers per instance, %u instances per batch\n",
           (unsigned)BONES,(unsigned)(2*BONES),g_v->ir_count/(2*BONES));
    *(int *)(frame_ptr + PF_FRAME_MAX_INSTANCES) = 1;
}

#ifndef PF_DISPATCH_EMBED
int main(int argc, char **argv)
{
    static unsigned a[RT * RT], b[RT * RT];
    unsigned i, r, detail, covered = 0;
    double s[2], c[2];
    setup_dispatch(argc, argv);
    sample(0, 1, &s[0], &c[0], a);
    sample(1, 1, &s[1], &c[1], b);
    REQUIRE(!memcmp(a, b, sizeof a));
    for (i = 0; i < RT * RT; i++)
        covered += shadow_mode ? a[i] != 0x3F800000u : a[i] != 0;
    REQUIRE(covered > 100);
    printf("BENCH %s: %u groups, %u members, %u instanced meshes; pixels/state/refs/counts match (%u covered pixels)\n",
           shadow_mode ? "shadow" : "lit", GROUPS, GROUPS * GROUP_SIZE, GROUPS * (GROUP_SIZE - 1), covered);
    printf("Object working set: %u bytes; six bones, one material; no gameplay/FPS claim\n",
           (unsigned)(sizeof records + sizeof many_meshes + sizeof many_containers + sizeof many_trees + sizeof many_pivots));
    puts("detail,round,current_submit_ms,bulk_submit_ms,saved_submit_ms,current_complete_ms,bulk_complete_ms,saved_complete_ms");
    printf("Each sample averages %u separately fenced workloads; AB/BA order alternates\n", FRAMES_PER_SAMPLE);
    for (detail = 0; detail <= 1; detail++) {
        double ds[ROUNDS], dc[ROUNDS], cs[ROUNDS], bs[ROUNDS];
        for (r = 0; r < ROUNDS + WARMUP; r++) {
            /* Alternate AB/BA to reduce order/thermal bias. */
            for (i = 0; i < 2; i++) {
                unsigned mode = i ^ (r & 1);
                unsigned rep;
                s[mode] = c[mode] = 0;
                for (rep = 0; rep < FRAMES_PER_SAMPLE; rep++) {
                    double sub, done;
                    sample(mode, detail, &sub, &done, NULL);
                    s[mode] += sub / FRAMES_PER_SAMPLE;
                    c[mode] += done / FRAMES_PER_SAMPLE;
                }
            }
            if (r < WARMUP) continue;
            i = r - WARMUP;
            cs[i] = s[0]; bs[i] = s[1]; ds[i] = s[0] - s[1]; dc[i] = c[0] - c[1];
            printf("%u,%u,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", detail, i,
                   s[0], s[1], ds[i], c[0], c[1], dc[i]);
        }
        qsort(ds, ROUNDS, sizeof(double), compare_double);
        qsort(dc, ROUNDS, sizeof(double), compare_double);
        qsort(cs, ROUNDS, sizeof(double), compare_double);
        qsort(bs, ROUNDS, sizeof(double), compare_double);
        printf("SUMMARY detail=%u submit medians current=%.4f bulk=%.4f; paired saving median=%.4f p10=%.4f p90=%.4f ms; completion saving median=%.4f ms\n",
               detail, cs[ROUNDS/2], bs[ROUNDS/2], ds[ROUNDS/2], ds[2], ds[18], dc[ROUNDS/2]);
    }
    return 0;
}
#endif
