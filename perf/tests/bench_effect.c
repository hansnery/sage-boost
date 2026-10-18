/* Offline D3DX runtime / state-saving screen. Never linked into the ASI.
 * No game executable is loaded; local compiled effects remain in memory. */
#define main shadow_fixture_main
#include "shadow_fx/shadow_fx.c"
#undef main

#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "refused %s:%d: %s\n", \
    __FILE__, __LINE__, #c); exit(1); } } while (0)
#define HR(c) REQUIRE(SUCCEEDED(c))
#define GROUPS 668
#define REPEATS 12
#define SAMPLES 15

static double frequency;
static IDirect3DDevice9 *device;
static IDirect3DQuery9 *fence;

static long long ticks(void)
{
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

static void drain(void)
{
    HRESULT hr;
    ULONGLONG until = GetTickCount64() + 10000;
    HR(IDirect3DQuery9_Issue(fence, D3DISSUE_END));
    do {
        hr = IDirect3DQuery9_GetData(fence, NULL, 0, D3DGETDATA_FLUSH);
        REQUIRE(hr == S_OK || hr == S_FALSE);
        REQUIRE(GetTickCount64() < until);
        if (hr == S_FALSE) SwitchToThread();
    } while (hr == S_FALSE);
}

static unsigned shader_hash(void *shader, int pixel)
{
    unsigned char code[32768];
    UINT n = 0;
    if (!shader) return 0;
    /* Query the exact size first. Do not hash unused stack capacity when
     * a backend leaves the supplied capacity unchanged on the second call. */
    if (pixel) HR(IDirect3DPixelShader9_GetFunction((IDirect3DPixelShader9 *)shader, NULL, &n));
    else HR(IDirect3DVertexShader9_GetFunction((IDirect3DVertexShader9 *)shader, NULL, &n));
    REQUIRE(n && n <= sizeof code);
    memset(code, 0, sizeof code);
    if (pixel) HR(IDirect3DPixelShader9_GetFunction((IDirect3DPixelShader9 *)shader, code, &n));
    else HR(IDirect3DVertexShader9_GetFunction((IDirect3DVertexShader9 *)shader, code, &n));
    REQUIRE(n <= sizeof code);
    return fnv1a(code, n);
}

static HRESULT WINAPI normalized_vs(ID3DXEffectStateManager *self, IDirect3DVertexShader9 *s)
{
    (void)self;
    put(11); put(shader_hash(s, 0)); return S_OK;
}
static HRESULT WINAPI normalized_ps(ID3DXEffectStateManager *self, IDirect3DPixelShader9 *s)
{
    (void)self;
    put(15); put(shader_hash(s, 1)); return S_OK;
}

typedef struct {
    D3DXHANDLE handle[2];
    unsigned bytes;
    unsigned char *data[2];
} input;

static unsigned collect_inputs(ID3DXEffect *fx[2], D3DXHANDLE tech[2], input *inputs,
                                unsigned cap)
{
    D3DXEFFECT_DESC desc[2];
    unsigned i, n = 0;
    HR(FXCALL(fx[0], GetDesc, &desc[0])); HR(FXCALL(fx[1], GetDesc, &desc[1]));
    REQUIRE(desc[0].Parameters == desc[1].Parameters);
    for (i = 0; i < desc[0].Parameters; i++) {
        D3DXPARAMETER_DESC d[2];
        D3DXHANDLE h[2];
        unsigned mode, k;
        int used[2];
        for (mode = 0; mode < 2; mode++) {
            h[mode] = FXCALL(fx[mode], GetParameter, NULL, i);
            HR(FXCALL(fx[mode], GetParameterDesc, h[mode], &d[mode]));
            used[mode] = FXCALL(fx[mode], IsParameterUsed, h[mode], tech[mode]);
        }
        REQUIRE(!strcmp(d[0].Name, d[1].Name) && d[0].Class == d[1].Class &&
            d[0].Type == d[1].Type && d[0].Bytes == d[1].Bytes && used[0] == used[1]);
        if (!used[0] || d[0].Class == D3DXPC_OBJECT || !d[0].Bytes) continue;
        REQUIRE(n < cap && d[0].Bytes <= 16384 && !(d[0].Bytes % 4));
        inputs[n].bytes = d[0].Bytes;
        inputs[n].handle[0] = h[0]; inputs[n].handle[1] = h[1];
        for (mode = 0; mode < 2; mode++) {
            inputs[n].data[mode] = calloc(1, d[0].Bytes);
            REQUIRE(inputs[n].data[mode]);
            for (k = 0; k < d[0].Bytes / 4; k++) {
                float value = mode ? 0.75f + 0.01f * (k % 7) : 0.25f + 0.02f * (k % 5);
                unsigned integer = 1;
                if (d[0].Type == D3DXPT_BOOL || d[0].Type == D3DXPT_INT)
                    memcpy(inputs[n].data[mode] + k * 4, &integer, 4);
                else memcpy(inputs[n].data[mode] + k * 4, &value, 4);
            }
        }
        n++;
    }
    return n;
}

static void set_inputs(ID3DXEffect *fx, unsigned runtime, input *inputs, unsigned n, unsigned variant)
{
    unsigned i;
    for (i = 0; i < n; i++)
        HR(FXCALL(fx, SetValue, inputs[i].handle[runtime], inputs[i].data[variant], inputs[i].bytes));
}

static void cycle(ID3DXEffect *fx, unsigned flags)
{
    UINT passes = 0, p;
    HR(FXCALL(fx, Begin, &passes, flags)); REQUIRE(passes && passes <= 8);
    for (p = 0; p < passes; p++) {
        HR(FXCALL(fx, BeginPass, p));
        HR(FXCALL(fx, CommitChanges));
        HR(FXCALL(fx, EndPass));
    }
    HR(FXCALL(fx, End));
}

/* Classify differences without storing shader bytes or constant payloads. */
static void describe_difference(const unsigned *a, const unsigned *b, unsigned n)
{
    unsigned at = 0, floats = 0, other = 0, max_ulp = 0;
    double max_abs = 0;
    while (at < n) {
        unsigned op = a[at], words, payload = 0;
        REQUIRE(op == b[at]);
        switch (op) {
        case 1: words = 18; break;
        case 2: words = 1 + sizeof(D3DMATERIAL9) / 4; break;
        case 3: words = 2 + sizeof(D3DLIGHT9) / 4; break;
        case 4: case 5: case 6: words = 3; break;
        case 7: case 8: words = 4; break;
        case 9: case 10: case 11: case 15: words = 2; break;
        default:
            REQUIRE(op >= 12 && op <= 18 && at + 3 <= n);
            REQUIRE(a[at + 1] == b[at + 1] && a[at + 2] == b[at + 2]);
            words = 3 + a[at + 2] * ((op == 14 || op == 18) ? 1 : 4);
            if (op == 12 || op == 16) payload = 3;
        }
        REQUIRE(words <= n - at);
        for (unsigned i = 1; i < words; i++) {
            unsigned x = a[at + i], y = b[at + i];
            if (x == y) continue;
            if (payload && i >= payload && !(x & 0x80000000u) &&
                !(y & 0x80000000u) && x < 0x7F800000u && y < 0x7F800000u) {
                unsigned ulp = x > y ? x - y : y - x;
                float xf, yf;
                double difference;
                memcpy(&xf, &x, sizeof xf); memcpy(&yf, &y, sizeof yf);
                difference = (double)xf - yf;
                if (difference < 0) difference = -difference;
                if (difference > max_abs) max_abs = difference;
                if (ulp > max_ulp) max_ulp = ulp;
                floats++;
            } else other++;
        }
        at += words;
    }
    printf("  changed nonnegative finite float constants=%u, max ULP=%u, max absolute=%.9g, other words=%u\n",
           floats, max_ulp, max_abs, other);
}

static unsigned compare_commands(ID3DXEffect *fx[2], input *inputs, unsigned n,
                                 const char *name, const char *technique)
{
    static unsigned saved[REC_MAX];
    unsigned runtime, variant, count = 0, differences = 0;
    for (variant = 0; variant < 2; variant++) {
        for (runtime = 0; runtime < 2; runtime++) {
            set_inputs(fx[runtime], runtime, inputs, n, variant);
            HR(FXCALL(fx[runtime], SetStateManager, (ID3DXEffectStateManager *)&g_rm));
            g_rec_n = 0; g_rec_overflow = 0;
            cycle(fx[runtime], 6);
            REQUIRE(!g_rec_overflow && g_rec_n);
            if (!runtime) { count = g_rec_n; memcpy(saved, g_rec, count * 4); }
            else if (g_rec_n != count || memcmp(saved, g_rec, count * 4)) {
                printf("COMMAND_DIFFERENCE %s %s inputs=%u variant=%u words=%u/%u hashes=%08X/%08X\n",
                    name, technique, n, variant, count, g_rec_n,
                    fnv1a((const unsigned char *)saved, count * 4),
                    fnv1a((const unsigned char *)g_rec, g_rec_n * 4));
                differences++;
                if (count == g_rec_n) describe_difference(saved, g_rec, count);
            }
            HR(FXCALL(fx[runtime], SetStateManager, NULL));
        }
    }
    return differences;
}

static double measure(ID3DXEffect *fx, unsigned runtime, input *inputs, unsigned n, unsigned flags)
{
    unsigned rep, group;
    long long begin, finish;
    drain(); begin = ticks();
    for (rep = 0; rep < REPEATS; rep++)
        for (group = 0; group < GROUPS; group++) {
            set_inputs(fx, runtime, inputs, n, group & 1);
            cycle(fx, flags);
        }
    finish = ticks(); drain();
    return (finish - begin) * 1000.0 / frequency / REPEATS;
}

static int cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
    const char *assets = argc > 1 ? argv[1] : "C:/Jogos/AgeoftheRing/rotwk";
    char path[MAX_PATH], backend[MAX_PATH];
    unsigned size, pos = 16, files, i, technique, checked = 0, differences = 0;
    unsigned char *big;
    HMODULE dll[2], d9;
    create_effect_fn create[2];
    IDirect3D9 *d3d;
    D3DPRESENT_PARAMETERS pp = {0};
    HWND wnd;
    LARGE_INTEGER f;
    snprintf(path, sizeof path, "%s/Shaders.big", assets);
    big = read_file(path, &size);
    REQUIRE(big && size >= 16 && (!memcmp(big, "BIGF", 4) || !memcmp(big, "BIG4", 4)));
    snprintf(path, sizeof path, "%s/d3dx9_27.dll", assets);
    dll[0] = LoadLibraryA(path); dll[1] = LoadLibraryA("d3dx9_43.dll");
    d9 = LoadLibraryA("d3d9.dll"); REQUIRE(dll[0] && dll[1] && d9);
    for (i = 0; i < 2; i++) {
        create[i] = (create_effect_fn)(void *)GetProcAddress(dll[i], "D3DXCreateEffect");
        REQUIRE(create[i]);
        REQUIRE(GetModuleFileNameA(dll[i], backend, sizeof backend));
        printf("runtime %u: %s\n", i, backend);
    }
    REQUIRE(GetModuleFileNameA(d9, backend, sizeof backend)); printf("backend: %s\n", backend);
    d3d = ((create9_fn)(void *)GetProcAddress(d9, "Direct3DCreate9"))(D3D_SDK_VERSION);
    REQUIRE(d3d);
    wnd = CreateWindowExA(0, "STATIC", "effect_screen", WS_POPUP, 0, 0, 64, 64, NULL,NULL,NULL,NULL);
    REQUIRE(wnd);
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = wnd;
    pp.BackBufferWidth = pp.BackBufferHeight = 64;
    HR(IDirect3D9_CreateDevice(d3d, 0, D3DDEVTYPE_HAL, wnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device));
    HR(IDirect3DDevice9_CreateQuery(device, D3DQUERYTYPE_EVENT, &fence));
    QueryPerformanceFrequency(&f); frequency = (double)f.QuadPart;
    g_rec = malloc(REC_MAX * 4); REQUIRE(g_rec);
    g_rm_vtbl.SetVertexShader = normalized_vs; g_rm_vtbl.SetPixelShader = normalized_ps;
    files = be32(big + 8);
    for (i = 0; i < files; i++) {
        unsigned off, len;
        const char *name;
        ID3DXEffect *fx[2] = {0};
        D3DXEFFECT_DESC desc[2];
        REQUIRE(pos + 8 < size);
        off = be32(big + pos); len = be32(big + pos + 4); name = (const char *)big + pos + 8;
        REQUIRE(memchr(name, 0, size - pos - 8));
        pos += 8 + (unsigned)strlen(name) + 1;
        if (!ends_with(name, ".fxo")) continue;
        REQUIRE(off <= size && len <= size - off);
        for (unsigned rt = 0; rt < 2; rt++) {
            HR(create[rt](device, big + off, len, NULL,NULL,0,NULL,&fx[rt],NULL));
            HR(FXCALL(fx[rt], GetDesc, &desc[rt]));
        }
        REQUIRE(desc[0].Techniques == desc[1].Techniques);
        for (technique = 0; technique < desc[0].Techniques; technique++) {
            D3DXHANDLE tech[2]; D3DXTECHNIQUE_DESC td[2]; input inputs[256]; unsigned n;
            for (unsigned rt = 0; rt < 2; rt++) {
                tech[rt] = FXCALL(fx[rt], GetTechnique, technique);
                HR(FXCALL(fx[rt], GetTechniqueDesc, tech[rt], &td[rt]));
                HR(FXCALL(fx[rt], SetTechnique, tech[rt]));
            }
            REQUIRE(!strcmp(td[0].Name, td[1].Name) && td[0].Passes == td[1].Passes);
            n = collect_inputs(fx, tech, inputs, 256);
            differences += compare_commands(fx, inputs, n, name, td[0].Name); checked++;
            /* The default unit material's regular/shadow techniques screen
             * throughput; other techniques receive command comparison only. */
            if (ends_with(name, "defaultw3d.fxo")) {
                printf("technique %s: %u inputs, %u passes\n", td[0].Name, n, td[0].Passes);
                if (!strcmp(td[0].Name, "_CreateShadowMap") || !strcmp(td[0].Name, "Default")) {
                    double values[3][SAMPLES], delta[2][SAMPLES];
                    for (unsigned round = 0; round < SAMPLES + 2; round++) {
                        double times[3];
                        for (unsigned j = 0; j < 3; j++) {
                            unsigned mode = (j + round) % 3;
                            unsigned rt = mode == 1;
                            times[mode] = measure(fx[rt], rt, inputs, n, mode == 2 ? 7 : 6);
                        }
                        if (round < 2) continue;
                        unsigned row = round - 2;
                        for (unsigned j = 0; j < 3; j++) values[j][row] = times[j];
                        delta[0][row] = times[0] - times[1]; delta[1][row] = times[0] - times[2];
                        printf("sample,%s,%u,%.4f,%.4f,%.4f\n", td[0].Name, row, times[0],times[1],times[2]);
                    }
                    for (unsigned j = 0; j < 3; j++) qsort(values[j],SAMPLES,sizeof(double),cmp);
                    for (unsigned j = 0; j < 2; j++) qsort(delta[j],SAMPLES,sizeof(double),cmp);
                    printf("SUMMARY %s: 27/flags6 %.4f, 43/flags6 %.4f, 27/flags7 %.4f ms per %u groups; paired savings newer=%.4f no-save=%.4f\n",
                        td[0].Name,values[0][SAMPLES/2],values[1][SAMPLES/2],values[2][SAMPLES/2],GROUPS,delta[0][SAMPLES/2],delta[1][SAMPLES/2]);
                }
            }
            for (unsigned j = 0; j < n; j++) { free(inputs[j].data[0]); free(inputs[j].data[1]); }
        }
        FXCALL(fx[0], Release); FXCALL(fx[1], Release);
    }
    REQUIRE(checked);
    printf("%s: %u techniques, %u command comparisons (two parameter sets), %u differences\n",
        differences ? "MISMATCH" : "OK", checked, 2 * checked, differences);
    free(g_rec); free(big);
    IDirect3DQuery9_Release(fence);
    IDirect3DDevice9_Release(device);
    IDirect3D9_Release(d3d);
    DestroyWindow(wnd);
    FreeLibrary(dll[0]); FreeLibrary(dll[1]); FreeLibrary(d9);
    return differences ? 2 : 0;
}
