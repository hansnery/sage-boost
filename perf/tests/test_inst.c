/* The instancing state machine on a real D3D9 device, with fake game objects.
 *
 * A fake batch renderer calls pf_inst_enter exactly as the hooked call site
 * would (saved registers, the batch's frame, the method passed by value), and
 * a fake Render_With_FX_Material uploads a mesh's bones and draws through a
 * fake DrawIndexedPrimitive site with the game's own vertex shader (read
 * from the local Shaders.big). The same scene drawn with every mesh stock
 * and with instancing must match, the game's state must be put back, the
 * method references must balance, and the reasons must add up. Verify mode
 * must report its samples as matching.
 *
 * Also checks the D3D9 vtable slots in d3d9vt.h against the SDK header.
 * Skips (passes) without the game's Shaders.big or a hardware device. */
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <d3dcommon.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d3d9vt.h"
#include "depthcmp.h"
#include "inst.h"
#include "inst_shaders.h"
#include "log.h"
#include "stubs.h"

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

#define RT 256
#define GRID 8
#define MESHES 9
#define BONES 6
#define PIVOTS 8
#define SITE_MESH 16
#define SITE_DIP 17

typedef HRESULT(WINAPI *compile_fn)(const void *, SIZE_T, const char *, const void *, void *,
                                    const char *, const char *, UINT, UINT, ID3DBlob **,
                                    ID3DBlob **);
static IDirect3DDevice9 *g_dev;
static IDirect3DVertexShader9 *g_ea;
static IDirect3DVertexDeclaration9 *g_decl;
static IDirect3DVertexBuffer9 *g_vb;
static IDirect3DIndexBuffer9 *g_ib;
static const pf_inst_variant *g_v;

/* Fake game objects, laid out at the offsets the plugin reads. */
typedef struct {
    unsigned char b[0x320];
} fake_mesh;
static unsigned char g_model[0x100], g_geom[0x40];
static short g_bonemap[BONES];
typedef struct {
    void **vt;
    void *htree;
} fake_container;
static unsigned char g_htree[MESHES][0x18];
static unsigned char g_pivots[MESHES][PIVOTS * 0x58];
static fake_container g_cont[MESHES];
static fake_mesh g_mesh[MESHES];
typedef struct {
    void *vt;
    unsigned refs;
} fake_method;
static fake_method g_method = { 0, 5 };
static void *g_cont_vt[0xE8 / 4 + 1];
static unsigned g_stock_draws;

static void *__attribute__((thiscall)) get_htree(fake_container *self)
{
    return self->htree;
}

/* What the hooked DrawIndexedPrimitive call site does: the stub's view of
 * the stack (slot[0] the return address, then the arguments), then the call. */
static void site_draw(UINT prims)
{
    void *a[16] = { 0 };
    void **slot = a + 8;
    void *next = PF_VT(g_dev)[PF_DEV_DRAW_INDEXED];
    slot[1] = g_dev;
    slot[2] = (void *)(UINT_PTR)D3DPT_TRIANGLELIST;
    slot[3] = 0;
    slot[4] = 0;
    slot[5] = (void *)(UINT_PTR)((GRID + 1) * (GRID + 1));
    slot[6] = 0;
    slot[7] = (void *)(UINT_PTR)prims;
    next = pf_inst_enter(SITE_DIP, slot, next);
    ((pf_dip_fn)next)(g_dev, D3DPT_TRIANGLELIST, 0, 0, (GRID + 1) * (GRID + 1), 0, prims);
    g_stock_draws++;
}

/* Render_With_FX_Material: the bones at the game's registers, commit, draw;
 * releases its by-value method argument. */
static void __attribute__((thiscall)) fake_render(fake_mesh *mesh, fake_method *method, int count)
{
    float bones[BONES * 2][4];
    fake_container *c = *(fake_container **)(mesh->b + PF_MESH_CONTAINER);
    const unsigned char *piv = *(unsigned char **)((unsigned char *)c->htree + 0x14);
    int j;
    (void)count;
    for (j = 0; j < BONES; j++) {
        memcpy(bones[2 * j], piv + g_bonemap[j] * 0x58 + 0x30, 28);
        bones[2 * j + 1][3] = 0;
    }
    IDirect3DDevice9_SetVertexShaderConstantF(g_dev, g_v->roles[PF_ROLE_BONES], &bones[0][0],
                                              BONES * 2);
    site_draw(GRID * GRID * 2);
    method->refs--;
}

/* The batch renderer's chunk-of-one call for record i of the group of
 * records first .. n - 1 (one record array, so groups can follow each other). */
static void batch_call_from(fake_mesh **meshes, unsigned first, unsigned i, unsigned n,
                            int pass_count, int max_inst)
{
    static unsigned char records[64 * 32];
    unsigned char frame[0x60];
    unsigned char *ebp = frame + 0x40;
    void *a[24] = { 0 };
    void **slot = a + 12;
    void *ret;

    *(unsigned char **)(ebp + 8) = records + n * 32;
    *(int *)(ebp - 0x14) = 0;
    *(int *)(ebp - 0x24) = pass_count;
    *(int *)(ebp - 0x2C) = max_inst;
    slot[-4] = meshes[i];
    slot[-6] = records + first * 32;
    slot[-8] = ebp;
    slot[-10] = records + i * 32;
    slot[1] = &g_method;
    slot[2] = (void *)1;
    g_method.refs++; /* the caller's copy for the by-value argument */
    ret = pf_inst_enter(SITE_MESH, slot, (void *)fake_render);
    if (ret == (void *)fake_render)
        fake_render(meshes[i], &g_method, 1);
    else
        CHECK(ret == pf_skip_thunk(8), "mesh %u: unexpected continuation", i);
}

/* Record i of a group of n starting at record 0. */
static void batch_call(fake_mesh **meshes, unsigned i, unsigned n, int pass_count, int max_inst)
{
    batch_call_from(meshes, 0, i, n, pass_count, max_inst);
}

