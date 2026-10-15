/* Off-line GPU check of the instanced vertex shaders (dev only, not shipped).
 *
 * For every variant in inst_shaders.h: finds the game's original vertex
 * shader by hash in the local Shaders.big, draws a synthetic mesh K times
 * with it (one draw per instance, bones at the game's registers) and once
 * with ours (one instanced draw), and compares every vertex output through a
 * debug pixel shader into a float render target. d3d9.dll is loaded from the
 * executable's folder first, so copying DXVK's next to it tests DXVK.
 *
 *   inst_gpu.exe [--big PATH] [--verbose]     exit code 1 if any variant fails
 */
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <d3dcommon.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "depthcmp.h"
#include "inst_shaders.h"

#define RT 256
#define GRID 12
#define TILES 4
#define SENTINEL (-7.0f)

typedef IDirect3D9 *(WINAPI *create9_fn)(UINT);
typedef HRESULT(WINAPI *compile_fn)(const void *, SIZE_T, const char *, const void *, void *,
                                    const char *, const char *, UINT, UINT, ID3DBlob **,
                                    ID3DBlob **);
static compile_fn g_compile;
static IDirect3DDevice9 *g_dev;
static int g_verbose;

typedef struct {
    float p0[3], p1[3], n0[3], n1[3];
    DWORD bidx;
    float bw[2];
    float uv0[2], uv1[2];
    DWORD col;
} vtx;

static float frand(unsigned *s)
{
    *s = *s * 1664525u + 1013904223u;
    return (float)((*s >> 8) & 0xFFFF) / 65535.0f;
}

static void norm3(float *v)
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    v[0] /= l, v[1] /= l, v[2] /= l;
}

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

/* An effect file inside the BIG archive. */
static const unsigned char *find_effect(const unsigned char *big, unsigned size, const char *want,
                                        unsigned *fx_size)
{
    unsigned n = be32(big + 8), i, pos = 16;
    for (i = 0; i < n && pos + 8 < size; i++) {
        unsigned off = be32(big + pos), len = be32(big + pos + 4);
        const char *name = (const char *)big + pos + 8;
        if (!_stricmp(name, want)) {
            *fx_size = len;
            return big + off;
        }
        pos += 8 + (unsigned)strlen(name) + 1;
    }
    return NULL;
}

static const unsigned char *find_fx(const unsigned char *big, unsigned size, unsigned *fx_size)
{
    return find_effect(big, size, "shaders\\compiled\\defaultw3d.fxo", fx_size);
}

static unsigned fnv1a(const unsigned char *p, unsigned n)
{
    unsigned h = 0x811C9DC5u, i;
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 0x01000193u;
    return h;
}

static const unsigned char *find_ea(const unsigned char *fx, unsigned n, unsigned hash,
                                    unsigned size)
{
    unsigned i;
    for (i = 4; i + size <= n; i += 4) {
        unsigned tok, len;
        memcpy(&tok, fx + i, 4);
        if (tok >> 16 != 0xFFFE)
            continue;
        memcpy(&len, fx + i - 4, 4);
        if (len == size && fnv1a(fx + i, size) == hash)
            return fx + i;
    }
    return NULL;
}

/* Writes the chosen components of one vertex output to .xyz, and 1 to .w so
 * the alpha channel marks coverage. */
static IDirect3DPixelShader9 *debug_ps(const char *semantic, const char *src_swz)
{
    static const char *const dst[] = { "", "x", "xy", "xyz" };
    char src[256];
    ID3DBlob *code = NULL, *err = NULL;
    IDirect3DPixelShader9 *ps = NULL;
    snprintf(src, sizeof src,
             "float4 main(float4 v : %s) : COLOR { float4 o = float4(0,0,0,1); o.%s = v.%s; "
             "return o; }",
             semantic, dst[strlen(src_swz)], src_swz);
    if (FAILED(g_compile(src, strlen(src), "dbg", NULL, NULL, "main", "ps_2_0", 0, 0, &code,
                         &err))) {
        fprintf(stderr, "ps compile %s: %s\n", semantic,
                err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?");
        exit(2);
    }
    IDirect3DDevice9_CreatePixelShader(g_dev, ID3D10Blob_GetBufferPointer(code), &ps);
    ID3D10Blob_Release(code);
    return ps;
}

typedef struct {
    const char *semantic, *mask;
} output;

static const output out_lit_shadow[] = {
    { "COLOR0", "xyz" },    { "COLOR0", "w" },      { "COLOR1", "xyz" },    { "COLOR1", "w" },
    { "TEXCOORD0", "xy" },  { "TEXCOORD1", "xy" },  { "TEXCOORD2", "xy" },  { "TEXCOORD3", "xy" },
    { "TEXCOORD4", "xyz" }, { "TEXCOORD4", "w" },   { "TEXCOORD5", "xyz" },
};
static const output out_lit[] = {
    { "COLOR0", "xyz" },   { "COLOR0", "w" },     { "COLOR1", "xyz" },   { "COLOR1", "w" },
    { "TEXCOORD0", "xy" }, { "TEXCOORD1", "xy" }, { "TEXCOORD2", "xy" },
};
static const output out_smap[] = { { "COLOR0", "x" }, { "TEXCOORD0", "xy" }, { "TEXCOORD1", "x" } };
/* Family 5; its oFog is checked separately (fog_check). */
static const output out_nm_low[] = {
    { "COLOR0", "xyz" }, { "COLOR0", "w" }, { "TEXCOORD0", "xy" }, { "TEXCOORD1", "xy" },
};

static void set4(float (*c)[4], int reg, float x, float y, float z, float w)
{
    if (reg < 0)
        return;
    c[reg][0] = x, c[reg][1] = y, c[reg][2] = z, c[reg][3] = w;
}

/* Instance k's data at regs (bones: rotation, translation per bone; rigid:
 * three world rows), in the game's layout. */
static void instance_data(const pf_inst_variant *v, int k, int bones, float (*out)[4],
                          unsigned seed)
{
    float cx = (float)(k % TILES) + 0.5f, cy = (float)(k / TILES) + 0.5f;
    unsigned s = seed * 7919u + (unsigned)k * 104729u;
    int b;
    for (b = 0; b < (v->joints ? bones : 1); b++) {
        float q[4] = { frand(&s) - 0.5f, frand(&s) - 0.5f, frand(&s) - 0.5f, frand(&s) + 0.2f };
        float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        float t[3] = { cx + (frand(&s) - 0.5f) * 0.05f, cy + (frand(&s) - 0.5f) * 0.05f,
                       frand(&s) * 0.5f };
        int i;
        for (i = 0; i < 4; i++)
            q[i] /= l;
        if (getenv("INST_GPU_IDENTITY"))
            q[0] = q[1] = q[2] = 0, q[3] = 1;
        if (v->joints) {
            memcpy(out[2 * b], q, sizeof q);
            out[2 * b + 1][0] = t[0], out[2 * b + 1][1] = t[1], out[2 * b + 1][2] = t[2];
            out[2 * b + 1][3] = 0;
        } else {
            float x = q[0], y = q[1], z = q[2], w = q[3];
            float m[3][4] = { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), t[0] },
                              { 2 * (x * y + z * w), 1 - 2 * (z * z + x * x), 2 * (y * z - x * w), t[1] },
                              { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (y * y + x * x), t[2] } };
            memcpy(out, m, sizeof m);
        }
    }
}

