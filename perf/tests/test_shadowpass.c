/* The shadow-pass fixes against fake game objects, the effect-file guard,
 * and ShadowPassVerify on a hidden D3D9 device. */
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <wchar.h>

#include "d3d9vt.h"
#include "fxguard.h"
#include "shadowfx_table.h"
#include "shadowpass.h"
#include "stubs.h"

/* stubs.c (for fps.c's skip thunks) calls these; nothing is hooked here. */
void *__attribute__((used)) pf_enter(unsigned k, void **slot)
{
    (void)k;
    (void)slot;
    return 0;
}
void *__attribute__((used)) pf_exit(unsigned k, unsigned char *esp)
{
    (void)k;
    (void)esp;
    return 0;
}

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

enum { S_OCCLUDEE = 1, S_OCCLUDER, S_OTHER, S_PARTICLES };

static volatile unsigned char g_flag;
static unsigned char g_gd_obj[0x100];
static const unsigned char *g_gd = g_gd_obj;
static void *g_device;
static unsigned char g_smudge_obj[0x40];
static unsigned char *g_smudge = g_smudge_obj;
static volatile unsigned g_smudge_count;
static unsigned char g_render_fn[16];
static void *g_mgr_vt[32];
static unsigned char g_mgr_obj[0x100];
static unsigned char *g_mgr = g_mgr_obj;
static char g_next[4], g_skip[4];

static void init(int batch, int stencil, int particles, int verify)
{
    pf_sp_data d;
    d.gd = &g_gd;
    d.device = &g_device;
    d.particle_render = g_render_fn;
    d.smudge = &g_smudge;
    d.smudge_count = &g_smudge_count;
    pf_sp_init(&g_flag, &d, batch, stencil, particles, verify);
    pf_sp_set_sites(S_OCCLUDEE, S_OCCLUDER, S_OTHER, S_PARTICLES);
    pf_sp_set_particle_manager(&g_mgr);
}

/* ---- ShadowBatchIgnoreLight ---- */

typedef struct {
    unsigned char m[0x330];
} fake_mesh;

static int g_same_calls;

/* The game's test: light environments equal by value, fog colours, opacity
 * (at +0x40 here). */
static unsigned char __attribute__((thiscall)) fake_same(void *self, void *other)
{
    unsigned char *a = self, *b = other;
    const unsigned *ea = *(const unsigned **)(a + PF_MESH_LIGHT_ENV);
    const unsigned *eb = *(const unsigned **)(b + PF_MESH_LIGHT_ENV);
    g_same_calls++;
    if ((ea == 0) != (eb == 0) || (ea && *ea != *eb))
        return 0;
    if (memcmp(a + PF_MESH_FOG_COLOR, b + PF_MESH_FOG_COLOR, 12))
        return 0;
    return *(unsigned *)(a + 0x40) == *(unsigned *)(b + 0x40);
}

static void mesh(fake_mesh *f, unsigned *env, unsigned fog, unsigned opacity)
{
    unsigned i;
    memset(f, 0, sizeof *f);
    *(unsigned **)(f->m + PF_MESH_LIGHT_ENV) = env;
    for (i = 0; i < 3; i++)
        ((unsigned *)(f->m + PF_MESH_FOG_COLOR))[i] = fog + i;
    *(unsigned *)(f->m + 0x40) = opacity;
}

