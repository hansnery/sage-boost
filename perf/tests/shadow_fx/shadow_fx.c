/* Off-line proof for ShadowBatchIgnoreLight (dev only, not shipped).
 *
 * The shadow-map pass draws each FX material with its effect's
 * `_CreateShadowMap` technique. With ShadowBatchIgnoreLight the game's batch
 * no longer splits a group where only the light environment differs, so a
 * merged group's meshes are drawn with the first mesh's lights. That is exact
 * if no `_CreateShadowMap` technique reads anything the light environment
 * feeds. For every effect in the game's Shaders.big this tool:
 *
 *   1. lists the parameters d3dx9 says the technique uses
 *      (ID3DXEffect::IsParameterUsed) and fails if one of them is a light,
 *      ambient or fog parameter (by name, semantic or SasBindAddress);
 *   2. records every device call the technique's passes make through a state
 *      manager (render, sampler and texture stage states, textures, shaders,
 *      every shader constant) with the light, ambient and fog parameters set to
 *      two different values, and fails if the two recordings differ;
 *   3. prints the passes' depth, blend, fog and stencil states.
 *
 * Then the NoStencilFlushes draw-order tests: the shadow map in any order,
 * and the main view with the flush windows sorted as one.
 *
 *   shadow_fx.exe [--big PATH] [--verbose]     exit code 1 on any failure
 */
#define COBJMACROS
#define CINTERFACE
#include <windows.h>
#include <d3d9.h>
#include <d3dcommon.h>
#include <d3dx9effect.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shadowfx_table.h"

#define FXCALL(fx, m, ...) ((fx)->lpVtbl->m((fx), ##__VA_ARGS__))
#define ID3DXEffect_SetValue(fx, ...) FXCALL(fx, SetValue, __VA_ARGS__)
#define ID3DXEffect_Begin(fx, ...) FXCALL(fx, Begin, __VA_ARGS__)
#define ID3DXEffect_BeginPass(fx, ...) FXCALL(fx, BeginPass, __VA_ARGS__)
#define ID3DXEffect_CommitChanges(fx) FXCALL(fx, CommitChanges)
#define ID3DXEffect_EndPass(fx) FXCALL(fx, EndPass)
#define ID3DXEffect_End(fx) FXCALL(fx, End)
#define ID3DXEffect_GetTechniqueByName(fx, ...) FXCALL(fx, GetTechniqueByName, __VA_ARGS__)
#define ID3DXEffect_Release(fx) FXCALL(fx, Release)
#define ID3DXEffect_GetDesc(fx, ...) FXCALL(fx, GetDesc, __VA_ARGS__)
#define ID3DXEffect_GetParameter(fx, ...) FXCALL(fx, GetParameter, __VA_ARGS__)
#define ID3DXEffect_GetParameterDesc(fx, ...) FXCALL(fx, GetParameterDesc, __VA_ARGS__)
#define ID3DXEffect_GetAnnotationByName(fx, ...) FXCALL(fx, GetAnnotationByName, __VA_ARGS__)
#define ID3DXEffect_GetString(fx, ...) FXCALL(fx, GetString, __VA_ARGS__)
#define ID3DXEffect_IsParameterUsed(fx, ...) FXCALL(fx, IsParameterUsed, __VA_ARGS__)
#define ID3DXEffect_SetStateManager(fx, ...) FXCALL(fx, SetStateManager, __VA_ARGS__)
#define ID3DXEffect_SetTechnique(fx, ...) FXCALL(fx, SetTechnique, __VA_ARGS__)

typedef HRESULT(WINAPI *create_effect_fn)(IDirect3DDevice9 *, const void *, UINT, const D3DXMACRO *,
                                         ID3DXInclude *, DWORD, ID3DXEffectPool *, ID3DXEffect **,
                                         ID3DXBuffer **);
typedef IDirect3D9 *(WINAPI *create9_fn)(UINT);

static int g_verbose;

/* ---- recording state manager ---- */

#define REC_MAX (1 << 20)
static unsigned *g_rec;
static unsigned g_rec_n;
static int g_rec_overflow;
/* Last value per render state seen, for the summary. */
static DWORD g_rs[256];
static unsigned char g_rs_set[256];

static void put(unsigned v)
{
    if (g_rec_n < REC_MAX)
        g_rec[g_rec_n++] = v;
    else
        g_rec_overflow = 1;
}

static void put_n(const void *p, unsigned dwords)
{
    const unsigned *d = p;
    unsigned i;
    for (i = 0; i < dwords; i++)
        put(d[i]);
}

typedef struct {
    ID3DXEffectStateManagerVtbl *lpVtbl;
} rec_manager;