static void globals(const pf_inst_variant *v, float (*c)[4], unsigned seed)
{
    const short *r = v->roles;
    unsigned s = seed;
    int i;
    float d[3][3];
    for (i = 0; i < 3; i++) {
        d[i][0] = frand(&s) - 0.5f, d[i][1] = frand(&s) - 0.5f, d[i][2] = frand(&s) + 0.1f;
        norm3(d[i]);
    }
    /* World x, y in 0..TILES map to clip -1..1; z to depth 0.25..0.75. */
    if (v->family == 3) {
        set4(c, r[PF_ROLE_VIEW] + 0, 1, 0, 0, 0);
        set4(c, r[PF_ROLE_VIEW] + 1, 0, 1, 0, 0);
        set4(c, r[PF_ROLE_VIEW] + 2, 0, 0, 1, 0);
        set4(c, r[PF_ROLE_VIEW] + 3, 0, 0, 0, 1);
        set4(c, r[PF_ROLE_PROJ] + 0, 2.0f / TILES, 0, 0, -1);
        set4(c, r[PF_ROLE_PROJ] + 1, 0, 2.0f / TILES, 0, -1);
        set4(c, r[PF_ROLE_PROJ] + 2, 0, 0, 0.3f, 0.25f);
        set4(c, r[PF_ROLE_PROJ] + 3, 0.01f, 0.02f, 0.05f, 1.1f);
        set4(c, r[PF_ROLE_OPAC], 0.85f, 0, 0, 0);
        set4(c, r[PF_ROLE_TT0OFF], 0.12f, 0.31f, 0, 0);
        set4(c, r[PF_ROLE_TAOFF], 0.25f, 0.5f, 0, 0);
        set4(c, r[PF_ROLE_BMFLAG], 0, 0, 0, 0);
        set4(c, r[PF_ROLE_TT0], 1.5f, 0.75f, 0.2f, 0.1f);
        return;
    }
    set4(c, r[PF_ROLE_VP] + 0, 2.0f / TILES, 0, 0, -1);
    set4(c, r[PF_ROLE_VP] + 1, 0, 2.0f / TILES, 0, -1);
    set4(c, r[PF_ROLE_VP] + 2, 0, 0, 0.3f, 0.25f);
    set4(c, r[PF_ROLE_VP] + 3, 0.01f, 0.02f, 0.05f, 1.1f);
    set4(c, r[PF_ROLE_RCPRANGE], 0.2f, 0, 0, 0);
    set4(c, r[PF_ROLE_AMB], 0.2f, 0.25f, 0.3f, 0);
    set4(c, r[PF_ROLE_OPAC], 0.9f, 0, 0, 0);
    set4(c, r[PF_ROLE_FOGRCP], 0.15f, 0, 0, 0);
    for (i = 0; i < 3; i++) {
        set4(c, r[PF_ROLE_DL] + 2 * i, 0.7f - 0.2f * i, 0.5f, 0.3f + 0.1f * i, 0);
        set4(c, r[PF_ROLE_DL] + 2 * i + 1, d[i][0], d[i][1], d[i][2], 0);
    }
    set4(c, r[PF_ROLE_FOG] + 0, 1, 0, 0, 0);
    set4(c, r[PF_ROLE_FOG] + 1, 0.4f, 0.5f, 0.6f, 1);
    set4(c, r[PF_ROLE_FOG] + 2, 3.0f, 0, 0, 0);
    set4(c, r[PF_ROLE_VIEWI] + 0, 1, 0, 0, 2.0f);
    set4(c, r[PF_ROLE_VIEWI] + 1, 0, 1, 0, 1.5f);
    set4(c, r[PF_ROLE_VIEWI] + 2, 0, 0, 1, 6.0f);
    set4(c, r[PF_ROLE_PL] + 0, 1.0f, 0.6f, 0.3f, 0);
    set4(c, r[PF_ROLE_PL] + 1, 2.2f, 1.8f, 1.5f, 0);
    set4(c, r[PF_ROLE_CD], 0.8f, 0.75f, 0.7f, 0);
    set4(c, r[PF_ROLE_CS], 0.6f, 0.55f, 0.5f, 0);
    set4(c, r[PF_ROLE_SHIN], 12.0f, 0, 0, 0);
    set4(c, r[PF_ROLE_SHROUD], 0.25f, 0.2f, 0.1f, -0.3f);
    set4(c, r[PF_ROLE_HCE], 0.7f, 0, 0, 0);
    set4(c, r[PF_ROLE_MAP0OFF], 0.1f, 0.2f, 0, 0);
    set4(c, r[PF_ROLE_TT0], 1.5f, 0.5f, 0.3f, 0.2f);
    set4(c, r[PF_ROLE_MAP1OFF], 0.3f, 0.05f, 0, 0);
    set4(c, r[PF_ROLE_TT1], 0.8f, 1.25f, 0.1f, 0.4f);
    set4(c, r[PF_ROLE_SI] + 0, 0.2f, 0.05f, 0.01f, 0.4f);
    set4(c, r[PF_ROLE_SI] + 1, -0.03f, 0.22f, 0.02f, 0.45f);
    set4(c, r[PF_ROLE_SI] + 2, 0.01f, 0.02f, 0.3f, 0.2f);
    set4(c, r[PF_ROLE_SI] + 3, 0, 0, 0.01f, 1);
    set4(c, r[PF_ROLE_CLOUD], 0.3f, 0.2f, 0.05f, 0.07f);
    set4(c, r[PF_ROLE_CLOUDOFF], 0.05f, 0.07f, 0, 0);
    set4(c, r[PF_ROLE_DIFF], 0.85f, 0.8f, 0.6f, 1);
    set4(c, r[PF_ROLE_OPACOV], 0.65f, 0, 0, 0);
}