static void test_batch(void)
{
    unsigned env_a = 1, env_b = 2;
    fake_mesh a, b, c, keep;
    pf_sp_counts n;

    printf("ShadowBatchIgnoreLight\n");
    init(1, 0, 0, 0);
    pf_sp_set_same_batch((void *)fake_same);
    mesh(&a, &env_a, 100, 7);
    mesh(&b, &env_b, 200, 7);
    g_flag = 1;
    pf_sp_set_batch_proven(1);
    CHECK(pf_sp_same_batch(a.m, b.m) == 0 && !pf_sp_batch_on(),
          "without the light environment's sites: the game's test");
    pf_sp_set_batch_light(1);
    pf_sp_set_batch_proven(0);
    CHECK(pf_sp_same_batch(a.m, b.m) == 0 && !pf_sp_batch_on(),
          "effect files not proven yet: the game's test");
    pf_sp_set_batch_proven(1);
    CHECK(pf_sp_batch_on(), "proven: on");
    mesh(&a, &env_a, 100, 7);
    mesh(&b, &env_b, 200, 7);
    mesh(&c, &env_b, 200, 8);
    keep = b;
    g_flag = 0;
    CHECK(pf_sp_same_batch(a.m, b.m) == 0, "outside the shadow pass the game's test decides");
    g_flag = 1;
    CHECK(pf_sp_same_batch(a.m, b.m) == 1, "light and fog colour are ignored in the shadow pass");
    CHECK(!memcmp(&b, &keep, sizeof b), "the other mesh's light environment and fog are put back");
    CHECK(pf_sp_same_batch(a.m, c.m) == 0, "opacity still splits");
    CHECK(pf_sp_same_batch(b.m, b.m) == 1, "equal meshes stay together");
    pf_sp_take(&n);
    CHECK(n.batch_tests == 3 && n.batch_split_game == 2 && n.batch_split_now == 1,
          "tests %u, game ends %u, ends now %u", n.batch_tests, n.batch_split_game,
          n.batch_split_now);
    pf_sp_set_same_batch(0);
    CHECK(!pf_sp_batch_on(), "no original: off");
    init(0, 0, 0, 0);
    pf_sp_set_same_batch((void *)fake_same);
    CHECK(pf_sp_same_batch(a.m, b.m) == 0, "ShadowBatchIgnoreLight=0: the game's test");
    g_flag = 0;
}

/* ---- ShadowBatchIgnoreLight's light environment ---- */

enum { MAX_RUN = 8 };
static fake_mesh g_run[MAX_RUN];
static unsigned char g_records[MAX_RUN * PF_SP_RECORD];
static unsigned char g_frame[16];

/* A run of n meshes with these light environments (same fog colour and
 * opacity), scanned as PerformRendering does: the group's first against
 * each next record while the test holds. Returns the records in the group. */
static unsigned scan(unsigned *const *envs, unsigned n)
{
    unsigned i;
    memset(g_records, 0, sizeof g_records);
    for (i = 0; i < n; i++) {
        mesh(&g_run[i], envs[i], 100, 7);
        *(unsigned char **)(g_records + i * PF_SP_RECORD) = g_run[i].m;
        *(unsigned *)(g_records + i * PF_SP_RECORD + 4) = 1;
    }
    for (i = 1; i < n && pf_sp_same_batch(g_run[0].m, g_run[i].m); i++)
        ;
    return i;
}

/* The group setup's call (pushal: ECX slot[-4], EBX slot[-6], EBP slot[-8]),
 * then its Set_Light_Environment(first mesh's environment): what the latter
 * gets. */
static void *setup(unsigned in_group)
{
    void *stack[16], **slot = stack + 12, *arg[2];
    slot[-4] = g_run[0].m;
    slot[-6] = g_records;
    slot[-8] = g_frame;
    *(unsigned char **)(g_frame + 8) = g_records + in_group * PF_SP_RECORD;
    pf_sp_group(slot);
    arg[0] = 0;
    arg[1] = *(void **)(g_run[0].m + PF_MESH_LIGHT_ENV);
    pf_sp_light_env(arg);
    return arg[1];
}