static HRESULT WINAPI rm_qi(ID3DXEffectStateManager *self, REFIID riid, void **out)
{
    (void)riid;
    *out = self;
    return S_OK;
}
static ULONG WINAPI rm_addref(ID3DXEffectStateManager *self)
{
    (void)self;
    return 1;
}
static ULONG WINAPI rm_release(ID3DXEffectStateManager *self)
{
    (void)self;
    return 1;
}
static HRESULT WINAPI rm_transform(ID3DXEffectStateManager *self, D3DTRANSFORMSTATETYPE s,
                                   const D3DMATRIX *m)
{
    (void)self;
    put(1), put((unsigned)s), put_n(m, 16);
    return S_OK;
}
static HRESULT WINAPI rm_material(ID3DXEffectStateManager *self, const D3DMATERIAL9 *m)
{
    (void)self;
    put(2), put_n(m, sizeof *m / 4);
    return S_OK;
}
static HRESULT WINAPI rm_light(ID3DXEffectStateManager *self, DWORD i, const D3DLIGHT9 *l)
{
    (void)self;
    put(3), put(i), put_n(l, sizeof *l / 4);
    return S_OK;
}
static HRESULT WINAPI rm_light_enable(ID3DXEffectStateManager *self, DWORD i, WINBOOL on)
{
    (void)self;
    put(4), put(i), put((unsigned)on);
    return S_OK;
}
static HRESULT WINAPI rm_rs(ID3DXEffectStateManager *self, D3DRENDERSTATETYPE s, DWORD v)
{
    (void)self;
    put(5), put((unsigned)s), put(v);
    if ((unsigned)s < 256) {
        g_rs[s] = v;
        g_rs_set[s] = 1;
    }
    return S_OK;
}
static HRESULT WINAPI rm_texture(ID3DXEffectStateManager *self, DWORD stage,
                                 IDirect3DBaseTexture9 *t)
{
    (void)self;
    put(6), put(stage), put((unsigned)(UINT_PTR)t);
    return S_OK;
}
static HRESULT WINAPI rm_tss(ID3DXEffectStateManager *self, DWORD stage, D3DTEXTURESTAGESTATETYPE t,
                             DWORD v)
{
    (void)self;
    put(7), put(stage), put((unsigned)t), put(v);
    return S_OK;
}
static HRESULT WINAPI rm_sampler(ID3DXEffectStateManager *self, DWORD s, D3DSAMPLERSTATETYPE t,
                                 DWORD v)
{
    (void)self;
    put(8), put(s), put((unsigned)t), put(v);
    return S_OK;
}
static HRESULT WINAPI rm_npatch(ID3DXEffectStateManager *self, FLOAT n)
{
    (void)self;
    put(9), put_n(&n, 1);
    return S_OK;
}
static HRESULT WINAPI rm_fvf(ID3DXEffectStateManager *self, DWORD f)
{
    (void)self;
    put(10), put(f);
    return S_OK;
}
static HRESULT WINAPI rm_vs(ID3DXEffectStateManager *self, IDirect3DVertexShader9 *s)
{
    (void)self;
    put(11), put((unsigned)(UINT_PTR)s);
    return S_OK;
}
static HRESULT WINAPI rm_vs_f(ID3DXEffectStateManager *self, UINT r, const FLOAT *c, UINT n)
{
    (void)self;
    put(12), put(r), put(n), put_n(c, 4 * n);
    return S_OK;
}
static HRESULT WINAPI rm_vs_i(ID3DXEffectStateManager *self, UINT r, const INT *c, UINT n)
{
    (void)self;
    put(13), put(r), put(n), put_n(c, 4 * n);
    return S_OK;
}
static HRESULT WINAPI rm_vs_b(ID3DXEffectStateManager *self, UINT r, const WINBOOL *c, UINT n)
{
    (void)self;
    put(14), put(r), put(n), put_n(c, n);
    return S_OK;
}
static HRESULT WINAPI rm_ps(ID3DXEffectStateManager *self, IDirect3DPixelShader9 *s)
{
    (void)self;
    put(15), put((unsigned)(UINT_PTR)s);
    return S_OK;
}
static HRESULT WINAPI rm_ps_f(ID3DXEffectStateManager *self, UINT r, const FLOAT *c, UINT n)
{
    (void)self;
    put(16), put(r), put(n), put_n(c, 4 * n);
    return S_OK;
}
static HRESULT WINAPI rm_ps_i(ID3DXEffectStateManager *self, UINT r, const INT *c, UINT n)
{
    (void)self;
    put(17), put(r), put(n), put_n(c, 4 * n);
    return S_OK;
}
static HRESULT WINAPI rm_ps_b(ID3DXEffectStateManager *self, UINT r, const WINBOOL *c, UINT n)
{
    (void)self;
    put(18), put(r), put(n), put_n(c, n);
    return S_OK;
}