static IDirect3DSurface9 *g_rt, *g_sys;

static void clear(void)
{
    D3DRECT r = { 0, 0, RT, RT };
    (void)r;
    /* Float targets: clear by drawing nothing; use ColorFill-free path. */
    IDirect3DDevice9_Clear(g_dev, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
}

static float *readback(void)
{
    static float img[RT * RT * 4];
    D3DLOCKED_RECT lr;
    int y;
    IDirect3DDevice9_GetRenderTargetData(g_dev, g_rt, g_sys);
    IDirect3DSurface9_LockRect(g_sys, &lr, NULL, D3DLOCK_READONLY);
    for (y = 0; y < RT; y++)
        memcpy(img + y * RT * 4, (char *)lr.pBits + y * lr.Pitch, RT * 16);
    IDirect3DSurface9_UnlockRect(g_sys);
    return img;
}

static unsigned float_bits(float f)
{
    unsigned u;
    memcpy(&u, &f, 4);
    return u;
}

static const unsigned char *g_fx;
static unsigned g_fx_size;
static IDirect3DSurface9 *g_srt, *g_ssys, *g_sds;
static IDirect3DTexture9 *g_alpha;

/* Shader number n of defaultw3d.fxo (vertex and pixel shaders counted
 * together, in file order, as tools/inst_shaders.py numbers them). */
static const unsigned char *find_shader(unsigned n)
{
    unsigned i = 4, k = 0, tok, len, end, ver;
    while (i + 8 <= g_fx_size) {
        memcpy(&tok, g_fx + i, 4);
        ver = tok & 0xFFFF;
        if ((tok >> 16 == 0xFFFE || tok >> 16 == 0xFFFF) &&
            (ver == 0x0101 || ver == 0x0104 || ver == 0x0200 || ver == 0x0201 || ver == 0x0300)) {
            memcpy(&len, g_fx + i - 4, 4);
            if (len >= 8 && len <= g_fx_size - i) {
                memcpy(&end, g_fx + i + len - 4, 4);
                if (end == 0xFFFF) {
                    if (k++ == n)
                        return g_fx + i;
                    i += len;
                    continue;
                }
            }
        }
        i += 4;
    }
    return NULL;
}

static unsigned *readback_depth(void)
{
    static unsigned img[RT * RT];
    D3DLOCKED_RECT lr;
    int y;
    IDirect3DDevice9_GetRenderTargetData(g_dev, g_srt, g_ssys);
    IDirect3DSurface9_LockRect(g_ssys, &lr, NULL, D3DLOCK_READONLY);
    for (y = 0; y < RT; y++)
        memcpy(img + y * RT, (char *)lr.pBits + y * lr.Pitch, RT * 4);
    IDirect3DSurface9_UnlockRect(g_ssys);
    return img;
}

/* The shadow-map pass as the game draws it: with each of the game's shadow
 * pixel shaders (0: depth only, 2: alpha test on the vertex alpha, 3: on the
 * texture's alpha too) into an R32F map with a depth buffer, cleared to the
 * far depth. The game's shader, K draws, against ours, one instanced draw;
 * depths compared with the plugin's verify compare. */
static int shadow_pass(const pf_inst_variant *v, IDirect3DVertexShader9 *ea,
                       IDirect3DVertexShader9 *ours, IDirect3DVertexDeclaration9 *decl_ea,
                       IDirect3DVertexDeclaration9 *decl_ours, IDirect3DVertexBuffer9 *vb,
                       IDirect3DVertexBuffer9 *ivb, float (*cref)[4], float (*cours)[4], int K,
                       int per)
{
    static const unsigned ps_index[3] = { 0, 2, 3 };
    static unsigned ref[RT * RT];
    unsigned p, n, k;
    int failed = 0;

    if (!g_srt || !g_ssys || !g_sds) {
        printf("  %-30s shadow pass skipped (no R32F target)\n", v->name);
        return 0;
    }
    IDirect3DDevice9_SetRenderTarget(g_dev, 0, g_srt);
    IDirect3DDevice9_SetDepthStencilSurface(g_dev, g_sds);
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_ZENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_ZWRITEENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    IDirect3DDevice9_SetTexture(g_dev, 0, (IDirect3DBaseTexture9 *)g_alpha);
    IDirect3DDevice9_SetSamplerState(g_dev, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(g_dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    for (p = 0; p < 3 && !failed; p++) {
        const unsigned char *code = find_shader(ps_index[p]);
        IDirect3DPixelShader9 *ps = NULL;
        unsigned *b, covered = 0, edge = 0, bad = 0, exact = 0, maxd = 0;
        if (code)
            IDirect3DDevice9_CreatePixelShader(g_dev, (const DWORD *)code, &ps);
        if (!ps) {
            printf("  %-30s FAIL (the game's shadow pixel shader %u)\n", v->name, ps_index[p]);
            failed = 1;
            break;
        }
        IDirect3DDevice9_SetPixelShader(g_dev, ps);

        IDirect3DDevice9_BeginScene(g_dev);
        IDirect3DDevice9_Clear(g_dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFFFFFFFF,
                               1.0f, 0);
        IDirect3DDevice9_SetVertexShader(g_dev, ea);
        IDirect3DDevice9_SetVertexDeclaration(g_dev, decl_ea);
        IDirect3DDevice9_SetStreamSource(g_dev, 0, vb, 0, sizeof(vtx));
        for (k = 0; k < (unsigned)K; k++) {
            int base = v->joints ? v->roles[PF_ROLE_BONES] : v->roles[PF_ROLE_WORLD];
            memcpy(cref + base, cours + v->ir_reg + k * per, (size_t)per * 16);
            IDirect3DDevice9_SetVertexShaderConstantF(g_dev, 0, &cref[0][0], 256);
            IDirect3DDevice9_DrawIndexedPrimitive(g_dev, D3DPT_TRIANGLELIST, 0, 0,
                                                  (GRID + 1) * (GRID + 1), 0, GRID * GRID * 2);
        }
        IDirect3DDevice9_EndScene(g_dev);
        memcpy(ref, readback_depth(), sizeof ref);

        IDirect3DDevice9_BeginScene(g_dev);
        IDirect3DDevice9_Clear(g_dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFFFFFFFF,
                               1.0f, 0);
        IDirect3DDevice9_SetVertexShader(g_dev, ours);
        IDirect3DDevice9_SetVertexDeclaration(g_dev, decl_ours);
        IDirect3DDevice9_SetStreamSource(g_dev, 1, ivb, 0, 4);
        IDirect3DDevice9_SetStreamSourceFreq(g_dev, 0, D3DSTREAMSOURCE_INDEXEDDATA | (UINT)K);
        IDirect3DDevice9_SetStreamSourceFreq(g_dev, 1, D3DSTREAMSOURCE_INSTANCEDATA | 1u);
        IDirect3DDevice9_SetVertexShaderConstantF(g_dev, 0, &cours[0][0], 256);
        IDirect3DDevice9_DrawIndexedPrimitive(g_dev, D3DPT_TRIANGLELIST, 0, 0,
                                              (GRID + 1) * (GRID + 1), 0, GRID * GRID * 2);
        IDirect3DDevice9_EndScene(g_dev);
        IDirect3DDevice9_SetStreamSourceFreq(g_dev, 0, 1);
        IDirect3DDevice9_SetStreamSourceFreq(g_dev, 1, 1);
        IDirect3DDevice9_SetStreamSource(g_dev, 1, NULL, 0, 0);
        b = readback_depth();

        for (n = 0; n < RT * RT; n++) {
            int ca = ref[n] != PF_DEPTH_CLEAR, cb = b[n] != PF_DEPTH_CLEAR;
            unsigned d;
            if (ca != cb) {
                edge++;
                continue;
            }
            if (!ca)
                continue;
            covered++;
            d = pf_depth_diff(ref[n], b[n]);
            exact += ref[n] == b[n];
            maxd = d > maxd ? d : maxd;
            bad += d > PF_DEPTH_TOLERANCE;
        }
        if (covered < 1000 || bad > covered / 1000 || edge > covered / 100)
            failed = 1;
        if (g_verbose || failed)
            printf("  %-30s shadow ps %u covered %6u edge %4u bad %4u exact %6u max %u/2^24\n",
                   v->name, ps_index[p], covered, edge, bad, exact, maxd);
        IDirect3DDevice9_SetPixelShader(g_dev, NULL);
        IDirect3DPixelShader9_Release(ps);
    }
    IDirect3DDevice9_SetTexture(g_dev, 0, NULL);
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_ZENABLE, FALSE);
    IDirect3DDevice9_SetDepthStencilSurface(g_dev, NULL);
    IDirect3DDevice9_SetRenderTarget(g_dev, 0, g_rt);
    return failed;
}

/* oFog can't be read by a ps_2_0 debug shader: draw black with fixed-function
 * vertex fog onto a white fog colour, so red = 1 - fog, game against ours. */
static int fog_check(const pf_inst_variant *v, IDirect3DVertexShader9 *ea,
                     IDirect3DVertexShader9 *ours, IDirect3DVertexDeclaration9 *decl_ea,
                     IDirect3DVertexDeclaration9 *decl_ours, IDirect3DVertexBuffer9 *vb,
                     IDirect3DVertexBuffer9 *ivb, float (*cref)[4], float (*cours)[4], int K,
                     int per)
{
    static const char src[] = "float4 main() : COLOR { return float4(0, 0, 0, 1); }";
    static float ref[RT * RT * 4];
    ID3DBlob *code = NULL, *err = NULL;
    IDirect3DPixelShader9 *ps = NULL;
    unsigned covered = 0, edge = 0, bad = 0, foggy = 0, n, k;
    float maxd = 0, *a, *b;
    int failed;

    if (FAILED(g_compile(src, strlen(src), "fog", NULL, NULL, "main", "ps_2_0", 0, 0, &code,
                         &err)))
        return 1;
    IDirect3DDevice9_CreatePixelShader(g_dev, ID3D10Blob_GetBufferPointer(code), &ps);
    ID3D10Blob_Release(code);
    IDirect3DDevice9_SetPixelShader(g_dev, ps);
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_FOGENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_FOGCOLOR, 0xFFFFFFFF);
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_FOGTABLEMODE, D3DFOG_NONE);
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_FOGVERTEXMODE, D3DFOG_NONE);

    IDirect3DDevice9_BeginScene(g_dev);
    clear();
    IDirect3DDevice9_SetVertexShader(g_dev, ea);
    IDirect3DDevice9_SetVertexDeclaration(g_dev, decl_ea);
    IDirect3DDevice9_SetStreamSource(g_dev, 0, vb, 0, sizeof(vtx));
    for (k = 0; k < (unsigned)K; k++) {
        int base = v->joints ? v->roles[PF_ROLE_BONES] : v->roles[PF_ROLE_WORLD];
        memcpy(cref + base, cours + v->ir_reg + k * per, (size_t)per * 16);
        IDirect3DDevice9_SetVertexShaderConstantF(g_dev, 0, &cref[0][0], 256);
        IDirect3DDevice9_DrawIndexedPrimitive(g_dev, D3DPT_TRIANGLELIST, 0, 0,
                                              (GRID + 1) * (GRID + 1), 0, GRID * GRID * 2);
    }
    IDirect3DDevice9_EndScene(g_dev);
    memcpy(ref, readback(), sizeof ref);

    IDirect3DDevice9_BeginScene(g_dev);
    clear();
    IDirect3DDevice9_SetVertexShader(g_dev, ours);
    IDirect3DDevice9_SetVertexDeclaration(g_dev, decl_ours);
    IDirect3DDevice9_SetStreamSource(g_dev, 1, ivb, 0, 4);
    IDirect3DDevice9_SetStreamSourceFreq(g_dev, 0, D3DSTREAMSOURCE_INDEXEDDATA | (UINT)K);
    IDirect3DDevice9_SetStreamSourceFreq(g_dev, 1, D3DSTREAMSOURCE_INSTANCEDATA | 1u);
    IDirect3DDevice9_SetVertexShaderConstantF(g_dev, 0, &cours[0][0], 256);
    IDirect3DDevice9_DrawIndexedPrimitive(g_dev, D3DPT_TRIANGLELIST, 0, 0,
                                          (GRID + 1) * (GRID + 1), 0, GRID * GRID * 2);
    IDirect3DDevice9_EndScene(g_dev);
    IDirect3DDevice9_SetStreamSourceFreq(g_dev, 0, 1);
    IDirect3DDevice9_SetStreamSourceFreq(g_dev, 1, 1);
    IDirect3DDevice9_SetStreamSource(g_dev, 1, NULL, 0, 0);
    b = readback();
    a = ref;
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_FOGENABLE, FALSE);
    IDirect3DDevice9_SetPixelShader(g_dev, NULL);
    IDirect3DPixelShader9_Release(ps);

    for (n = 0; n < RT * RT; n++) {
        int ca = a[n * 4 + 3] != 0, cb = b[n * 4 + 3] != 0;
        float d;
        if (ca != cb) {
            edge++;
            continue;
        }
        if (!ca)
            continue;
        covered++;
        foggy += a[n * 4] > 0.05f && a[n * 4] < 0.95f;
        d = fabsf(a[n * 4] - b[n * 4]);
        maxd = d > maxd ? d : maxd;
        bad += d > 2e-3f + 2e-3f * fabsf(a[n * 4]);
    }
    /* Fog must have been applied (red between 0 and 1) for the check to mean anything. */
    failed = covered < 1000 || foggy < covered / 2 || bad > covered / 1000 || edge > covered / 100;
    if (g_verbose || failed)
        printf("  %-30s %-9s covered %6u edge %4u bad %5u fogged %6u max %.6f\n", v->name, "FOG",
               covered, edge, bad, foggy, maxd);
    return failed;
}