static void test_batch_light(void)
{
    unsigned e1 = 1, e2 = 2, e3 = 3, e1b = 1;
    unsigned *run_a[] = { &e1, &e1, &e2, &e2, &e3 };
    unsigned *run_b[] = { &e1, &e2, &e1 };
    unsigned *run_c[] = { &e1, &e1b, &e1 };
    unsigned *run_d[] = { &e1, 0 };
    unsigned n, calls;
    void *got;
    pf_sp_counts c;

    printf("ShadowBatchIgnoreLight: the light environment a merged group stores\n");
    init(1, 0, 0, 0);
    pf_sp_set_same_batch((void *)fake_same);
    pf_sp_set_batch_light(1);
    pf_sp_set_batch_proven(1);
    pf_sp_take(&c);
    g_flag = 1;
    /* The game makes three groups: e1 e1 | e2 e2 | e3; its last setup stores e3. */
    n = scan(run_a, 5);
    CHECK(n == 5, "one group of %u", n);
    calls = g_same_calls;
    got = setup(n);
    CHECK(got == &e3, "the setup stores the last game group's environment (%p, e3 %p)", got,
          (void *)&e3);
    CHECK(g_same_calls - calls == 4, "the game's test, once per record after the first: %d",
          g_same_calls - calls);
    /* e1 | e2 | e1: the game's last group stores the first mesh's own. */
    n = scan(run_b, 3);
    got = setup(n);
    CHECK(n == 3 && got == &e1, "e1 e2 e1: e1 (%p)", got);
    /* Equal by value: the game makes one group and stores the first's. */
    n = scan(run_c, 3);
    got = setup(n);
    CHECK(n == 3 && got == &e1, "equal environments: the first's (%p)", got);
    /* None and one don't merge. */
    n = scan(run_d, 2);
    CHECK(n == 1, "a mesh without an environment ends the group (%u)", n);
    pf_sp_take(&c);
    CHECK(c.batch_light_fixed == 1 && c.batch_light_missed == 0, "fixed %u, missed %u",
          c.batch_light_fixed, c.batch_light_missed);
    /* A setup whose call isn't the expected one: left alone, counted. */
    n = scan(run_a, 5);
    {
        void *stack[16], **slot = stack + 12, *arg[2];
        slot[-4] = g_run[0].m;
        slot[-6] = g_records;
        slot[-8] = g_frame;
        *(unsigned char **)(g_frame + 8) = g_records + n * PF_SP_RECORD;
        pf_sp_group(slot);
        arg[1] = &e2;
        pf_sp_light_env(arg);
        CHECK(arg[1] == &e2, "another environment passes");
        arg[1] = &e1;
        pf_sp_light_env(arg);
        CHECK(arg[1] == &e1, "the fix is spent");
    }
    pf_sp_take(&c);
    CHECK(c.batch_light_fixed == 0 && c.batch_light_missed == 1, "fixed %u, missed %u",
          c.batch_light_fixed, c.batch_light_missed);
    /* A group the game makes itself, and the other passes, are left alone. */
    {
        unsigned *same[] = { &e2, &e2 };
        n = scan(same, 2);
        CHECK(setup(n) == &e2, "a game group");
    }
    n = scan(run_a, 5);
    g_flag = 0;
    CHECK(setup(n) == &e1, "outside the shadow pass");
    g_flag = 1;
    /* Without the sites: the game's groups, the first's environment. */
    pf_sp_set_batch_light(0);
    n = scan(run_a, 5);
    CHECK(n == 2 && setup(n) == &e1, "sites missing: the game's groups (%u)", n);
    g_flag = 0;
}

/* ---- NoStencilFlushes ---- */

static void *stencil(unsigned site, int reg, unsigned value, unsigned *out)
{
    void *stack[16], **slot = stack + 12, *to;
    unsigned i;
    for (i = 0; i < 16; i++)
        stack[i] = (void *)(UINT_PTR)(0x1000 + i);
    slot[reg] = (void *)(UINT_PTR)value;
    to = pf_sp_enter(site, slot, g_next, g_skip);
    *out = (unsigned)(UINT_PTR)slot[reg];
    return to;
}