static void quat_tile(float *dst, unsigned k, unsigned seed)
{
    unsigned s = seed * 2654435761u + k;
    float q[4], l;
    int i;
    for (i = 0; i < 4; i++) {
        s = s * 1664525u + 1013904223u;
        q[i] = (float)((s >> 8) & 0xFFFF) / 65535.0f - 0.5f;
    }
    q[3] += 1.0f;
    l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (i = 0; i < 4; i++)
        dst[i] = q[i] / l;
    dst[4] = (float)(k % 3) + 0.5f + (float)(seed % 7) * 0.01f;
    dst[5] = (float)(k / 3) + 0.5f;
    dst[6] = 0.2f;
}

static void build_scene(void)
{
    unsigned m, p;
    memset(g_model, 0, sizeof g_model);
    memset(g_geom, 0, sizeof g_geom);
    *(unsigned *)(g_model + PF_MODEL_FLAGS) = PF_MODEL_SKIN;
    *(unsigned char **)(g_model + PF_MODEL_GEOMETRY) = g_geom;
    for (p = 0; p < BONES; p++)
        g_bonemap[p] = (short)(PIVOTS - 1 - p);
    *(short **)(g_geom + PF_GEOM_BONES_BEGIN) = g_bonemap;
    *(short **)(g_geom + PF_GEOM_BONES_END) = g_bonemap + BONES;
    /* Joints per vertex, as the game's geometry has it: the bound variant's
     * (1 here), not the length of the bone list. */
    *(int *)(g_geom + PF_GEOM_JOINTS) = g_v->joints;
    g_cont_vt[0xE8 / 4] = (void *)get_htree;
    for (m = 0; m < MESHES; m++) {
        memset(&g_mesh[m], 0, sizeof g_mesh[m]);
        *(unsigned char **)(g_mesh[m].b + PF_MESH_MODEL) = g_model;
        *(fake_container **)(g_mesh[m].b + PF_MESH_CONTAINER) = &g_cont[m];
        g_cont[m].vt = g_cont_vt;
        g_cont[m].htree = g_htree[m];
        *(int *)(g_htree[m] + 0x10) = PIVOTS;
        *(unsigned char **)(g_htree[m] + 0x14) = g_pivots[m];
        for (p = 0; p < PIVOTS; p++)
            quat_tile((float *)(g_pivots[m] + p * 0x58 + 0x30), m, p + 1);
    }
}

static const unsigned char *g_fx;
static unsigned g_fx_size;