static ID3DXEffectStateManagerVtbl g_rm_vtbl = {
    rm_qi,     rm_addref, rm_release, rm_transform, rm_material, rm_light,
    rm_light_enable, rm_rs, rm_texture, rm_tss,  rm_sampler, rm_npatch,
    rm_fvf,    rm_vs,     rm_vs_f,    rm_vs_i,      rm_vs_b,     rm_ps,
    rm_ps_f,   rm_ps_i,   rm_ps_b,
};
static rec_manager g_rm = { &g_rm_vtbl };

/* ---- BIG archive ---- */

static unsigned char *read_file(const char *path, unsigned *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    long n;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)n);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(b);
        return NULL;
    }
    fclose(f);
    *size = (unsigned)n;
    return b;
}

static unsigned be32(const unsigned char *p)
{
    return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3];
}

static int ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && !_stricmp(s + a - b, tail);
}

static unsigned fnv1a(const unsigned char *p, unsigned n)
{
    unsigned h = 0x811C9DC5u, i;
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 0x01000193u;
    return h;
}

/* ---- the check ---- */

static int contains_i(const char *s, const char *word)
{
    size_t n = strlen(word);
    if (!s)
        return 0;
    for (; *s; s++)
        if (!_strnicmp(s, word, n))
            return 1;
    return 0;
}

/* Fed by the light environment or the fog colour (what a merged group's
 * meshes may have differently from its first mesh). */
static int is_light_or_fog(const char *name, const char *semantic, const char *address)
{
    static const char *const words[] = { "light", "ambient", "fog" };
    unsigned i;
    for (i = 0; i < sizeof words / sizeof *words; i++)
        if (contains_i(name, words[i]) || contains_i(semantic, words[i]) ||
            contains_i(address, words[i]))
            return 1;
    return 0;
}

typedef struct {
    D3DXHANDLE h;
    D3DXPARAMETER_DESC d;
    const char *address;
    int used, light;
} param;

static void fill(ID3DXEffect *fx, const param *p, int variant)
{
    unsigned char buf[16384];
    unsigned i, bytes = p->d.Bytes;
    if (!bytes || bytes > sizeof buf || p->d.Class == D3DXPC_OBJECT)
        return;
    for (i = 0; i < bytes / 4; i++) {
        float f = variant ? 0.75f + 0.01f * (float)(i % 7) : -0.5f + 0.02f * (float)(i % 5);
        if (p->d.Type == D3DXPT_BOOL || p->d.Type == D3DXPT_INT) {
            int v = variant ? (int)(i % 3) + 1 : 0;
            memcpy(buf + 4 * i, &v, 4);
        } else {
            memcpy(buf + 4 * i, &f, 4);
        }
    }
    ID3DXEffect_SetValue(fx, p->h, buf, bytes);
}

/* Records Begin(6) .. End of every pass, as the game draws one mesh. */
static unsigned record(ID3DXEffect *fx, param *ps, unsigned np, int variant, unsigned *out,
                       unsigned cap)
{
    UINT passes = 0, k, i;
    for (i = 0; i < np; i++)
        if (ps[i].light)
            fill(fx, &ps[i], variant);
    g_rec_n = 0;
    g_rec_overflow = 0;
    memset(g_rs_set, 0, sizeof g_rs_set);
    ID3DXEffect_Begin(fx, &passes, D3DXFX_DONOTSAVESHADERSTATE | D3DXFX_DONOTSAVESAMPLERSTATE);
    for (k = 0; k < passes; k++) {
        ID3DXEffect_BeginPass(fx, k);
        ID3DXEffect_CommitChanges(fx);
        ID3DXEffect_EndPass(fx);
    }
    ID3DXEffect_End(fx);
    if (g_rec_n > cap)
        g_rec_n = cap;
    memcpy(out, g_rec, g_rec_n * 4);
    return g_rec_overflow ? 0xFFFFFFFFu : g_rec_n;
}

static const char *rs_name(unsigned s)
{
    switch (s) {
    case D3DRS_ZENABLE: return "ZENABLE";
    case D3DRS_ZWRITEENABLE: return "ZWRITEENABLE";
    case D3DRS_ZFUNC: return "ZFUNC";
    case D3DRS_ALPHABLENDENABLE: return "ALPHABLENDENABLE";
    case D3DRS_ALPHATESTENABLE: return "ALPHATESTENABLE";
    case D3DRS_FOGENABLE: return "FOGENABLE";
    case D3DRS_STENCILENABLE: return "STENCILENABLE";
    case D3DRS_COLORWRITEENABLE: return "COLORWRITEENABLE";
    case D3DRS_CULLMODE: return "CULLMODE";
    }
    return NULL;
}