static int run_variant(const pf_inst_variant *v, const unsigned char *ea_code, unsigned seed)
{
    static vtx verts[(GRID + 1) * (GRID + 1)];
    static WORD idx[GRID * GRID * 6];
    static float cref[256][4], cours[256][4];
    const output *outs = v->family == 1                      ? out_lit_shadow
                         : v->family == 2 || v->family == 4 ? out_lit
                         : v->family == 5                   ? out_nm_low
                                                            : out_smap;
    unsigned nout = v->family == 1                      ? 11
                    : v->family == 2 || v->family == 4 ? 7
                    : v->family == 5                   ? 4
                                                       : 3;
    int bones = v->joints ? (v->ea_bones < 12 ? v->ea_bones : 12) : 0;
    int per = v->joints ? 2 * bones : 3;
    int K = v->ir_count / per;
    IDirect3DVertexShader9 *ea = NULL, *ours = NULL;
    IDirect3DVertexDeclaration9 *decl_ea = NULL, *decl_ours = NULL;
    IDirect3DVertexBuffer9 *vb = NULL, *ivb = NULL;
    IDirect3DIndexBuffer9 *ib = NULL;
    unsigned s = seed, i, o, k;
    int failed = 0;
    void *p;
    D3DVERTEXELEMENT9 el[] = {
        { 0, 0, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 1 },
        { 0, 24, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_NORMAL, 0 },
        { 0, 36, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_NORMAL, 1 },
        { 0, 48, D3DDECLTYPE_D3DCOLOR, 0, D3DDECLUSAGE_BLENDINDICES, 0 },
        { 0, 52, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_BLENDWEIGHT, 0 },
        { 0, 60, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 0 },
        { 0, 68, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 1 },
        { 0, 76, D3DDECLTYPE_D3DCOLOR, 0, D3DDECLUSAGE_COLOR, 0 },
        { 1, 0, D3DDECLTYPE_FLOAT1, 0, D3DDECLUSAGE_TEXCOORD, 7 },
        D3DDECL_END(),
    };
    D3DVERTEXELEMENT9 el_ea[11];

    if (K > TILES * TILES)
        K = TILES * TILES;
    if (K < 2) {
        printf("%-30s skipped (fewer than 2 instances fit)\n", v->name);
        return 0;
    }
    for (i = 0; i <= GRID; i++) {
        for (k = 0; k <= GRID; k++) {
            vtx *t = &verts[i * (GRID + 1) + k];
            float x = ((float)k / GRID - 0.5f) * 0.6f, y = ((float)i / GRID - 0.5f) * 0.6f;
            int b0 = bones ? (int)(frand(&s) * bones) % bones : 0;
            int b1 = bones ? (int)(frand(&s) * bones) % bones : 0;
            float w = frand(&s);
            t->p0[0] = x, t->p0[1] = y, t->p0[2] = (frand(&s) - 0.5f) * 0.2f;
            t->p1[0] = x + (frand(&s) - 0.5f) * 0.05f, t->p1[1] = y + (frand(&s) - 0.5f) * 0.05f;
            t->p1[2] = t->p0[2];
            t->n0[0] = frand(&s) - 0.5f, t->n0[1] = frand(&s) - 0.5f, t->n0[2] = frand(&s) + 0.2f;
            t->n1[0] = frand(&s) - 0.5f, t->n1[1] = frand(&s) - 0.5f, t->n1[2] = frand(&s) + 0.2f;
            norm3(t->n0);
            norm3(t->n1);
            /* D3DCOLOR: blue byte reads as .z (first joint), green as .y. */
            t->bidx = (DWORD)b1 << 8 | (DWORD)b0;
            t->bw[0] = w, t->bw[1] = 1 - w;
            t->uv0[0] = frand(&s), t->uv0[1] = frand(&s);
            t->uv1[0] = frand(&s), t->uv1[1] = frand(&s);
            t->col = D3DCOLOR_ARGB((int)(frand(&s) * 255), (int)(frand(&s) * 255),
                                   (int)(frand(&s) * 255), (int)(frand(&s) * 255));
        }
    }
    for (i = 0, o = 0; i < GRID; i++) {
        for (k = 0; k < GRID; k++) {
            WORD a = (WORD)(i * (GRID + 1) + k), b = a + 1, c = a + GRID + 1, d = c + 1;
            idx[o++] = a, idx[o++] = b, idx[o++] = c, idx[o++] = b, idx[o++] = d, idx[o++] = c;
        }
    }
    memcpy(el_ea, el, sizeof el_ea);
    el_ea[9] = (D3DVERTEXELEMENT9)D3DDECL_END();
    IDirect3DDevice9_CreateVertexShader(g_dev, (const DWORD *)ea_code, &ea);
    IDirect3DDevice9_CreateVertexShader(g_dev, (const DWORD *)v->code, &ours);
    IDirect3DDevice9_CreateVertexDeclaration(g_dev, el_ea, &decl_ea);
    IDirect3DDevice9_CreateVertexDeclaration(g_dev, el, &decl_ours);
    IDirect3DDevice9_CreateVertexBuffer(g_dev, sizeof verts, 0, 0, D3DPOOL_MANAGED, &vb, NULL);
    IDirect3DDevice9_CreateVertexBuffer(g_dev, 4 * 64, 0, 0, D3DPOOL_MANAGED, &ivb, NULL);
    IDirect3DDevice9_CreateIndexBuffer(g_dev, sizeof idx, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib,
                                       NULL);
    if (!ea || !ours || !decl_ea || !decl_ours || !vb || !ivb || !ib) {
        printf("%-30s FAIL (creating objects)\n", v->name);
        return 1;
    }
    IDirect3DVertexBuffer9_Lock(vb, 0, 0, &p, 0);
    memcpy(p, verts, sizeof verts);
    IDirect3DVertexBuffer9_Unlock(vb);
    IDirect3DIndexBuffer9_Lock(ib, 0, 0, &p, 0);
    memcpy(p, idx, sizeof idx);
    IDirect3DIndexBuffer9_Unlock(ib);
    IDirect3DVertexBuffer9_Lock(ivb, 0, 0, &p, 0);
    for (k = 0; k < 64; k++)
        ((float *)p)[k] = (float)(k * per);
    IDirect3DVertexBuffer9_Unlock(ivb);

    memset(cref, 0, sizeof cref);
    globals(v, cref, seed);
    memcpy(cours, cref, sizeof cours);
    for (k = 0; k < (unsigned)K; k++)
        instance_data(v, (int)k, bones, cours + v->ir_reg + k * per, seed);

    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_ZENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetIndices(g_dev, ib);
    for (o = 0; o < nout; o++) {
        IDirect3DPixelShader9 *ps = debug_ps(outs[o].semantic, outs[o].mask);
        float *a, *b;
        static float ref[RT * RT * 4];
        unsigned bad = 0, covered = 0, edge = 0, n;
        float maxd = 0;
        IDirect3DDevice9_SetPixelShader(g_dev, ps);

        /* The game's shader, one instance per draw, its bones at its registers. */
        IDirect3DDevice9_BeginScene(g_dev);
        clear();
        IDirect3DDevice9_SetVertexShader(g_dev, ea);
        IDirect3DDevice9_SetVertexDeclaration(g_dev, decl_ea);
        IDirect3DDevice9_SetStreamSource(g_dev, 0, vb, 0, sizeof(vtx));
        IDirect3DDevice9_SetStreamSourceFreq(g_dev, 0, 1);
        for (k = 0; k < (unsigned)K; k++) {
            int base = v->joints ? v->roles[PF_ROLE_BONES] : v->roles[PF_ROLE_WORLD];
            memcpy(cref + base, cours + v->ir_reg + k * per, (size_t)per * 16);
            IDirect3DDevice9_SetVertexShaderConstantF(g_dev, 0, &cref[0][0], 256);
            IDirect3DDevice9_DrawIndexedPrimitive(g_dev, D3DPT_TRIANGLELIST, 0, 0,
                                                  (GRID + 1) * (GRID + 1), 0, GRID * GRID * 2);
        }
        IDirect3DDevice9_EndScene(g_dev);
        memcpy(ref, readback(), sizeof ref);

        /* Ours: all instances in one draw. */
        IDirect3DDevice9_BeginScene(g_dev);
        clear();
        IDirect3DDevice9_SetVertexShader(g_dev, ours);
        IDirect3DDevice9_SetVertexDeclaration(g_dev, decl_ours);
        IDirect3DDevice9_SetStreamSource(g_dev, 1, ivb, 0, 4);
        IDirect3DDevice9_SetStreamSourceFreq(g_dev, 0, D3DSTREAMSOURCE_INDEXEDDATA | (UINT)K);
        IDirect3DDevice9_SetStreamSourceFreq(g_dev, 1, D3DSTREAMSOURCE_INSTANCEDATA | 1u);
        IDirect3DDevice9_SetVertexShaderConstantF(g_dev, 0, &cours[0][0], 256);
        IDirect3DDevice9_DrawIndexedPrimitive(g_dev, D3DPT_TRIANGLELIST, 0, 0,
                                              (GRID + 1) * (GRID + 1), 0, GRID * GRID * 2);
        IDirect3DDevice9_EndScene(g_dev);
        IDirect3DDevice9_SetStreamSourceFreq(g_dev, 0, 1);
        IDirect3DDevice9_SetStreamSourceFreq(g_dev, 1, 1);
        IDirect3DDevice9_SetStreamSource(g_dev, 1, NULL, 0, 0);
        b = readback();
        a = ref;

        for (n = 0; n < RT * RT; n++) {
            int ca = a[n * 4 + 3] != 0, cb = b[n * 4 + 3] != 0, c;
            if (ca != cb) {
                edge++;
                continue;
            }
            if (!ca)
                continue;
            covered++;
            for (c = 0; c < 4; c++) {
                float d = fabsf(a[n * 4 + c] - b[n * 4 + c]);
                float tol = 2e-3f + 2e-3f * fabsf(a[n * 4 + c]);
                if (d > maxd)
                    maxd = d;
                if (d > tol) {
                    bad++;
                    break;
                }
            }
        }
        if (covered < 1000 || bad > covered / 1000 || edge > covered / 100)
            failed = 1;
        if (g_verbose || failed)
            printf("  %-30s %-9s covered %6u edge %4u bad %5u max %.6f\n", v->name,
                   outs[o].semantic, covered, edge, bad, maxd);
        IDirect3DDevice9_SetPixelShader(g_dev, NULL);
        IDirect3DPixelShader9_Release(ps);
        if (failed)
            break;
    }
    if (!failed && v->family == 3)
        failed = shadow_pass(v, ea, ours, decl_ea, decl_ours, vb, ivb, cref, cours, K, per);
    if (!failed && v->family == 5)
        failed = fog_check(v, ea, ours, decl_ea, decl_ours, vb, ivb, cref, cours, K, per);
    printf("%-30s %s (game shader %u, %d instances of %d bone(s))\n", v->name,
           failed ? "FAIL" : "ok", v->ea_index, K, bones);
    IDirect3DDevice9_SetVertexShader(g_dev, NULL);
    IDirect3DDevice9_SetIndices(g_dev, NULL);
    IDirect3DDevice9_SetStreamSource(g_dev, 0, NULL, 0, 0);
    IDirect3DVertexShader9_Release(ea);
    IDirect3DVertexShader9_Release(ours);
    IDirect3DVertexDeclaration9_Release(decl_ea);
    IDirect3DVertexDeclaration9_Release(decl_ours);
    IDirect3DVertexBuffer9_Release(vb);
    IDirect3DVertexBuffer9_Release(ivb);
    IDirect3DIndexBuffer9_Release(ib);
    (void)float_bits;
    return failed;
}