static int load_ea(const char *big_path)
{
    static unsigned char *big;
    FILE *f = fopen(big_path, "rb");
    long n;
    unsigned i, count, pos = 16;
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    big = malloc((size_t)n);
    if (fread(big, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return 0;
    }
    fclose(f);
    count = (unsigned)big[8] << 24 | big[9] << 16 | big[10] << 8 | big[11];
    for (i = 0; i < count; i++) {
        unsigned off = (unsigned)big[pos] << 24 | big[pos + 1] << 16 | big[pos + 2] << 8 | big[pos + 3];
        unsigned len = (unsigned)big[pos + 4] << 24 | big[pos + 5] << 16 | big[pos + 6] << 8 | big[pos + 7];
        const char *name = (const char *)big + pos + 8;
        if (!_stricmp(name, "shaders\\compiled\\defaultw3d.fxo")) {
            g_fx = big + off;
            g_fx_size = len;
            return 1;
        }
        pos += 8 + (unsigned)strlen(name) + 1;
    }
    return 0;
}

static const unsigned char *find_ea(const pf_inst_variant *v)
{
    unsigned i, k;
    for (i = 4; i + v->ea_size <= g_fx_size; i += 4) {
        unsigned h = 0x811C9DC5u, len;
        memcpy(&len, g_fx + i - 4, 4);
        if (len != v->ea_size || g_fx[i + 3] != 0xFF || g_fx[i + 2] != 0xFE)
            continue;
        for (k = 0; k < len; k++)
            h = (h ^ g_fx[i + k]) * 0x01000193u;
        if (h == v->ea_hash)
            return g_fx + i;
    }
    return NULL;
}

/* Shader number n of defaultw3d.fxo (vertex and pixel shaders counted
 * together, in file order, as tools/inst_shaders.py numbers them). */
static const unsigned char *find_shader(unsigned n, unsigned *size)
{
    unsigned i = 4, k = 0, tok, len, end;
    while (i + 8 <= g_fx_size) {
        memcpy(&tok, g_fx + i, 4);
        if ((tok >> 16 == 0xFFFE || tok >> 16 == 0xFFFF) &&
            ((tok & 0xFFFF) == 0x0101 || (tok & 0xFFFF) == 0x0104 || (tok & 0xFFFF) == 0x0200 ||
             (tok & 0xFFFF) == 0x0201 || (tok & 0xFFFF) == 0x0300)) {
            memcpy(&len, g_fx + i - 4, 4);
            if (len >= 8 && len <= g_fx_size - i) {
                memcpy(&end, g_fx + i + len - 4, 4);
                if (end == 0xFFFF) {
                    if (k++ == n) {
                        *size = len;
                        return g_fx + i;
                    }
                    i += len;
                    continue;
                }
            }
        }
        i += 4;
    }
    return NULL;
}

static IDirect3DSurface9 *g_rt, *g_sys;

static void draw_frame(int instanced, unsigned *pixels)
{
    fake_mesh *list[MESHES];
    D3DLOCKED_RECT lr;
    unsigned i, y;
    for (i = 0; i < MESHES; i++)
        list[i] = &g_mesh[i];
    IDirect3DDevice9_BeginScene(g_dev);
    IDirect3DDevice9_Clear(g_dev, 0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);
    IDirect3DDevice9_SetVertexShader(g_dev, g_ea);
    IDirect3DDevice9_SetVertexDeclaration(g_dev, g_decl);
    IDirect3DDevice9_SetStreamSource(g_dev, 0, g_vb, 0, 40);
    IDirect3DDevice9_SetIndices(g_dev, g_ib);
    for (i = 0; i < MESHES; i++) {
        if (instanced)
            batch_call(list, i, MESHES, 1, 1);
        else
            fake_render(list[i], &g_method, ((void)g_method.refs++, 1));
    }
    IDirect3DDevice9_EndScene(g_dev);
    IDirect3DDevice9_GetRenderTargetData(g_dev, g_rt, g_sys);
    IDirect3DSurface9_LockRect(g_sys, &lr, NULL, D3DLOCK_READONLY);
    for (y = 0; y < RT; y++)
        memcpy(pixels + y * RT, (char *)lr.pBits + y * lr.Pitch, RT * 4);
    IDirect3DSurface9_UnlockRect(g_sys);
}

static void check_vtable_slots(void)
{
#define SLOT(name) (unsigned)(offsetof(IDirect3DDevice9Vtbl, name) / sizeof(void *))
    CHECK(SLOT(Release) == PF_DEV_RELEASE && SLOT(GetDeviceCaps) == PF_DEV_GET_DEVICE_CAPS &&
              SLOT(CreateVertexBuffer) == PF_DEV_CREATE_VERTEX_BUFFER &&
              SLOT(CreateRenderTarget) == PF_DEV_CREATE_RENDER_TARGET &&
              SLOT(CreateDepthStencilSurface) == PF_DEV_CREATE_DEPTH_STENCIL &&
              SLOT(GetRenderTargetData) == PF_DEV_GET_RENDER_TARGET_DATA &&
              SLOT(CreateOffscreenPlainSurface) == PF_DEV_CREATE_OFFSCREEN_PLAIN &&
              SLOT(SetRenderTarget) == PF_DEV_SET_RENDER_TARGET &&
              SLOT(GetRenderTarget) == PF_DEV_GET_RENDER_TARGET &&
              SLOT(SetDepthStencilSurface) == PF_DEV_SET_DEPTH_STENCIL &&
              SLOT(GetDepthStencilSurface) == PF_DEV_GET_DEPTH_STENCIL &&
              SLOT(Clear) == PF_DEV_CLEAR && SLOT(SetViewport) == PF_DEV_SET_VIEWPORT &&
              SLOT(GetViewport) == PF_DEV_GET_VIEWPORT && SLOT(SetScissorRect) == PF_DEV_SET_SCISSOR &&
              SLOT(GetScissorRect) == PF_DEV_GET_SCISSOR &&
              SLOT(DrawIndexedPrimitive) == PF_DEV_DRAW_INDEXED &&
              SLOT(CreateVertexDeclaration) == PF_DEV_CREATE_DECL &&
              SLOT(SetVertexDeclaration) == PF_DEV_SET_DECL &&
              SLOT(GetVertexDeclaration) == PF_DEV_GET_DECL &&
              SLOT(CreateVertexShader) == PF_DEV_CREATE_VS && SLOT(SetVertexShader) == PF_DEV_SET_VS &&
              SLOT(GetVertexShader) == PF_DEV_GET_VS &&
              SLOT(SetVertexShaderConstantF) == PF_DEV_SET_VS_CONST_F &&
              SLOT(GetVertexShaderConstantF) == PF_DEV_GET_VS_CONST_F &&
              SLOT(SetStreamSource) == PF_DEV_SET_STREAM && SLOT(GetStreamSource) == PF_DEV_GET_STREAM &&
              SLOT(SetStreamSourceFreq) == PF_DEV_SET_STREAM_FREQ &&
              SLOT(GetStreamSourceFreq) == PF_DEV_GET_STREAM_FREQ,
          "device vtable slots");
#undef SLOT
    CHECK(offsetof(IDirect3DVertexShader9Vtbl, AddRef) / sizeof(void *) == PF_UNK_ADDREF &&
              offsetof(IDirect3DVertexShader9Vtbl, Release) / sizeof(void *) == PF_UNK_RELEASE &&
              offsetof(IDirect3DVertexShader9Vtbl, GetFunction) / sizeof(void *) == PF_VS_GET_FUNCTION &&
              offsetof(IDirect3DVertexDeclaration9Vtbl, GetDeclaration) / sizeof(void *) ==
                  PF_DECL_GET_DECLARATION &&
              offsetof(IDirect3DVertexBuffer9Vtbl, Lock) / sizeof(void *) == PF_VB_LOCK &&
              offsetof(IDirect3DVertexBuffer9Vtbl, Unlock) / sizeof(void *) == PF_VB_UNLOCK &&
              offsetof(IDirect3DSurface9Vtbl, GetDesc) / sizeof(void *) == PF_SURF_GET_DESC &&
              offsetof(IDirect3DSurface9Vtbl, LockRect) / sizeof(void *) == PF_SURF_LOCK_RECT &&
              offsetof(IDirect3DSurface9Vtbl, UnlockRect) / sizeof(void *) == PF_SURF_UNLOCK_RECT,
          "object vtable slots");
    CHECK(sizeof(D3DCAPS9) == PF_CAPS_SIZE &&
              offsetof(D3DCAPS9, VertexShaderVersion) == PF_CAPS_VS_VERSION &&
              offsetof(D3DCAPS9, MaxVertexShaderConst) == PF_CAPS_MAX_VS_CONST,
          "D3DCAPS9 offsets");
    CHECK(sizeof(pf_vertex_element) == sizeof(D3DVERTEXELEMENT9) &&
              sizeof(pf_viewport) == sizeof(D3DVIEWPORT9) &&
              sizeof(pf_locked_rect) == sizeof(D3DLOCKED_RECT) &&
              sizeof(pf_surface_desc) == sizeof(D3DSURFACE_DESC) &&
              offsetof(D3DSURFACE_DESC, Format) == offsetof(pf_surface_desc, format) &&
              offsetof(D3DSURFACE_DESC, Width) == offsetof(pf_surface_desc, width),
          "struct sizes");
    CHECK(PF_D3DFMT_R32F == D3DFMT_R32F, "R32F");
}

static float bits_float(unsigned u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static unsigned float_bits(float f)
{
    unsigned u;
    memcpy(&u, &f, 4);
    return u;
}

/* The verify's integer depth compare against the floats it stands for. */
static void check_depth_compare(void)
{
    static const float v[] = { 0.0f, 1.0f, 0.5f, 0.25f, 0.4375f, 0.999f, 1e-7f, 3.0f, -0.5f };
    unsigned i;
    for (i = 0; i < sizeof v / sizeof v[0]; i++) {
        double want = (double)v[i] * 16777216.0;
        int got = pf_depth_fixed(float_bits(v[i]));
        CHECK(fabs((double)got - want) <= 1.0, "fixed(%g) = %d, want %.1f", (double)v[i], got,
              want);
    }
    CHECK(pf_depth_diff(float_bits(0.5f), float_bits(0.5f + 1.0f / 65536)) == 256,
          "a 2^-16 step is the tolerance");
    CHECK(pf_depth_diff(float_bits(0.5f), float_bits(0.5f + 1.0f / 32768)) > PF_DEPTH_TOLERANCE,
          "a 2^-15 step is bad");
    CHECK(pf_depth_diff(PF_DEPTH_CLEAR, float_bits(0.3f)) > PF_DEPTH_TOLERANCE,
          "covered on one side only is bad");
    CHECK(pf_depth_diff(0x7FC00000u, float_bits(1.0f)) > PF_DEPTH_TOLERANCE, "NaN is bad");
    CHECK(bits_float(PF_DEPTH_CLEAR) == 1.0f, "clear depth");
}

static IDirect3DPixelShader9 *diffuse_ps(void)
{
    static const char src[] = "float4 main(float4 c : COLOR0, float2 t : TEXCOORD0) : COLOR "
                              "{ return float4(c.rgb, 1) + float4(frac(t * 4), 0, 0) * 0.25; }";
    compile_fn compile = (compile_fn)(void *)GetProcAddress(LoadLibraryA("d3dcompiler_47.dll"),
                                                           "D3DCompile");
    ID3DBlob *code = NULL;
    IDirect3DPixelShader9 *ps = NULL;
    if (!compile || FAILED(compile(src, sizeof src - 1, "ps", NULL, NULL, "main", "ps_2_0", 0, 0,
                                   &code, NULL)))
        return NULL;
    IDirect3DDevice9_CreatePixelShader(g_dev, ID3D10Blob_GetBufferPointer(code), &ps);
    ID3D10Blob_Release(code);
    return ps;
}

static void setup_constants(void)
{
    static float c[256][4];
    const short *r = g_v->roles;
    int i;
    memset(c, 0, sizeof c);
#define SET(reg, a, b, cc, d) (c[reg][0] = a, c[reg][1] = b, c[reg][2] = cc, c[reg][3] = d)
    SET(r[PF_ROLE_VP] + 0, 2.0f / 3, 0, 0, -1);
    SET(r[PF_ROLE_VP] + 1, 0, 2.0f / 3, 0, -1);
    SET(r[PF_ROLE_VP] + 2, 0, 0, 0.2f, 0.4f);
    SET(r[PF_ROLE_VP] + 3, 0, 0, 0, 1);
    SET(r[PF_ROLE_RCPRANGE], 0.2f, 0, 0, 0);
    SET(r[PF_ROLE_AMB], 0.2f, 0.2f, 0.25f, 0);
    SET(r[PF_ROLE_OPAC], 1, 0, 0, 0);
    SET(r[PF_ROLE_FOGRCP], 0.1f, 0, 0, 0);
    for (i = 0; i < 3; i++) {
        SET(r[PF_ROLE_DL] + 2 * i, 0.6f, 0.5f - 0.1f * i, 0.4f, 0);
        SET(r[PF_ROLE_DL] + 2 * i + 1, 0.3f * (i - 1), 0.4f, 0.866f, 0);
    }
    SET(r[PF_ROLE_VIEWI] + 0, 1, 0, 0, 1.5f);
    SET(r[PF_ROLE_VIEWI] + 1, 0, 1, 0, 1.5f);
    SET(r[PF_ROLE_VIEWI] + 2, 0, 0, 1, 8);
    SET(r[PF_ROLE_PL] + 0, 0.5f, 0.4f, 0.2f, 0);
    SET(r[PF_ROLE_PL] + 1, 1, 1, 1, 0);
    SET(r[PF_ROLE_CD], 0.9f, 0.9f, 0.9f, 0);
    SET(r[PF_ROLE_CS], 0.4f, 0.4f, 0.4f, 0);
    SET(r[PF_ROLE_SHIN], 10, 0, 0, 0);
    SET(r[PF_ROLE_SHROUD], 0.3f, 0.3f, 0, 0);
    SET(r[PF_ROLE_HCE], 1, 0, 0, 0);
    SET(r[PF_ROLE_SI] + 3, 0, 0, 0, 1);
    SET(r[PF_ROLE_CLOUD], 0.2f, 0.2f, 0, 0);
#undef SET
    IDirect3DDevice9_SetVertexShaderConstantF(g_dev, 0, &c[0][0], 256);
}

/* The shadow-map technique's constants: light view and projection, opacity,
 * texture transform; the bones come from fake_render. */
static void setup_shadow_constants(void)
{
    static float c[256][4];
    const short *r = g_v->roles;
    memset(c, 0, sizeof c);
#define SET(reg, a, b, cc, d) (c[reg][0] = a, c[reg][1] = b, c[reg][2] = cc, c[reg][3] = d)
    SET(r[PF_ROLE_VIEW] + 0, 1, 0, 0, 0);
    SET(r[PF_ROLE_VIEW] + 1, 0, 1, 0, 0);
    SET(r[PF_ROLE_VIEW] + 2, 0, 0, 1, 0);
    SET(r[PF_ROLE_VIEW] + 3, 0, 0, 0, 1);
    SET(r[PF_ROLE_PROJ] + 0, 2.0f / 3, 0, 0, -1);
    SET(r[PF_ROLE_PROJ] + 1, 0, 2.0f / 3, 0, -1);
    SET(r[PF_ROLE_PROJ] + 2, 0, 0, 0.3f, 0.35f);
    SET(r[PF_ROLE_PROJ] + 3, 0.01f, 0.02f, 0.05f, 1.1f);
    SET(r[PF_ROLE_OPAC], 0.85f, 0, 0, 0);
    SET(r[PF_ROLE_TT0], 1, 1, 0, 0);
#undef SET
    IDirect3DDevice9_SetVertexShaderConstantF(g_dev, 0, &c[0][0], 256);
}

static IDirect3DSurface9 *g_srt, *g_ssys, *g_sds;
static volatile unsigned char g_shadow_flag;

/* The shadow pass: the meshes into an R32F map with a depth buffer, as one
 * group (split 0) or as two groups of the same model and method, the
 * second starting at record split. */
static void shadow_frame(int instanced, unsigned split, unsigned *texels)
{
    fake_mesh *list[MESHES];
    D3DLOCKED_RECT lr;
    unsigned i, y;
    for (i = 0; i < MESHES; i++)
        list[i] = &g_mesh[i];
    IDirect3DDevice9_SetRenderTarget(g_dev, 0, g_srt);
    IDirect3DDevice9_SetDepthStencilSurface(g_dev, g_sds);
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_ZENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_ZWRITEENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    IDirect3DDevice9_BeginScene(g_dev);
    IDirect3DDevice9_Clear(g_dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFFFFFFFF, 1.0f,
                           0);
    IDirect3DDevice9_SetVertexShader(g_dev, g_ea);
    IDirect3DDevice9_SetVertexDeclaration(g_dev, g_decl);
    IDirect3DDevice9_SetStreamSource(g_dev, 0, g_vb, 0, 40);
    IDirect3DDevice9_SetIndices(g_dev, g_ib);
    g_shadow_flag = 1;
    for (i = 0; i < MESHES; i++) {
        if (!instanced)
            fake_render(list[i], &g_method, ((void)g_method.refs++, 1));
        else if (split && i >= split)
            batch_call_from(list, split, i, MESHES, 1, 1);
        else
            batch_call_from(list, 0, i, split ? split : MESHES, 1, 1);
    }
    g_shadow_flag = 0;
    IDirect3DDevice9_EndScene(g_dev);
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_ZENABLE, FALSE);
    IDirect3DDevice9_SetRenderTarget(g_dev, 0, g_rt);
    IDirect3DDevice9_SetDepthStencilSurface(g_dev, NULL);
    IDirect3DDevice9_GetRenderTargetData(g_dev, g_srt, g_ssys);
    IDirect3DSurface9_LockRect(g_ssys, &lr, NULL, D3DLOCK_READONLY);
    for (y = 0; y < RT; y++)
        memcpy(texels + y * RT, (char *)lr.pBits + y * lr.Pitch, RT * 4);
    IDirect3DSurface9_UnlockRect(g_ssys);
}

static int log_has(const char *text)
{
    static char buf[1 << 16];
    WCHAR path[MAX_PATH];
    FILE *f;
    size_t n;
    char name[MAX_PATH];
    GetTempPathW(MAX_PATH, path);
    wcscat(path, L"bfme2perf-inst-test.log");
    WideCharToMultiByte(CP_ACP, 0, path, -1, name, MAX_PATH, NULL, NULL);
    if (!(f = fopen(name, "rb")))
        return 0;
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return strstr(buf, text) != NULL;
}

#define SH(c, field) ((c).shadow[offsetof(pf_inst_counts, field) / sizeof(unsigned)])

/* The game's shadow-map shader (shadowmap 1j) and depth-only pixel shader,
 * an R32F map: the instanced map must equal the game's, the pass must be
 * counted as the shadow pass, ShadowInstancing=0 must leave it to the game,
 * and a verify sample there must be taken in R32F and match. */
static void shadow_tests(void)
{
    static unsigned stock[RT * RT], inst[RT * RT];
    const pf_inst_variant *lit = g_v;
    const unsigned char *ps_code;
    IDirect3DVertexShader9 *lit_ea = g_ea;
    IDirect3DPixelShader9 *ps = NULL, *ps_before = NULL;
    pf_inst_counts c;
    unsigned i, size = 0, covered = 0, bad = 0, exact = 0, maxd = 0;

    for (i = 0; i < PF_INST_VARIANTS; i++)
        if (pf_inst_variants[i].ea_index == 5)
            g_v = &pf_inst_variants[i];
    ps_code = find_shader(2, &size);
    CHECK(g_v != lit && g_v->family == 3 && g_v->joints == lit->joints, "shadowmap 1j variant");
    CHECK(ps_code && size == 208 && *(const unsigned *)ps_code == 0xFFFF0200u,
          "the game's shadow pixel shader (number 2, %u bytes)", size);
    if (g_v == lit || !ps_code)
        return;
    g_ea = NULL;
    IDirect3DDevice9_CreateVertexShader(g_dev, (const DWORD *)find_ea(g_v), &g_ea);
    IDirect3DDevice9_CreatePixelShader(g_dev, (const DWORD *)ps_code, &ps);
    IDirect3DDevice9_CreateRenderTarget(g_dev, RT, RT, D3DFMT_R32F, D3DMULTISAMPLE_NONE, 0, FALSE,
                                        &g_srt, NULL);
    IDirect3DDevice9_CreateOffscreenPlainSurface(g_dev, RT, RT, D3DFMT_R32F, D3DPOOL_SYSTEMMEM,
                                                 &g_ssys, NULL);
    IDirect3DDevice9_CreateDepthStencilSurface(g_dev, RT, RT, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0,
                                               TRUE, &g_sds, NULL);
    CHECK(g_ea && ps && g_srt && g_ssys && g_sds, "shadow pass objects");
    if (!g_ea || !ps || !g_srt || !g_ssys || !g_sds)
        goto out;
    IDirect3DDevice9_GetPixelShader(g_dev, &ps_before);
    IDirect3DDevice9_SetPixelShader(g_dev, ps);
    setup_shadow_constants();
    pf_inst_init(0);
    pf_inst_set_shadow(&g_shadow_flag, 1);
    pf_inst_take(&c);

    shadow_frame(0, 0, stock);
    g_stock_draws = 0;
    shadow_frame(1, 0, inst);
    pf_inst_take(&c);
    CHECK(g_stock_draws == 1 && c.meshes == MESHES - 1 && SH(c, meshes) == MESHES - 1 &&
              SH(c, groups) == 1 && SH(c, leaders) == 1 && SH(c, draws) >= 1 && c.shadow_off == 0,
          "shadow pass: game draws %u, meshes %u (shadow %u), groups %u, leaders %u, off %u, "
          "mismatch %u",
          g_stock_draws, c.meshes, SH(c, meshes), SH(c, groups), SH(c, leaders), c.shadow_off,
          c.mismatch);
    for (i = 0; i < RT * RT; i++) {
        unsigned d;
        if (stock[i] == PF_DEPTH_CLEAR && inst[i] == PF_DEPTH_CLEAR)
            continue;
        covered++;
        d = pf_depth_diff(stock[i], inst[i]);
        exact += stock[i] == inst[i];
        maxd = d > maxd ? d : maxd;
        bad += d > PF_DEPTH_TOLERANCE;
    }
    printf("  shadow map: %u texels drawn, %u bit-identical, largest difference %u/2^24\n",
           covered, exact, maxd);
    CHECK(covered > 5000 && bad * 1000 < covered, "shadow map: %u texels drawn, %u differ", covered,
          bad);

    /* Two groups of the same model and method in a row (the game splits by
     * light, colour or opacity): the second's meshes are counted as split. */
    shadow_frame(1, 4, inst);
    pf_inst_take(&c);
    CHECK(c.shadow_split == MESHES - 4 && SH(c, groups) == 2 && SH(c, meshes) == MESHES - 2,
          "split: %u meshes in a continuing group, %u groups, %u instanced", c.shadow_split,
          SH(c, groups), SH(c, meshes));

    printf("shadow-map pass, ShadowInstancing=0\n");
    pf_inst_set_shadow(&g_shadow_flag, 0);
    g_stock_draws = 0;
    shadow_frame(1, 0, inst);
    pf_inst_take(&c);
    CHECK(g_stock_draws == MESHES && c.meshes == 0 && c.shadow_off == MESHES,
          "off: game draws %u, meshes %u, off %u", g_stock_draws, c.meshes, c.shadow_off);
    /* Without the game's flag, the shadow-map shader still tells the pass. */
    pf_inst_set_shadow(NULL, 0);
    g_stock_draws = 0;
    shadow_frame(1, 0, inst);
    pf_inst_take(&c);
    CHECK(g_stock_draws == MESHES && c.meshes == 0 && c.shadow_off == MESHES,
          "off, no flag: game draws %u, meshes %u, off %u", g_stock_draws, c.meshes,
          c.shadow_off);

    printf("shadow-map pass, verify\n");
    pf_inst_set_shadow(&g_shadow_flag, 1);
    pf_inst_init(2);
    {
        pf_inst_counts t, before;
        unsigned frames;
        pf_inst_totals(&before);
        for (frames = 0; frames < 20; frames++) {
            shadow_frame(1, 0, inst);
            pf_inst_totals(&t);
            if (SH(t, verify_samples) > SH(before, verify_samples))
                break;
            Sleep(50);
        }
        CHECK(SH(t, verify_samples) > SH(before, verify_samples) &&
                  SH(t, verify_bad) == SH(before, verify_bad),
              "shadow verify samples %u -> %u, bad %u -> %u", SH(before, verify_samples),
              SH(t, verify_samples), SH(before, verify_bad), SH(t, verify_bad));
        CHECK(log_has("(game shader 5, shadow-map pass)") && log_has("texels drawn"),
              "the shadow verify sample was taken in R32F (see the log)");
    }
    pf_inst_init(0);
    IDirect3DDevice9_SetPixelShader(g_dev, ps_before);
out:
    if (ps_before)
        IDirect3DPixelShader9_Release(ps_before);
    if (ps)
        IDirect3DPixelShader9_Release(ps);
    if (g_ea)
        IDirect3DVertexShader9_Release(g_ea);
    if (g_srt)
        IDirect3DSurface9_Release(g_srt);
    if (g_ssys)
        IDirect3DSurface9_Release(g_ssys);
    if (g_sds)
        IDirect3DSurface9_Release(g_sds);
    g_ea = lit_ea;
    g_v = lit;
    pf_inst_set_shadow(NULL, 1);
}

int main(int argc, char **argv)
{
    const char *big_path = argc > 1 ? argv[1] : "C:\\Jogos\\AgeoftheRing\\rotwk\\Shaders.big";
    static unsigned stock[RT * RT], inst[RT * RT];
    IDirect3D9 *(WINAPI * create9)(UINT);
    IDirect3D9 *d3d;
    D3DPRESENT_PARAMETERS pp;
    IDirect3DPixelShader9 *ps;
    D3DVERTEXELEMENT9 el[] = {
        { 0, 0, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_NORMAL, 0 },
        { 0, 24, D3DDECLTYPE_D3DCOLOR, 0, D3DDECLUSAGE_BLENDINDICES, 0 },
        { 0, 28, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 0 },
        { 0, 36, D3DDECLTYPE_D3DCOLOR, 0, D3DDECLUSAGE_COLOR, 0 },
        D3DDECL_END(),
    };
    pf_inst_counts c;
    IDirect3DVertexShader9 *vs_after = NULL;
    IDirect3DVertexDeclaration9 *decl_after = NULL;
    UINT freq0 = 0, freq1 = 0;
    unsigned i, diff, covered, refs_before;
    WCHAR log_path[MAX_PATH];
    HWND wnd;
    void *p;

    check_vtable_slots();
    check_depth_compare();
    for (i = 0; i < PF_INST_VARIANTS; i++)
        if (pf_inst_variants[i].ea_index == 476)
            g_v = &pf_inst_variants[i];
    if (!g_v || !load_ea(big_path)) {
        printf("SKIP: the game's Shaders.big isn't readable (%s)\n", big_path);
        goto done;
    }
    create9 = (IDirect3D9 * (WINAPI *)(UINT))(void *)GetProcAddress(LoadLibraryA("d3d9.dll"),
                                                                  "Direct3DCreate9");
    d3d = create9 ? create9(D3D_SDK_VERSION) : NULL;
    wnd = CreateWindowExA(0, "STATIC", "test_inst", WS_POPUP, 0, 0, 64, 64, NULL, NULL, NULL, NULL);
    memset(&pp, 0, sizeof pp);
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = pp.BackBufferHeight = 64;
    pp.hDeviceWindow = wnd;
    if (!d3d || FAILED(IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                                               D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev))) {
        printf("SKIP: no hardware D3D9 device\n");
        DestroyWindow(wnd);
        goto done;
    }
    GetTempPathW(MAX_PATH, log_path);
    wcscat(log_path, L"bfme2perf-inst-test.log");
    DeleteFileW(log_path);
    fu_log_event_path(log_path);

    IDirect3DDevice9_CreateVertexShader(g_dev, (const DWORD *)find_ea(g_v), &g_ea);
    IDirect3DDevice9_CreateVertexDeclaration(g_dev, el, &g_decl);
    IDirect3DDevice9_CreateRenderTarget(g_dev, RT, RT, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0,
                                        FALSE, &g_rt, NULL);
    IDirect3DDevice9_CreateOffscreenPlainSurface(g_dev, RT, RT, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                 &g_sys, NULL);
    IDirect3DDevice9_CreateVertexBuffer(g_dev, 40 * (GRID + 1) * (GRID + 1), 0, 0, D3DPOOL_MANAGED,
                                        &g_vb, NULL);
    IDirect3DDevice9_CreateIndexBuffer(g_dev, 2 * 6 * GRID * GRID, 0, D3DFMT_INDEX16,
                                       D3DPOOL_MANAGED, &g_ib, NULL);
    ps = diffuse_ps();
    CHECK(g_ea && g_decl && g_rt && g_sys && g_vb && g_ib && ps, "device objects");
    if (!g_ea || !ps)
        goto done;
    IDirect3DVertexBuffer9_Lock(g_vb, 0, 0, &p, 0);
    for (i = 0; i < (GRID + 1) * (GRID + 1); i++) {
        float *v = (float *)((char *)p + 40 * i);
        unsigned gx = i % (GRID + 1), gy = i / (GRID + 1);
        v[0] = ((float)gx / GRID - 0.5f) * 0.6f, v[1] = ((float)gy / GRID - 0.5f) * 0.6f, v[2] = 0;
        v[3] = 0, v[4] = 0.3f, v[5] = 0.95f;
        ((DWORD *)v)[6] = (DWORD)((gx + gy) % BONES); /* blue byte = .z = bone */
        v[7] = (float)gx / GRID, v[8] = (float)gy / GRID;
        ((DWORD *)v)[9] = 0xFFFFFFFF;
    }
    IDirect3DVertexBuffer9_Unlock(g_vb);
    IDirect3DIndexBuffer9_Lock(g_ib, 0, 0, &p, 0);
    for (i = 0; i < GRID * GRID; i++) {
        WORD a = (WORD)(i / GRID * (GRID + 1) + i % GRID), *w = (WORD *)p + 6 * i;
        w[0] = a, w[1] = a + 1, w[2] = a + GRID + 1, w[3] = a + 1, w[4] = a + GRID + 2,
        w[5] = a + GRID + 1;
    }
    IDirect3DIndexBuffer9_Unlock(g_ib);
    IDirect3DDevice9_SetRenderTarget(g_dev, 0, g_rt);
    IDirect3DDevice9_SetPixelShader(g_dev, ps);
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_ZENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_CULLMODE, D3DCULL_NONE);
    build_scene();
    setup_constants();

    printf("stock frame\n");
    g_stock_draws = 0;
    draw_frame(0, stock);
    CHECK(g_stock_draws == MESHES, "stock draws %u", g_stock_draws);

    printf("instanced frame\n");
    pf_inst_init(0);
    pf_inst_set_sites(SITE_MESH, SITE_DIP);
    g_stock_draws = 0;
    refs_before = g_method.refs;
    draw_frame(1, inst);
    pf_inst_take(&c);
    CHECK(g_method.refs == refs_before, "method references %u -> %u", refs_before, g_method.refs);
    CHECK(g_stock_draws == 1, "the game drew %u meshes (the leader only)", g_stock_draws);
    CHECK(c.meshes == MESHES - 1 && c.groups == 1 && c.leaders == 1 && c.draws >= 1,
          "counts: meshes %u groups %u leaders %u draws %u (mismatch %u no_shader %u disabled %u "
          "bones %u)",
          c.meshes, c.groups, c.leaders, c.draws, c.mismatch, c.no_shader, c.disabled, c.bones);
    CHECK(c.bonelist == MESHES - 1, "meshes with a %d-bone list for 1 joint: %u, want %d", BONES,
          c.bonelist, MESHES - 1);
    IDirect3DDevice9_GetVertexShader(g_dev, &vs_after);
    IDirect3DDevice9_GetVertexDeclaration(g_dev, &decl_after);
    IDirect3DDevice9_GetStreamSourceFreq(g_dev, 0, &freq0);
    IDirect3DDevice9_GetStreamSourceFreq(g_dev, 1, &freq1);
    CHECK(vs_after == g_ea && decl_after == g_decl && freq0 == 1 && freq1 == 1,
          "state put back: vs %d decl %d freq %u %u", vs_after == g_ea, decl_after == g_decl,
          freq0, freq1);
    if (vs_after)
        IDirect3DVertexShader9_Release(vs_after);
    if (decl_after)
        IDirect3DVertexDeclaration9_Release(decl_after);
    for (i = diff = covered = 0; i < RT * RT; i++) {
        unsigned a = stock[i], b = inst[i], k, worst = 0;
        if (!a && !b)
            continue;
        covered++;
        for (k = 0; k < 32; k += 8) {
            unsigned x = a >> k & 0xFF, y = b >> k & 0xFF;
            if ((x > y ? x - y : y - x) > worst)
                worst = x > y ? x - y : y - x;
        }
        diff += worst > 2;
    }
    CHECK(covered > 5000 && diff * 1000 < covered, "images: %u pixels drawn, %u differ", covered,
          diff);

    printf("reasons\n");
    {
        fake_mesh *list[MESHES];
        for (i = 0; i < MESHES; i++)
            list[i] = &g_mesh[i];
        IDirect3DDevice9_BeginScene(g_dev);
        batch_call(list, 0, 2, 1, 1);
        batch_call(list, 1, 2, 1, 1);
        for (i = 0; i < 3; i++)
            batch_call(list, i, 3, 2, 1);
        for (i = 0; i < 3; i++)
            batch_call(list, i, 3, 1, 4);
        IDirect3DDevice9_EndScene(g_dev);
        pf_inst_take(&c);
        CHECK(c.small == 2 && c.multipass == 3 && c.game_inst == 3 && c.meshes == 0,
              "small %u multipass %u game_inst %u meshes %u", c.small, c.multipass, c.game_inst,
              c.meshes);
        /* A mesh of another model in the middle splits the group there. */
        {
            static unsigned char other_model[0x100];
            fake_mesh odd = g_mesh[4];
            memcpy(other_model, g_model, sizeof other_model);
            *(unsigned char **)(odd.b + PF_MESH_MODEL) = other_model;
            list[4] = &odd;
            g_stock_draws = 0;
            IDirect3DDevice9_BeginScene(g_dev);
            for (i = 0; i < MESHES; i++)
                batch_call(list, i, MESHES, 1, 1);
            IDirect3DDevice9_EndScene(g_dev);
            pf_inst_take(&c);
            /* 0 leads 1-3; 4 (another model) leads nothing; 5 leads 6-8. */
            CHECK(c.meshes == 6 && c.groups == 3 && c.draws == 2 && c.mismatch == 0 &&
                      g_stock_draws == 3,
                  "split: meshes %u groups %u draws %u mismatch %u game draws %u", c.meshes,
                  c.groups, c.draws, c.mismatch, g_stock_draws);

            /* Two meshes left at the change: too few to lead, the game draws them. */
            list[4] = &g_mesh[4];
            odd = g_mesh[7];
            *(unsigned char **)(odd.b + PF_MESH_MODEL) = other_model;
            list[7] = &odd;
            g_stock_draws = 0;
            IDirect3DDevice9_BeginScene(g_dev);
            for (i = 0; i < MESHES; i++)
                batch_call(list, i, MESHES, 1, 1);
            IDirect3DDevice9_EndScene(g_dev);
            pf_inst_take(&c);
            CHECK(c.meshes == 6 && c.groups == 1 && c.mismatch == 2 && g_stock_draws == 3,
                  "tail: meshes %u groups %u mismatch %u game draws %u", c.meshes, c.groups,
                  c.mismatch, g_stock_draws);
            list[7] = &g_mesh[7];
        }
        /* Joints per vertex the game doesn't use, or unlike the bound
         * shader's: the game draws every mesh. */
        {
            static const int joints[3] = { 0, 3, 2 };
            unsigned k;
            for (k = 0; k < 3; k++) {
                *(int *)(g_geom + PF_GEOM_JOINTS) = joints[k];
                g_stock_draws = 0;
                IDirect3DDevice9_BeginScene(g_dev);
                for (i = 0; i < MESHES; i++)
                    batch_call(list, i, MESHES, 1, 1);
                IDirect3DDevice9_EndScene(g_dev);
                pf_inst_take(&c);
                CHECK(c.meshes == 0 && c.mismatch == MESHES && g_stock_draws == MESHES,
                      "%d joints: meshes %u mismatch %u game draws %u", joints[k], c.meshes,
                      c.mismatch, g_stock_draws);
            }
            *(int *)(g_geom + PF_GEOM_JOINTS) = g_v->joints;
        }
        /* A list as long as the joints (the kind instanced before 0.8.8). */
        {
            short *end_before = *(short **)(g_geom + PF_GEOM_BONES_END);
            *(short **)(g_geom + PF_GEOM_BONES_END) = g_bonemap + 1;
            for (i = 0; i < 4; i++)
                g_bonemap[i] = (short)(PIVOTS - 1); /* every vertex's bone reads entry 0 */
            IDirect3DDevice9_BeginScene(g_dev);
            for (i = 0; i < MESHES; i++)
                batch_call(list, i, MESHES, 1, 1);
            IDirect3DDevice9_EndScene(g_dev);
            pf_inst_take(&c);
            CHECK(c.meshes == MESHES - 1 && c.bonelist == 0,
                  "one bone, one joint: meshes %u, of them counted as a longer list %u", c.meshes,
                  c.bonelist);
            *(short **)(g_geom + PF_GEOM_BONES_END) = end_before;
            for (i = 0; i < BONES; i++)
                g_bonemap[i] = (short)(PIVOTS - 1 - i);
        }
    }

    printf("verify\n");
    pf_inst_init(2);
    {
        pf_inst_counts t;
        unsigned frames;
        for (frames = 0; frames < 20; frames++) {
            draw_frame(1, inst);
            pf_inst_totals(&t);
            if (t.verify_samples)
                break;
            Sleep(50);
        }
        CHECK(t.verify_samples >= 1 && t.verify_bad == 0, "verify samples %u bad %u",
              t.verify_samples, t.verify_bad);
        CHECK(SH(t, verify_samples) == 0 && SH(t, meshes) == 0,
              "the main pass counted as the shadow pass: samples %u, meshes %u",
              SH(t, verify_samples), SH(t, meshes));
    }

    printf("shadow-map pass\n");
    shadow_tests();

done:
    if (g_failures) {
        printf("FAILED: %d check(s) (log in %%TEMP%%\\bfme2perf-inst-test.log)\n", g_failures);
        return 1;
    }
    printf("OK: instancing tests passed\n");
    return 0;
}