/* The plugin's table entry for this file, if it lists it with this content. */
static const pf_shadowfx_entry *table_entry(const char *name, const unsigned char *data,
                                            unsigned size)
{
    const char *base = strrchr(name, '\\');
    unsigned i;
    base = base ? base + 1 : name;
    for (i = 0; i < PF_SHADOWFX_COUNT; i++)
        if (!_stricmp(pf_shadowfx_table[i].name, base) && pf_shadowfx_table[i].size == size &&
            pf_shadowfx_table[i].fnv == fnv1a(data, size))
            return &pf_shadowfx_table[i];
    return NULL;
}

static int check_effect(IDirect3DDevice9 *dev, create_effect_fn create, const char *name,
                        const unsigned char *data, unsigned size)
{
    static unsigned rec_a[REC_MAX], rec_b[REC_MAX];
    const pf_shadowfx_entry *entry = table_entry(name, data, size);
    ID3DXEffect *fx = NULL;
    ID3DXBuffer *err = NULL;
    D3DXEFFECT_DESC desc;
    D3DXHANDLE tech;
    param *ps;
    unsigned i, na, nb, used = 0, light_used = 0, light_total = 0;
    int fail = 0;

    if (!entry) {
        printf("%-40s FAIL: not in perf/src/shadowfx_table.h with this content (fnv %08X, %u "
               "bytes)\n",
               name, fnv1a(data, size), size);
        fail = 1;
    }
    if (FAILED(create(dev, data, size, NULL, NULL, 0, NULL, &fx, &err)) || !fx) {
        printf("%-40s FAIL: D3DXCreateEffect: %s\n", name,
               err ? (const char *)ID3DXBuffer_GetBufferPointer(err) : "?");
        return 1;
    }
    tech = ID3DXEffect_GetTechniqueByName(fx, "_CreateShadowMap");
    if (entry && entry->shadow != (tech != NULL)) {
        printf("%-40s FAIL: the table says it %s a _CreateShadowMap technique\n", name,
               entry->shadow ? "has" : "has no");
        fail = 1;
    }
    if (!tech) {
        printf("%-40s fnv %08X: no _CreateShadowMap technique (never drawn into the shadow map)\n",
               name, fnv1a(data, size));
        ID3DXEffect_Release(fx);
        return fail;
    }
    ID3DXEffect_GetDesc(fx, &desc);
    ps = calloc(desc.Parameters ? desc.Parameters : 1, sizeof *ps);
    for (i = 0; i < desc.Parameters; i++) {
        D3DXHANDLE a;
        param *p = &ps[i];
        p->h = ID3DXEffect_GetParameter(fx, NULL, i);
        ID3DXEffect_GetParameterDesc(fx, p->h, &p->d);
        a = ID3DXEffect_GetAnnotationByName(fx, p->h, "SasBindAddress");
        if (a)
            ID3DXEffect_GetString(fx, a, &p->address);
        p->used = ID3DXEffect_IsParameterUsed(fx, p->h, tech) != 0;
        p->light = is_light_or_fog(p->d.Name, p->d.Semantic, p->address);
        used += p->used;
        light_total += p->light;
        light_used += p->used && p->light;
    }
    printf("%-40s fnv %08X: %u parameters, %u used by _CreateShadowMap, %u light/ambient/fog "
           "parameters, %u of them used\n",
           name, fnv1a(data, size), desc.Parameters, used, light_total, light_used);
    for (i = 0; i < desc.Parameters; i++) {
        param *p = &ps[i];
        if (p->used || (g_verbose && p->light))
            printf("    %s %-36s sem %-24s addr %s%s\n", p->used ? "used  " : "unused",
                   p->d.Name ? p->d.Name : "?", p->d.Semantic ? p->d.Semantic : "-",
                   p->address ? p->address : "-", p->light ? "  [light/ambient/fog]" : "");
        if (p->used && p->light)
            fail = 1;
    }

    ID3DXEffect_SetStateManager(fx, (ID3DXEffectStateManager *)&g_rm);
    ID3DXEffect_SetTechnique(fx, tech);
    na = record(fx, ps, desc.Parameters, 0, rec_a, REC_MAX);
    nb = record(fx, ps, desc.Parameters, 1, rec_b, REC_MAX);
    if (na == 0xFFFFFFFFu || nb == 0xFFFFFFFFu) {
        printf("    FAIL: recording overflow\n");
        fail = 1;
    } else if (na != nb || memcmp(rec_a, rec_b, na * 4)) {
        printf("    FAIL: the device calls differ with other light/ambient/fog values (%u vs %u "
               "dwords)\n",
               na, nb);
        fail = 1;
    } else {
        printf("    device calls identical for both light/ambient/fog values (%u dwords)\n", na);
    }
    printf("    pass states:");
    for (i = 0; i < 256; i++) {
        const char *n = rs_name(i);
        if (n && g_rs_set[i])
            printf(" %s=%lu", n, g_rs[i]);
    }
    printf("\n");
    ID3DXEffect_SetStateManager(fx, NULL);
    free(ps);
    ID3DXEffect_Release(fx);
    return fail;
}