static void test_stencil(void)
{
    unsigned v;
    pf_sp_counts n;

    printf("NoStencilFlushes\n");
    init(0, 1, 0, 0);
    g_flag = 1;
    CHECK(stencil(S_OCCLUDEE, -4, 0x12340183u, &v) == g_next && v == 0,
          "occludee bits dropped in the shadow pass (ECX %08X)", v);
    CHECK(stencil(S_OCCLUDER, -3, 0xABCD0300u, &v) == g_next && v == 0,
          "occluder bits dropped (EAX %08X)", v);
    CHECK(stencil(S_OTHER, -6, 0x00000080u, &v) == g_next && v == 0,
          "other bits dropped (EBX %08X)", v);
    CHECK(stencil(S_OCCLUDEE, -4, 0x12340002u, &v) == g_next && v == 0x12340002u,
          "no NON_SELF bits: untouched (%08X)", v);
    CHECK(stencil(S_OCCLUDER, -3, 0xABCD0002u, &v) == g_next && v == 0xABCD0002u,
          "SHADOW_VOLUME alone: untouched (%08X)", v);
    g_flag = 0;
    CHECK(stencil(S_OCCLUDEE, -4, 0x00000100u, &v) == g_next && v == 0x100,
          "NoStencilFlushes=1: other passes untouched (%08X)", v);
    pf_sp_take(&n);
    CHECK(n.stencil_shadow == 3 && n.stencil_other == 0, "counts %u %u", n.stencil_shadow,
          n.stencil_other);

    init(0, 2, 0, 0);
    g_gd_obj[PF_GD_SHADOW_MAP] = 1;
    g_gd_obj[PF_GD_SHADOW_VOLUMES] = 0;
    CHECK(stencil(S_OTHER, -6, 0x00000200u, &v) == g_next && v == 0,
          "NoStencilFlushes=2 with shadow mapping: dropped in the views (%08X)", v);
    g_gd_obj[PF_GD_SHADOW_VOLUMES] = 1;
    CHECK(stencil(S_OTHER, -6, 0x00000200u, &v) == g_next && v == 0x200,
          "stencil shadow volumes on: untouched (%08X)", v);
    g_gd_obj[PF_GD_SHADOW_VOLUMES] = 0;
    g_gd_obj[PF_GD_SHADOW_MAP] = 0;
    CHECK(stencil(S_OTHER, -6, 0x00000200u, &v) == g_next && v == 0x200,
          "shadow mapping off: untouched (%08X)", v);
    g_flag = 1;
    g_gd_obj[PF_GD_SHADOW_VOLUMES] = 1;
    CHECK(stencil(S_OCCLUDEE, -4, 0x80u, &v) == g_next && v == 0,
          "the shadow pass never needs them (%08X)", v);
    g_gd_obj[PF_GD_SHADOW_VOLUMES] = 0;
    pf_sp_take(&n);
    CHECK(n.stencil_shadow == 1 && n.stencil_other == 1, "counts %u %u", n.stencil_shadow,
          n.stencil_other);
    init(0, 0, 0, 0);
    CHECK(stencil(S_OCCLUDEE, -4, 0x80u, &v) == g_next && v == 0x80, "off: untouched");
    g_flag = 0;
}

/* ---- ShadowSkipParticles ---- */

static void particle_world(int pending, int smudges_on)
{
    unsigned i;
    for (i = 0; i < 32; i++)
        g_mgr_vt[i] = (void *)(UINT_PTR)(0x9000 + i);
    g_mgr_vt[PF_PMGR_RENDER_SLOT / 4] = g_render_fn;
    memset(g_mgr_obj, 0, sizeof g_mgr_obj);
    *(void ***)g_mgr_obj = g_mgr_vt;
    g_mgr_obj[PF_PMGR_RENDER_PENDING] = (unsigned char)pending;
    memset(g_smudge_obj, 0, sizeof g_smudge_obj);
    *(unsigned *)(g_smudge_obj + PF_SMUDGE_COUNT) = 9;
    g_smudge_count = 5;
    g_gd_obj[PF_GD_SMUDGES] = (unsigned char)smudges_on;
}