int main(int argc, char **argv)
{
    const char *big_path = "C:\\Jogos\\AgeoftheRing\\rotwk\\Shaders.big";
    unsigned big_size, fx_size, nm_size = 0, i, fails = 0;
    unsigned char *big;
    const unsigned char *fx, *nm;
    HMODULE d3d9, dc;
    create9_fn create9;
    IDirect3D9 *d3d;
    D3DPRESENT_PARAMETERS pp;
    HWND wnd;
    char loaded[MAX_PATH];
    int a;

    for (a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--big") && a + 1 < argc)
            big_path = argv[++a];
        else if (!strcmp(argv[a], "--verbose"))
            g_verbose = 1;
    }
    big = read_file(big_path, &big_size);
    if (!big || !(fx = find_fx(big, big_size, &fx_size))) {
        printf("SKIP: %s not readable (needs the game's Shaders.big)\n", big_path);
        return 0;
    }
    d3d9 = LoadLibraryA("d3d9.dll");
    dc = LoadLibraryA("d3dcompiler_47.dll");
    if (!d3d9 || !dc) {
        printf("SKIP: d3d9.dll or d3dcompiler_47.dll missing\n");
        return 0;
    }
    GetModuleFileNameA(d3d9, loaded, MAX_PATH);
    printf("d3d9: %s\n", loaded);
    create9 = (create9_fn)(void *)GetProcAddress(d3d9, "Direct3DCreate9");
    g_compile = (compile_fn)(void *)GetProcAddress(dc, "D3DCompile");
    d3d = create9(D3D_SDK_VERSION);
    wnd = CreateWindowExA(0, "STATIC", "inst_gpu", WS_POPUP, 0, 0, 64, 64, NULL, NULL, NULL,
                          NULL);
    memset(&pp, 0, sizeof pp);
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow = wnd;
    if (!d3d || FAILED(IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                                               D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev))) {
        printf("SKIP: no hardware D3D9 device\n");
        DestroyWindow(wnd);
        return 0;
    }
    IDirect3DDevice9_CreateRenderTarget(g_dev, RT, RT, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0,
                                        FALSE, &g_rt, NULL);
    IDirect3DDevice9_CreateOffscreenPlainSurface(g_dev, RT, RT, D3DFMT_A32B32G32R32F,
                                                 D3DPOOL_SYSTEMMEM, &g_sys, NULL);
    if (!g_rt || !g_sys) {
        printf("SKIP: no float render target\n");
        return 0;
    }
    IDirect3DDevice9_SetRenderTarget(g_dev, 0, g_rt);
    g_fx = fx;
    g_fx_size = fx_size;
    IDirect3DDevice9_CreateRenderTarget(g_dev, RT, RT, D3DFMT_R32F, D3DMULTISAMPLE_NONE, 0, FALSE,
                                        &g_srt, NULL);
    IDirect3DDevice9_CreateOffscreenPlainSurface(g_dev, RT, RT, D3DFMT_R32F, D3DPOOL_SYSTEMMEM,
                                                 &g_ssys, NULL);
    IDirect3DDevice9_CreateDepthStencilSurface(g_dev, RT, RT, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0,
                                               TRUE, &g_sds, NULL);
    if (SUCCEEDED(IDirect3DDevice9_CreateTexture(g_dev, 16, 16, 1, 0, D3DFMT_A8R8G8B8,
                                                 D3DPOOL_MANAGED, &g_alpha, NULL))) {
        D3DLOCKED_RECT lr;
        int x, y;
        IDirect3DTexture9_LockRect(g_alpha, 0, &lr, NULL, 0);
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++)
                ((DWORD *)((char *)lr.pBits + y * lr.Pitch))[x] =
                    (DWORD)((x * 37 + y * 91) % 256) << 24 | 0x808080;
        IDirect3DTexture9_UnlockRect(g_alpha, 0);
    }

    nm = find_effect(big, big_size, "shaders\\compiled\\normalmapped.fxo", &nm_size);
    for (i = 0; i < PF_INST_VARIANTS; i++) {
        const pf_inst_variant *v = &pf_inst_variants[i];
        const unsigned char *ea = find_ea(fx, fx_size, v->ea_hash, v->ea_size);
        if (!ea && nm)
            ea = find_ea(nm, nm_size, v->ea_hash, v->ea_size);
        if (!ea) {
            printf("%-30s FAIL (game shader %u not found by hash)\n", v->name, v->ea_index);
            fails++;
            continue;
        }
        if (run_variant(v, ea, 12345u + i)) {
            fails++;
            printf("  -> failing game shader index: %u\n", v->ea_index);
        }
    }
    printf("%s: %u of %u variants failed\n", fails ? "FAIL" : "OK", fails, PF_INST_VARIANTS);
    if (g_alpha)
        IDirect3DTexture9_Release(g_alpha);
    if (g_sds)
        IDirect3DSurface9_Release(g_sds);
    if (g_ssys)
        IDirect3DSurface9_Release(g_ssys);
    if (g_srt)
        IDirect3DSurface9_Release(g_srt);
    IDirect3DSurface9_Release(g_sys);
    IDirect3DSurface9_Release(g_rt);
    IDirect3DDevice9_Release(g_dev);
    IDirect3D9_Release(d3d);
    DestroyWindow(wnd);
    free(big);
    return fails ? 1 : 0;
}