/* ---- draw order (NoStencilFlushes) ----
 * Dropping the flushes around a big monster draws its meshes in the batch's
 * order instead of on their own. The shadow passes above all draw with
 * ZENABLE=1 ZWRITEENABLE=1 ZFUNC=LESSEQUAL and no blending, writing z/w: the
 * map is the nearest surface's depth whatever the order. This draws random
 * overlapping objects into an R32F map with a D24S8 buffer in two orders and
 * compares the maps bit for bit. */

typedef HRESULT(WINAPI *compile_fn)(const void *, SIZE_T, const char *, const void *, void *,
                                    const char *, const char *, UINT, UINT, ID3DBlob **,
                                    ID3DBlob **);

static void *compile_shader(compile_fn cc, IDirect3DDevice9 *dev, const char *src,
                            const char *target)
{
    ID3DBlob *code = NULL, *err = NULL;
    void *sh = NULL;
    if (FAILED(cc(src, strlen(src), "order", NULL, NULL, "main", target, 0, 0, &code, &err)))
        return NULL;
    if (target[0] == 'v')
        IDirect3DDevice9_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(code),
                                            (IDirect3DVertexShader9 **)&sh);
    else
        IDirect3DDevice9_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(code),
                                           (IDirect3DPixelShader9 **)&sh);
    ID3D10Blob_Release(code);
    return sh;
}

#define ORDER_RT 256
#define ORDER_OBJECTS 24
#define ORDER_TRIS 40

typedef struct {
    float x, y, z, w;
} order_vtx;

static float urand(unsigned *s)
{
    *s = *s * 1664525u + 1013904223u;
    return (float)((*s >> 8) & 0xFFFF) / 65535.0f;
}

static void draw_objects(IDirect3DDevice9 *dev, const order_vtx *v, const int *order)
{
    int k;
    IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                           D3DCOLOR_ARGB(255, 255, 255, 255), 1.0f, 0);
    IDirect3DDevice9_BeginScene(dev);
    for (k = 0; k < ORDER_OBJECTS; k++)
        IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLELIST, ORDER_TRIS,
                                         v + order[k] * ORDER_TRIS * 3, sizeof *v);
    IDirect3DDevice9_EndScene(dev);
}

static int read_map(IDirect3DDevice9 *dev, IDirect3DSurface9 *rt, IDirect3DSurface9 *sys,
                    unsigned *out)
{
    D3DLOCKED_RECT lr;
    int y;
    if (FAILED(IDirect3DDevice9_GetRenderTargetData(dev, rt, sys)) ||
        FAILED(IDirect3DSurface9_LockRect(sys, &lr, NULL, D3DLOCK_READONLY)))
        return 0;
    for (y = 0; y < ORDER_RT; y++)
        memcpy(out + y * ORDER_RT, (const char *)lr.pBits + y * lr.Pitch, ORDER_RT * 4);
    IDirect3DSurface9_UnlockRect(sys);
    return 1;
}