static void test_particles(void)
{
    void *stack[16], **slot = stack + 12;
    pf_sp_counts n;

    printf("ShadowSkipParticles\n");
    init(0, 0, 1, 0);
    particle_world(1, 0);
    g_flag = 0;
    CHECK(pf_sp_enter(S_PARTICLES, slot, g_next, g_skip) == g_next, "views: the walk runs");
    g_flag = 1;
    CHECK(pf_sp_enter(S_PARTICLES, slot, g_next, g_skip) == g_skip, "shadow pass: skipped");
    CHECK(g_mgr_obj[PF_PMGR_RENDER_PENDING] == 0 && g_smudge_count == 0 &&
              *(unsigned *)(g_smudge_obj + PF_SMUDGE_COUNT) == 0,
          "the walk's bookkeeping reproduced: pending %u, smudges %u/%u",
          g_mgr_obj[PF_PMGR_RENDER_PENDING], g_smudge_count,
          *(unsigned *)(g_smudge_obj + PF_SMUDGE_COUNT));
    particle_world(0, 1);
    CHECK(pf_sp_enter(S_PARTICLES, slot, g_next, g_skip) == g_skip && g_smudge_count == 5,
          "nothing pending: the render would return at once");
    particle_world(1, 1);
    CHECK(pf_sp_enter(S_PARTICLES, slot, g_next, g_skip) == g_next &&
              g_mgr_obj[PF_PMGR_RENDER_PENDING] == 1 && g_smudge_count == 5,
          "smudges possible: the walk runs (ShadowSkipParticles=1)");
    *(unsigned *)(g_smudge_obj + PF_SMUDGE_STATE) = 1;
    CHECK(pf_sp_enter(S_PARTICLES, slot, g_next, g_skip) == g_skip,
          "smudges unsupported by the hardware: skipped");
    particle_world(1, 1);
    g_smudge = 0;
    CHECK(pf_sp_enter(S_PARTICLES, slot, g_next, g_skip) == g_skip && g_smudge_count == 0,
          "no smudge manager: skipped");
    g_smudge = g_smudge_obj;
    particle_world(1, 1);
    g_mgr_vt[PF_PMGR_RENDER_SLOT / 4] = (void *)0x1234;
    CHECK(pf_sp_enter(S_PARTICLES, slot, g_next, g_skip) == g_next &&
              g_mgr_obj[PF_PMGR_RENDER_PENDING] == 1,
          "another render in the vtable: the walk runs");
    g_mgr = 0;
    CHECK(pf_sp_enter(S_PARTICLES, slot, g_next, g_skip) == g_next, "no manager: the call runs");
    g_mgr = g_mgr_obj;
    pf_sp_take(&n);
    CHECK(n.particles_skipped == 4 && n.particles_kept == 1, "skipped %u kept %u",
          n.particles_skipped, n.particles_kept);

    init(0, 0, 2, 0);
    particle_world(1, 1);
    CHECK(pf_sp_enter(S_PARTICLES, slot, g_next, g_skip) == g_skip && g_smudge_count == 0,
          "ShadowSkipParticles=2: always skipped");
    init(0, 0, 0, 0);
    particle_world(1, 0);
    CHECK(pf_sp_enter(S_PARTICLES, slot, g_next, g_skip) == g_next, "0: the walk runs");
    g_flag = 0;
}

/* ---- the effect-file guard ---- */

static void put_be32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v >> 24), p[1] = (unsigned char)(v >> 16), p[2] = (unsigned char)(v >> 8),
    p[3] = (unsigned char)v;
}

/* A .big with one file of the given name and content. */
static void write_big(const WCHAR *path, const char *name, const char *data)
{
    unsigned char buf[512];
    unsigned nlen = (unsigned)strlen(name) + 1, dlen = (unsigned)strlen(data);
    unsigned hdr = 16 + 8 + nlen;
    HANDLE h;
    DWORD put;
    memcpy(buf, "BIGF", 4);
    put_be32(buf + 4, hdr + dlen);
    put_be32(buf + 8, 1);
    put_be32(buf + 12, hdr);
    put_be32(buf + 16, hdr);
    put_be32(buf + 20, dlen);
    memcpy(buf + 24, name, nlen);
    memcpy(buf + hdr, data, dlen);
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(h, buf, hdr + dlen, &put, NULL);
    CloseHandle(h);
}