static int order_test(IDirect3DDevice9 *dev)
{
    static const char vs[] = "float4 main(float4 p : POSITION, out float2 zw : TEXCOORD1) : POSITION"
                             " { zw = p.zw; return p; }";
    static const char ps[] = "float4 main(float2 zw : TEXCOORD1) : COLOR { return zw.x / zw.y; }";
    static order_vtx v[ORDER_OBJECTS * ORDER_TRIS * 3];
    static unsigned a[ORDER_RT * ORDER_RT], b[ORDER_RT * ORDER_RT];
    HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
    compile_fn cc = dc ? (compile_fn)(void *)GetProcAddress(dc, "D3DCompile") : NULL;
    IDirect3DSurface9 *rt = NULL, *sys = NULL, *ds = NULL;
    IDirect3DVertexShader9 *vsh;
    IDirect3DPixelShader9 *psh;
    int order[ORDER_OBJECTS], k, round, fail = 0;
    unsigned s = 1234567u, i, covered = 0, diff = 0;

    if (!cc) {
        printf("order: SKIP (d3dcompiler_47.dll missing)\n");
        return 0;
    }
    vsh = compile_shader(cc, dev, vs, "vs_2_0");
    psh = compile_shader(cc, dev, ps, "ps_2_0");
    IDirect3DDevice9_CreateRenderTarget(dev, ORDER_RT, ORDER_RT, D3DFMT_R32F, D3DMULTISAMPLE_NONE,
                                        0, FALSE, &rt, NULL);
    IDirect3DDevice9_CreateOffscreenPlainSurface(dev, ORDER_RT, ORDER_RT, D3DFMT_R32F,
                                                 D3DPOOL_SYSTEMMEM, &sys, NULL);
    IDirect3DDevice9_CreateDepthStencilSurface(dev, ORDER_RT, ORDER_RT, D3DFMT_D24S8,
                                               D3DMULTISAMPLE_NONE, 0, TRUE, &ds, NULL);
    if (!vsh || !psh || !rt || !sys || !ds) {
        printf("order: SKIP (no R32F target, D24S8 buffer or shaders)\n");
        return 0;
    }
    /* Each object: triangles anywhere on the map at depths 0.1..0.9; one in
     * four objects also repeats another object's triangles exactly (equal
     * depths, the tie case). */
    for (i = 0; i < ORDER_OBJECTS * ORDER_TRIS * 3; i++) {
        float w = 0.5f + urand(&s);
        v[i].x = (urand(&s) * 2 - 1) * w;
        v[i].y = (urand(&s) * 2 - 1) * w;
        v[i].z = (0.1f + 0.8f * urand(&s)) * w;
        v[i].w = w;
    }
    for (k = 3; k < ORDER_OBJECTS; k += 4)
        memcpy(v + k * ORDER_TRIS * 3, v + (k - 3) * ORDER_TRIS * 3, sizeof *v * ORDER_TRIS * 3);
    IDirect3DDevice9_SetRenderTarget(dev, 0, rt);
    IDirect3DDevice9_SetDepthStencilSurface(dev, ds);
    IDirect3DDevice9_SetVertexShader(dev, vsh);
    IDirect3DDevice9_SetPixelShader(dev, psh);
    IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZW);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZWRITEENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_STENCILENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
    for (k = 0; k < ORDER_OBJECTS; k++)
        order[k] = k;
    draw_objects(dev, v, order);
    if (!read_map(dev, rt, sys, a))
        fail = 1;
    for (round = 0; round < 4 && !fail; round++) {
        for (k = ORDER_OBJECTS - 1; k > 0; k--) {
            int j = (int)(urand(&s) * (float)(k + 1)), t;
            if (j > k)
                j = k;
            t = order[k], order[k] = order[j], order[j] = t;
        }
        draw_objects(dev, v, order);
        if (!read_map(dev, rt, sys, b)) {
            fail = 1;
            break;
        }
        for (i = 0; i < ORDER_RT * ORDER_RT; i++) {
            covered += a[i] != 0x3F800000u;
            diff += a[i] != b[i];
        }
    }
    printf("order: %d objects x %d triangles, 4 shuffled orders: %u texels drawn, %u differ from "
           "the first order%s\n",
           ORDER_OBJECTS, ORDER_TRIS, covered, diff, fail ? " (read-back failed)" : "");
    IDirect3DDevice9_SetRenderTarget(dev, 0, NULL);
    IDirect3DSurface9_Release(ds);
    IDirect3DSurface9_Release(sys);
    IDirect3DSurface9_Release(rt);
    IDirect3DVertexShader9_Release(vsh);
    IDirect3DPixelShader9_Release(psh);
    return fail || diff || !covered;
}

/* ---- draw order in the main view (NoStencilFlushes=2) ----
 * The flushes split the FX queue into windows: what was queued before the
 * monster, the monster, what comes after. The mesh renderer sorts each window
 * by model pointer only, opaque and blended meshes alike, so dropping the
 * flushes sorts the three windows as one. This draws coloured objects (8 in
 * the first window, 4 for the monster, 12 after) both ways into an A8R8G8B8
 * target with LESSEQUAL depth and compares the colours. All opaque without
 * equal depths must not differ; the cases that make value 2 inexact must. */

#define VIEW_FIRST 8
#define VIEW_MONSTER 4

static void view_sort(int *seq, int n, const unsigned *key)
{
    int i, j;
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && key[seq[j]] < key[seq[j - 1]]; j--) {
            int t = seq[j];
            seq[j] = seq[j - 1];
            seq[j - 1] = t;
        }
}

static void draw_view(IDirect3DDevice9 *dev, const order_vtx *v, const int *seq,
                      const float (*col)[4], const int *blend, int blend_zwrite)
{
    int k;
    IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0,
                           1.0f, 0);
    IDirect3DDevice9_BeginScene(dev);
    for (k = 0; k < ORDER_OBJECTS; k++) {
        int o = seq[k];
        IDirect3DDevice9_SetPixelShaderConstantF(dev, 0, col[o], 1);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, blend[o] ? TRUE : FALSE);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_ZWRITEENABLE, blend[o] ? blend_zwrite : TRUE);
        IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLELIST, ORDER_TRIS,
                                         v + o * ORDER_TRIS * 3, sizeof *v);
    }
    IDirect3DDevice9_EndScene(dev);
}

static int view_order_test(IDirect3DDevice9 *dev)
{
    static const char vs[] = "float4 main(float4 p : POSITION) : POSITION { return p; }";
    static const char ps[] = "float4 c : register(c0); float4 main() : COLOR { return c; }";
    static const char *const names[5] = {
        "opaque", "opaque, equal depth across windows", "translucent before the monster",
        "translucent before the monster, no depth write", "translucent in the monster",
    };
    static order_vtx v[ORDER_OBJECTS * ORDER_TRIS * 3];
    static unsigned a[ORDER_RT * ORDER_RT], b[ORDER_RT * ORDER_RT];
    HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
    compile_fn cc = dc ? (compile_fn)(void *)GetProcAddress(dc, "D3DCompile") : NULL;
    IDirect3DSurface9 *rt = NULL, *sys = NULL, *ds = NULL;
    IDirect3DVertexShader9 *vsh;
    IDirect3DPixelShader9 *psh;
    float col[ORDER_OBJECTS][4];
    unsigned key[ORDER_OBJECTS], s = 7654321u, i;
    int stock[ORDER_OBJECTS], merged[ORDER_OBJECTS], blend[ORDER_OBJECTS], k, c, fail = 0;
    int moved = 0, x = -1, y = -1;

    if (!cc) {
        printf("view order: SKIP (d3dcompiler_47.dll missing)\n");
        return 0;
    }
    vsh = compile_shader(cc, dev, vs, "vs_2_0");
    psh = compile_shader(cc, dev, ps, "ps_2_0");
    IDirect3DDevice9_CreateRenderTarget(dev, ORDER_RT, ORDER_RT, D3DFMT_A8R8G8B8,
                                        D3DMULTISAMPLE_NONE, 0, FALSE, &rt, NULL);
    IDirect3DDevice9_CreateOffscreenPlainSurface(dev, ORDER_RT, ORDER_RT, D3DFMT_A8R8G8B8,
                                                 D3DPOOL_SYSTEMMEM, &sys, NULL);
    IDirect3DDevice9_CreateDepthStencilSurface(dev, ORDER_RT, ORDER_RT, D3DFMT_D24S8,
                                               D3DMULTISAMPLE_NONE, 0, TRUE, &ds, NULL);
    if (!vsh || !psh || !rt || !sys || !ds) {
        printf("view order: SKIP (no A8R8G8B8 target, D24S8 buffer or shaders)\n");
        return 0;
    }
    for (k = 0; k < ORDER_OBJECTS; k++) {
        col[k][0] = 0.1f + 0.8f * urand(&s);
        col[k][1] = 0.1f + 0.8f * urand(&s);
        col[k][2] = 0.1f + 0.8f * urand(&s);
        col[k][3] = 1;
        key[k] = (unsigned)k * 2654435761u >> 8;
        stock[k] = merged[k] = k;
    }
    view_sort(stock, VIEW_FIRST, key);
    view_sort(stock + VIEW_FIRST, VIEW_MONSTER, key);
    view_sort(stock + VIEW_FIRST + VIEW_MONSTER, ORDER_OBJECTS - VIEW_FIRST - VIEW_MONSTER, key);
    view_sort(merged, ORDER_OBJECTS, key);
    for (k = 0; k < ORDER_OBJECTS; k++)
        moved += stock[k] != merged[k];
    /* x in the first window drawn before y of the monster, after it merged. */
    for (k = 0; k < VIEW_FIRST * VIEW_MONSTER; k++)
        if (key[k / VIEW_MONSTER] > key[VIEW_FIRST + k % VIEW_MONSTER]) {
            x = k / VIEW_MONSTER;
            y = VIEW_FIRST + k % VIEW_MONSTER;
        }
    if (x < 0) {
        printf("view order: FAIL (no first-window object sorts after a monster mesh)\n");
        return 1;
    }

    IDirect3DDevice9_SetRenderTarget(dev, 0, rt);
    IDirect3DDevice9_SetDepthStencilSurface(dev, ds);
    IDirect3DDevice9_SetVertexShader(dev, vsh);
    IDirect3DDevice9_SetPixelShader(dev, psh);
    IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZW);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_STENCILENABLE, FALSE);

    for (c = 0; c < 5 && !fail; c++) {
        unsigned covered = 0, diff = 0;
        int want_diff = c != 0;
        /* Distinct depths everywhere; case 1 copies a monster mesh into the
         * first window under another colour. */
        for (i = 0; i < ORDER_OBJECTS * ORDER_TRIS * 3; i++) {
            float w = 0.5f + urand(&s);
            v[i].x = (urand(&s) * 2 - 1) * w;
            v[i].y = (urand(&s) * 2 - 1) * w;
            v[i].z = (0.1f + 0.8f * urand(&s)) * w;
            v[i].w = w;
        }
        for (k = 0; k < ORDER_OBJECTS; k++) {
            blend[k] = 0;
            col[k][3] = 1;
        }
        if (c == 1)
            memcpy(v + x * ORDER_TRIS * 3, v + y * ORDER_TRIS * 3, sizeof *v * ORDER_TRIS * 3);
        if (c == 2 || c == 3)
            blend[x] = 1;
        if (c == 4)
            blend[y] = 1;
        for (k = 0; k < ORDER_OBJECTS; k++)
            if (blend[k])
                col[k][3] = 0.5f;
        draw_view(dev, v, stock, (const float(*)[4])col, blend, c != 3);
        if (!read_map(dev, rt, sys, a)) {
            fail = 1;
            break;
        }
        draw_view(dev, v, merged, (const float(*)[4])col, blend, c != 3);
        if (!read_map(dev, rt, sys, b)) {
            fail = 1;
            break;
        }
        for (i = 0; i < ORDER_RT * ORDER_RT; i++) {
            covered += a[i] != 0;
            diff += a[i] != b[i];
        }
        printf("view order: %-46s %u texels drawn, %u differ%s\n", names[c], covered, diff,
               want_diff ? (diff ? " (as expected: not exact)" : " (EXPECTED a difference)")
                         : (diff ? " (EXPECTED none)" : ""));
        fail |= !covered || (want_diff ? diff == 0 : diff != 0);
    }
    printf("view order: %d of %d objects change place when the three windows are sorted as one\n",
           moved, ORDER_OBJECTS);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZWRITEENABLE, TRUE);
    IDirect3DDevice9_SetRenderTarget(dev, 0, NULL);
    IDirect3DSurface9_Release(ds);
    IDirect3DSurface9_Release(sys);
    IDirect3DSurface9_Release(rt);
    IDirect3DVertexShader9_Release(vsh);
    IDirect3DPixelShader9_Release(psh);
    return fail || !moved;
}