static void test_guard(void)
{
    WCHAR tmp[MAX_PATH], dir[MAX_PATH], big[MAX_PATH], cmd[2 * MAX_PATH], loose[MAX_PATH],
        mod[MAX_PATH];
    const WCHAR *game = L"C:\\Jogos\\AgeoftheRing\\rotwk";
    pf_fxguard_result r;
    int real = GetFileAttributesW(L"C:\\Jogos\\AgeoftheRing\\rotwk\\Shaders.big") !=
               INVALID_FILE_ATTRIBUTES;

    printf("effect-file guard\n");
    CHECK(pf_fxguard_mod_dir(L"game.dat -mod \"C:\\a b\\realms\" -x", mod, MAX_PATH) &&
              !wcscmp(mod, L"C:\\a b\\realms"),
          "quoted -mod: %ls", mod);
    CHECK(pf_fxguard_mod_dir(L"game.dat -MOD D:\\mods\\x -y", mod, MAX_PATH) &&
              !wcscmp(mod, L"D:\\mods\\x"),
          "plain -mod: %ls", mod);
    CHECK(!pf_fxguard_mod_dir(L"game.dat -model x", mod, MAX_PATH), "-model isn't -mod");

    GetTempPathW(MAX_PATH, tmp);
    swprintf(dir, MAX_PATH, L"%lsbfme2perf-fxguard-test", tmp);
    CreateDirectoryW(dir, NULL);
    swprintf(big, MAX_PATH, L"%ls\\evil.big", dir);
    write_big(big, "shaders\\compiled\\defaultw3d.fxo", "not the game's effect");
    CHECK(!pf_fxguard_check(dir, L"game.dat", &r) && r.archives == 1 && r.effects == 1 &&
              r.proven == 0 && strstr(r.why, "defaultw3d.fxo"),
          "an effect with other content: %u %u %u '%s'", r.archives, r.effects, r.proven, r.why);
    write_big(big, "art\\textures\\x.dds", "nothing");
    CHECK(!pf_fxguard_check(dir, L"game.dat", &r) && strstr(r.why, "not found"),
          "no effects at all: '%s'", r.why);
    if (!real) {
        printf("  (no local Shaders.big: the real-folder cases are skipped)\n");
    } else {
        CHECK(pf_fxguard_check(game, L"game.dat", &r) && r.proven == r.effects &&
                  r.effects >= PF_SHADOWFX_COUNT,
              "the game's folder is proven: %u archives, %u effects, %u proven '%s'", r.archives,
              r.effects, r.proven, r.why);
        swprintf(cmd, 2 * MAX_PATH, L"game.dat -mod \"%ls\"", dir);
        write_big(big, "shaders\\compiled\\mymod.fxo", "a mod's own effect");
        CHECK(!pf_fxguard_check(game, cmd, &r) && strstr(r.why, "mymod.fxo"),
              "a mod's effect switches it off: '%s'", r.why);
        DeleteFileW(big);
        swprintf(loose, MAX_PATH, L"%ls\\shaders", dir);
        CreateDirectoryW(loose, NULL);
        swprintf(loose, MAX_PATH, L"%ls\\shaders\\compiled", dir);
        CreateDirectoryW(loose, NULL);
        swprintf(loose, MAX_PATH, L"%ls\\shaders\\compiled\\terrain.fxo", dir);
        {
            HANDLE h = CreateFileW(loose, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            DWORD put;
            WriteFile(h, "x", 1, &put, NULL);
            CloseHandle(h);
        }
        CHECK(!pf_fxguard_check(game, cmd, &r) && strstr(r.why, "terrain.fxo"),
              "a loose effect switches it off: '%s'", r.why);
        DeleteFileW(loose);
        swprintf(loose, MAX_PATH, L"%ls\\shaders\\compiled", dir);
        RemoveDirectoryW(loose);
        swprintf(loose, MAX_PATH, L"%ls\\shaders", dir);
        RemoveDirectoryW(loose);
        CHECK(pf_fxguard_check(game, cmd, &r), "an empty mod folder: proven ('%s')", r.why);
    }
    DeleteFileW(big);
    RemoveDirectoryW(dir);
}

/* ---- ShadowPassVerify on a device ---- */

static IDirect3DDevice9 *g_dev;
static IDirect3DSurface9 *g_map, *g_depth;
static int g_inexact; /* the "stencil fix" moves a quad when on */
/* A stand-in for LightSkipShadow (lights.c): on, held off by the verify, and
 * wrong (moves a quad while it acts) or not. */
static int g_light_on, g_light_wrong, g_light_held;

static int fake_light_active(void)
{
    return g_light_on;
}

static void fake_light_off(void)
{
    g_light_on = 0;
}

typedef struct {
    float x, y, z, w;
    DWORD c;
} quad_vtx;

static void draw_quad(float x0, float x1, float z)
{
    quad_vtx v[4] = { { x0, -0.5f, z, 1, 0 }, { x1, -0.5f, z, 1, 0 }, { x0, 0.5f, z, 1, 0 },
                      { x1, 0.5f, z, 1, 0 } };
    IDirect3DDevice9_DrawPrimitiveUP(g_dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof *v);
}

/* UpdateShadowMap as the verify sees it: raises the flag, draws a map into
 * an R32F target (the particle site runs inside, as in RTS3DScene::Flush),
 * and lowers the flag. One quad goes through the stencil site: where a fix
 * is wrong (g_inexact), it lands elsewhere when the fix acts. */
static void __attribute__((thiscall)) fake_update(void *self, void *scene)
{
    IDirect3DSurface9 *bb = NULL, *bd = NULL;
    void *stack[16], **slot = stack + 12;
    unsigned v;
    (void)self;
    (void)scene;
    g_flag = 1;
    IDirect3DDevice9_GetRenderTarget(g_dev, 0, &bb);
    IDirect3DDevice9_GetDepthStencilSurface(g_dev, &bd);
    IDirect3DDevice9_SetRenderTarget(g_dev, 0, g_map);
    IDirect3DDevice9_SetDepthStencilSurface(g_dev, g_depth);
    IDirect3DDevice9_Clear(g_dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFFFFFFFF, 1.0f, 0);
    g_mgr_obj[PF_PMGR_RENDER_PENDING] = 1; /* Customized_Render asks for the walk */
    if (pf_sp_enter(S_PARTICLES, slot, g_next, g_skip) == g_next) {
        /* The game's walk, which adds no smudge here. */
        g_mgr_obj[PF_PMGR_RENDER_PENDING] = 0;
        g_smudge_count = 0;
        *(unsigned *)(g_smudge_obj + PF_SMUDGE_COUNT) = 0;
    }
    IDirect3DDevice9_BeginScene(g_dev);
    draw_quad(-0.8f, -0.2f, 0.25f);
    v = 0x80;
    slot[-4] = (void *)(UINT_PTR)v;
    pf_sp_enter(S_OCCLUDEE, slot, g_next, g_skip);
    v = (unsigned)(UINT_PTR)slot[-4];
    g_light_held += pf_sp_holding(PF_SP_LIGHT);
    if ((g_inexact && v == 0) || (g_light_wrong && g_light_on && !pf_sp_holding(PF_SP_LIGHT)))
        draw_quad(0.3f, 0.9f, 0.5f);
    else
        draw_quad(0.2f, 0.8f, 0.5f);
    IDirect3DDevice9_EndScene(g_dev);
    IDirect3DDevice9_SetRenderTarget(g_dev, 0, bb);
    IDirect3DDevice9_SetDepthStencilSurface(g_dev, bd);
    if (bb)
        IDirect3DSurface9_Release(bb);
    if (bd)
        IDirect3DSurface9_Release(bd);
    g_flag = 0;
}

static void test_verify(void)
{
    IDirect3D9 *(WINAPI * create9)(UINT);
    IDirect3D9 *d3d;
    D3DPRESENT_PARAMETERS pp;
    HWND wnd;
    void *stack[16], **slot = stack + 12;
    pf_sp_counts n;
    int i;

    printf("ShadowPassVerify\n");
    create9 = (IDirect3D9 * (WINAPI *)(UINT))(void *)GetProcAddress(LoadLibraryA("d3d9.dll"),
                                                                  "Direct3DCreate9");
    d3d = create9 ? create9(D3D_SDK_VERSION) : NULL;
    wnd = CreateWindowExA(0, "STATIC", "test_shadowpass", WS_POPUP, 0, 0, 64, 64, NULL, NULL, NULL,
                          NULL);
    memset(&pp, 0, sizeof pp);
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = pp.BackBufferHeight = 64;
    pp.hDeviceWindow = wnd;
    if (!d3d || FAILED(IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                                               D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev))) {
        printf("  (no hardware D3D9 device: skipped)\n");
        DestroyWindow(wnd);
        return;
    }
    IDirect3DDevice9_CreateRenderTarget(g_dev, 128, 128, D3DFMT_R32F, D3DMULTISAMPLE_NONE, 0,
                                        FALSE, &g_map, NULL);
    IDirect3DDevice9_CreateDepthStencilSurface(g_dev, 128, 128, D3DFMT_D24S8, D3DMULTISAMPLE_NONE,
                                               0, TRUE, &g_depth, NULL);
    IDirect3DDevice9_SetFVF(g_dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    IDirect3DDevice9_SetFVF(g_dev, D3DFVF_XYZW | D3DFVF_DIFFUSE);
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(g_dev, D3DRS_CULLMODE, D3DCULL_NONE);
    g_device = g_dev;
    stack[12 - 4] = (void *)0x1111; /* ECX: the shadow manager */
    stack[12 + 1] = (void *)0x2222; /* the scene argument */

    init(0, 1, 0, 2);
    g_inexact = 0;
    for (i = 0; i < 3; i++) {
        CHECK(pf_sp_verify_due(), "due at every redraw in test mode");
        pf_sp_verify(slot, (void *)fake_update);
    }
    pf_sp_take(&n);
    CHECK(n.verify_samples == 3 && n.verify_bad == 0, "exact fix: %u samples, %u bad",
          n.verify_samples, n.verify_bad);
    CHECK(pf_sp_stencil_mode() == 1, "still on");

    init(0, 1, 0, 2);
    g_inexact = 1;
    pf_sp_verify(slot, (void *)fake_update);
    CHECK(pf_sp_stencil_mode() == 1, "one bad sample: still on");
    pf_sp_verify(slot, (void *)fake_update);
    pf_sp_take(&n);
    CHECK(n.verify_samples == 2 && n.verify_bad == 2, "wrong fix: %u samples, %u bad",
          n.verify_samples, n.verify_bad);
    CHECK(pf_sp_stencil_mode() == 0, "two bad samples: off for the session");
    CHECK(!pf_sp_verify_due(), "nothing left to verify");

    g_inexact = 0;
    particle_world(1, 1);
    init(0, 1, 2, 2);
    for (i = 0; i < 3; i++)
        pf_sp_verify(slot, (void *)fake_update);
    pf_sp_take(&n);
    CHECK(n.verify_samples == 3 && n.verify_bad == 0,
          "two fixes and 'all three' rotate: %u samples, %u bad", n.verify_samples, n.verify_bad);
    CHECK(pf_sp_stencil_mode() == 1 && pf_sp_particles_mode() == 2, "both still on");

    /* LightSkipShadow as a fourth variant: held off in one of each sample's
     * two draws; a cut that changes the map is switched off by lights.c's
     * callback after two samples. */
    pf_sp_set_light_cut(fake_light_active, fake_light_off);
    init(0, 0, 0, 2);
    g_light_on = 1;
    g_light_wrong = 0;
    g_light_held = 0;
    CHECK(pf_sp_verify_due(), "the light cut alone is verified");
    for (i = 0; i < 2; i++)
        pf_sp_verify(slot, (void *)fake_update);
    pf_sp_take(&n);
    CHECK(n.verify_samples == 2 && n.verify_bad == 0 && g_light_held == 2,
          "exact light cut: %u samples, %u bad, held in %d draws", n.verify_samples, n.verify_bad,
          g_light_held);
    CHECK(!pf_sp_holding(PF_SP_LIGHT) && !pf_sp_verifying(), "nothing held after the verify");
    g_light_wrong = 1;
    pf_sp_verify(slot, (void *)fake_update);
    CHECK(g_light_on, "one bad sample: still on");
    pf_sp_verify(slot, (void *)fake_update);
    pf_sp_take(&n);
    CHECK(n.verify_bad == 2 && !g_light_on, "wrong light cut: %u bad, %s", n.verify_bad,
          g_light_on ? "still on" : "off");
    g_light_wrong = 0;
    pf_sp_set_light_cut(0, 0);

    IDirect3DSurface9_Release(g_depth);
    IDirect3DSurface9_Release(g_map);
    IDirect3DDevice9_Release(g_dev);
    IDirect3D9_Release(d3d);
    DestroyWindow(wnd);
    g_device = 0;
}

/* The device slots and state the stale-light count reads (lights.c). */
static void test_slots(void)
{
    printf("d3d9 slots\n");
    CHECK(offsetof(IDirect3DDevice9Vtbl, GetRenderState) / sizeof(void *) ==
              PF_DEV_GET_RENDER_STATE,
          "GetRenderState slot");
    CHECK(offsetof(IDirect3DDevice9Vtbl, GetVertexShader) / sizeof(void *) == PF_DEV_GET_VS,
          "GetVertexShader slot");
    CHECK(D3DRS_LIGHTING == PF_D3DRS_LIGHTING, "D3DRS_LIGHTING");
}

int main(void)
{
    test_slots();
    test_batch();
    test_batch_light();
    test_stencil();
    test_particles();
    test_guard();
    test_verify();
    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: shadow-pass tests passed\n");
    return 0;
}