int main(int argc, char **argv)
{
    const char *big_path = "C:\\Jogos\\AgeoftheRing\\rotwk\\Shaders.big";
    unsigned big_size, n, pos = 16, i, fails = 0, checked = 0;
    unsigned char *big;
    HMODULE d3d9, d3dx;
    create9_fn create9;
    create_effect_fn create;
    IDirect3D9 *d3d;
    IDirect3DDevice9 *dev = NULL;
    D3DPRESENT_PARAMETERS pp;
    HWND wnd;
    int a;

    for (a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--big") && a + 1 < argc)
            big_path = argv[++a];
        else if (!strcmp(argv[a], "--verbose"))
            g_verbose = 1;
    }
    big = read_file(big_path, &big_size);
    if (!big || big_size < 16 || (memcmp(big, "BIGF", 4) && memcmp(big, "BIG4", 4))) {
        printf("SKIP: %s not readable (needs the game's Shaders.big)\n", big_path);
        return 0;
    }
    d3d9 = LoadLibraryA("d3d9.dll");
    d3dx = LoadLibraryA("d3dx9_43.dll");
    if (!d3d9 || !d3dx) {
        printf("SKIP: d3d9.dll or d3dx9_43.dll missing\n");
        return 0;
    }
    create9 = (create9_fn)(void *)GetProcAddress(d3d9, "Direct3DCreate9");
    create = (create_effect_fn)(void *)GetProcAddress(d3dx, "D3DXCreateEffect");
    d3d = create9(D3D_SDK_VERSION);
    wnd = CreateWindowExA(0, "STATIC", "shadow_fx", WS_POPUP, 0, 0, 64, 64, NULL, NULL, NULL, NULL);
    memset(&pp, 0, sizeof pp);
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow = wnd;
    if (!d3d || FAILED(IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                                               D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev))) {
        printf("SKIP: no hardware D3D9 device\n");
        DestroyWindow(wnd);
        return 0;
    }
    g_rec = malloc(REC_MAX * 4);

    n = be32(big + 8);
    for (i = 0; i < n && pos + 8 < big_size; i++) {
        unsigned off = be32(big + pos), len = be32(big + pos + 4);
        const char *name = (const char *)big + pos + 8;
        if (ends_with(name, ".fxo") && off + len <= big_size) {
            fails += (unsigned)check_effect(dev, create, name, big + off, len);
            checked++;
        }
        pos += 8 + (unsigned)strlen(name) + 1;
    }
    {
        IDirect3DSurface9 *bb = NULL;
        IDirect3DDevice9_GetRenderTarget(dev, 0, &bb);
        fails += (unsigned)order_test(dev);
        fails += (unsigned)view_order_test(dev);
        IDirect3DDevice9_SetRenderTarget(dev, 0, bb);
        if (bb)
            IDirect3DSurface9_Release(bb);
    }
    printf("%s: %u effects checked, %u failed (with the draw-order tests)\n",
           fails || !checked ? "FAIL" : "OK", checked, fails);
    free(g_rec);
    IDirect3DDevice9_Release(dev);
    IDirect3D9_Release(d3d);
    DestroyWindow(wnd);
    free(big);
    return fails || !checked ? 1 : 0;
}
