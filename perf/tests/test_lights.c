/* The light environment cuts against fake scenes, lights and game functions:
 * LightCull and LightReuse give what the game's own loops give (the same
 * Add_Light calls in the same order, the same environment and flag),
 * LightSkipShadow's continuations, LightVerify, the timers and the
 * stale-light count. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "d3d9vt.h"
#include "lights.h"
#include "shadowpass.h"
#include "sites.h"
#include "stubs.h"

/* fps.c isn't linked: the tests say when an extra frame is being drawn. */
static int g_in_extra;
int pf_fps_in_extra(void)
{
    return g_in_extra;
}

/* stubs.c calls these; nothing is hooked here. */
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

#define ENV_DWORDS (PF_LT_ENV_SIZE / 4)
#define MAX_FAKE 40

/* ---- fake lights: the sphere (integers here) at +0x4C, a colour at +0xE0 ---- */

typedef struct {
    unsigned char b[0x160];
} fake_light;

static int g_bsphere_calls, g_other_calls;

static const unsigned *__attribute__((thiscall)) fake_bsphere(void *obj)
{
    g_bsphere_calls++;
    return (const unsigned *)((unsigned char *)obj + 0x4C);
}

static const unsigned *__attribute__((thiscall)) other_bsphere(void *obj)
{
    g_other_calls++;
    return (const unsigned *)((unsigned char *)obj + 0x4C);
}

static void *g_vt[128], *g_other_vt[128];

static void light(fake_light *l, int x, int y, int z, int r, int type, int enabled, unsigned colour)
{
    memset(l, 0, sizeof *l);
    *(void ***)l->b = g_vt;
    ((int *)(l->b + 0x4C))[0] = x;
    ((int *)(l->b + 0x4C))[1] = y;
    ((int *)(l->b + 0x4C))[2] = z;
    ((int *)(l->b + 0x4C))[3] = r;
    *(int *)(l->b + PF_LT_LIGHT_TYPE) = type;
    l->b[PF_LT_LIGHT_ENABLED] = (unsigned char)enabled;
    *(unsigned *)(l->b + 0xE0) = colour;
}

/* (r1+r2)^2 > |c1-c2|^2, as the game's test but on integers. */
static unsigned char fake_spheres(const void *a, const void *b)
{
    const int *p = a, *q = b;
    long long dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2], r = (long long)p[3] + q[3];
    return r * r > dx * dx + dy * dy + dz * dz;
}

/* Add_Light: order-sensitive, reads the light's colour and the environment. */
static void *g_adds[2 * MAX_FAKE * 8];
static unsigned g_nadds;

static void __attribute__((thiscall)) fake_add(void *env, void *obj)
{
    unsigned *e = env;
    unsigned colour = *(unsigned *)((unsigned char *)obj + 0xE0);
    e[1]++;
    e[2 + e[1] % 16] ^= colour * 2654435761u;
    e[100] = e[100] * 31 + colour + (unsigned)(UINT_PTR)obj;
    e[101] += e[50];
    if (g_nadds < sizeof g_adds / sizeof g_adds[0])
        g_adds[g_nadds++] = obj;
}

static void __attribute__((thiscall)) fake_pru(void *env, const void *tm)
{
    unsigned *e = env;
    const unsigned *m = tm;
    e[120] = e[120] * 7 + m[0] + 3 * m[1];
    e[121] ^= e[1];
}

/* ---- a fake scene: the two lists as the game lays them out ---- */

typedef struct node {
    unsigned pad0;
    struct node *next; /* +4 */
    unsigned pad8;
    unsigned obj8;     /* +0xC: the light's +8 */
} node;

static unsigned char g_scene[0x800];
static node g_nodes[2][MAX_FAKE];
static fake_light g_lights[2][MAX_FAKE];
static unsigned g_count[2];

static void build_list(int which)
{
    unsigned list = which ? PF_LT_SCENE_DYNAMIC : PF_LT_SCENE_LIGHTS, i;
    node *sentinel = (node *)(g_scene + list);
    node *prev = sentinel;
    for (i = 0; i < g_count[which]; i++) {
        node *n = &g_nodes[which][i];
        n->obj8 = (unsigned)(UINT_PTR)g_lights[which][i].b + 8;
        prev->next = n;
        prev = n;
    }
    prev->next = sentinel;
}

/* renderOneObject's frame: EBP at +0x300. */
static unsigned char g_frame[0x400];
static unsigned char *const g_ebp = g_frame + 0x300;
static unsigned g_camera[16], g_robj[4];
static void *g_rinfo[4];
static volatile unsigned char g_flag;
static volatile int g_threshold = 7;
static volatile unsigned g_lighting;
static const unsigned char *volatile g_cur_env; /* Set_Light_Environment's cached one */
static volatile unsigned char g_lock;
static char g_markers[8];
static char *const NEXT = &g_markers[0];
static int g_pass_now = 2;

static int fake_pass(void)
{
    return g_pass_now;
}

static unsigned *env_of(unsigned char *ebp)
{
    return (unsigned *)(ebp + PF_LT_F_ENV);
}

static void object(int x, int y, int z, int r, int added, unsigned seed)
{
    unsigned i;
    unsigned *env = env_of(g_ebp);
    for (i = 0; i < ENV_DWORDS; i++)
        env[i] = seed * 0x9E3779B9u + i;
    ((int *)(g_ebp + PF_LT_F_OBJ_SPHERE))[0] = x;
    ((int *)(g_ebp + PF_LT_F_OBJ_SPHERE))[1] = y;
    ((int *)(g_ebp + PF_LT_F_OBJ_SPHERE))[2] = z;
    ((int *)(g_ebp + PF_LT_F_OBJ_SPHERE))[3] = r;
    *(int *)(g_ebp + PF_LT_F_ADDED) = added;
    g_ebp[PF_LT_F_LIGHTMAP] = 0;
    *(void **)(g_ebp + PF_LT_F_RINFO) = g_rinfo;
    *(void **)(g_ebp + 0xC) = g_robj;
}

typedef const unsigned *(__attribute__((thiscall)) * getter_fn)(void *obj);

/* The game's two loops (0x0006FC16..0x0006FD36), as the template pins them. */
static void game_lists(unsigned char *ebp, unsigned char *scene)
{
    unsigned which;
    for (which = 0; which < 2; which++) {
        node *sentinel = (node *)(scene + (which ? PF_LT_SCENE_DYNAMIC : PF_LT_SCENE_LIGHTS));
        node *n;
        for (n = sentinel->next; n != sentinel; n = n->next) {
            unsigned char *obj = n->obj8 ? (unsigned char *)(UINT_PTR)(n->obj8 - 8) : 0;
            getter_fn get;
            unsigned sph[4];
            if (which && !obj[PF_LT_LIGHT_ENABLED])
                continue;
            get = (getter_fn)(*(void ***)obj)[PF_LT_BSPHERE_SLOT / 4];
            memcpy(sph, get(obj), 16);
            if (*(int *)(obj + PF_LT_LIGHT_TYPE) != 0 ||
                fake_spheres(ebp + PF_LT_F_OBJ_SPHERE, sph)) {
                fake_add(ebp + PF_LT_F_ENV, obj);
                *(int *)(ebp + PF_LT_F_ADDED) = 1;
            }
        }
    }
}

static void **slot_of(void **stack)
{
    void **slot = stack + 12;
    slot[-8] = g_ebp;
    slot[-9] = g_scene;
    return slot;
}

static pf_lt_data g_d;

static void init(int skip, int cull, int reuse, int verify)
{
    g_d.glights_skip = (unsigned char *)&g_markers[1];
    g_d.lists = (unsigned char *)&g_markers[2];
    g_d.lists_after = (unsigned char *)&g_markers[3];
    g_d.lists_default = (unsigned char *)&g_markers[4];
    g_d.add_light = (void *)fake_add;
    g_d.pre_render_update = (void *)fake_pru;
    g_d.spheres_intersect = (void *)fake_spheres;
    g_d.bsphere = (void *)fake_bsphere;
    g_d.threshold = &g_threshold;
    g_d.lighting = &g_lighting;
    g_d.light_env = &g_cur_env;
    g_d.light_lock = &g_lock;
    g_cur_env = 0;
    g_lock = 0;
    g_vt[PF_LT_BSPHERE_SLOT / 4] = (void *)fake_bsphere;
    g_other_vt[PF_LT_BSPHERE_SLOT / 4] = (void *)other_bsphere;
    pf_lt_init(&g_d, &g_flag, skip, cull, reuse, verify);
    pf_lt_set_installed(1, 1);
    pf_lt_set_timing(1, fake_pass);
    g_flag = 0;
    g_pass_now = 2;
}

static unsigned g_rand = 12345;

static unsigned rnd(unsigned n)
{
    g_rand = g_rand * 1103515245u + 12345u;
    return (g_rand >> 8) % n;
}

static void random_scene(void)
{
    unsigned which, i;
    for (which = 0; which < 2; which++) {
        g_count[which] = rnd(which ? 30 : 20);
        for (i = 0; i < g_count[which]; i++)
            light(&g_lights[which][i], (int)rnd(400), (int)rnd(400), (int)rnd(100),
                  1 + (int)rnd(80), rnd(6) == 0 ? 1 + (int)rnd(2) : 0, rnd(4) != 0,
                  rnd(0xFFFFFF));
        build_list(which);
    }
}

/* Runs the plugin's lists on the current frame and the game's on a copy:
 * both must give the same environment, flag and Add_Light calls. */
static int same_as_game(const char *what, int expect_plugin)
{
    unsigned char saved[sizeof g_frame];
    unsigned ref[ENV_DWORDS];
    void *ref_adds[sizeof g_adds / sizeof g_adds[0]], *stack[16];
    unsigned nref, i;
    int ref_added, ok;
    void *to;

    memcpy(saved, g_frame, sizeof g_frame);
    g_nadds = 0;
    game_lists(g_ebp, g_scene);
    memcpy(ref, env_of(g_ebp), sizeof ref);
    ref_added = *(int *)(g_ebp + PF_LT_F_ADDED);
    nref = g_nadds;
    memcpy(ref_adds, g_adds, nref * sizeof g_adds[0]);
    memcpy(g_frame, saved, sizeof g_frame);
    g_nadds = 0;
    to = pf_lt_enter(PF_ROO_LISTS, slot_of(stack), NEXT);
    if (expect_plugin && to != g_d.lists_after) {
        CHECK(0, "%s: the plugin left the lists to the game", what);
        return 0;
    }
    if (to == NEXT)
        game_lists(g_ebp, g_scene);
    ok = !memcmp(ref, env_of(g_ebp), sizeof ref) &&
         ref_added == *(int *)(g_ebp + PF_LT_F_ADDED);
    if (to == g_d.lists_after && expect_plugin != 2) {
        ok = ok && g_nadds == nref;
        for (i = 0; ok && i < nref; i++)
            ok = g_adds[i] == ref_adds[i];
    }
    CHECK(ok, "%s: the plugin's lists differ from the game's (%u adds, game %u)", what, g_nadds,
          nref);
    return ok;
}

static void test_cull(void)
{
    pf_lt_counts c;
    void *stack[16];
    unsigned trial, i, lights;
    int all = 1;

    printf("LightCull\n");
    init(0, 1, 0, 0);
    for (trial = 0; trial < 400; trial++) {
        random_scene();
        pf_lt_pass_begin();
        object((int)rnd(400), (int)rnd(400), (int)rnd(100), 1 + (int)rnd(60), (int)rnd(2), trial);
        all &= same_as_game("random scene", 1);
    }
    CHECK(all, "every random scene matches");

    /* One snapshot per pass: the bounding spheres are read once per light. */
    random_scene();
    pf_lt_pass_begin();
    lights = g_count[0];
    for (i = 0; i < g_count[1]; i++)
        lights += g_lights[1][i].b[PF_LT_LIGHT_ENABLED] != 0;
    g_bsphere_calls = 0;
    for (i = 0; i < 5; i++) {
        object((int)rnd(400), (int)rnd(400), 10, 30, 1, i);
        pf_lt_enter(PF_ROO_LISTS, slot_of(stack), NEXT);
    }
    CHECK((unsigned)g_bsphere_calls == lights, "5 objects: %d sphere reads for %u lights",
          g_bsphere_calls, lights);
    pf_lt_pass_begin();
    pf_lt_enter(PF_ROO_LISTS, slot_of(stack), NEXT);
    CHECK((unsigned)g_bsphere_calls == 2 * lights, "a new pass reads them again");

    /* A light the snapshot can't vouch for leaves the pass to the game. */
    g_count[0] = 3;
    light(&g_lights[0][0], 0, 0, 0, 50, 0, 1, 1);
    light(&g_lights[0][1], 10, 0, 0, 50, 0, 1, 2);
    light(&g_lights[0][2], 20, 0, 0, 50, 0, 1, 3);
    build_list(0);
    g_count[1] = 0;
    build_list(1);
    *(void **)(g_lights[0][1].b + PF_LT_CONTAINER) = (void *)stack;
    pf_lt_take(&c);
    pf_lt_pass_begin();
    object(0, 0, 0, 10, 0, 1);
    CHECK(pf_lt_enter(PF_ROO_LISTS, slot_of(stack), NEXT) == NEXT,
          "a light with a container: the game's loops");
    *(void **)(g_lights[0][1].b + PF_LT_CONTAINER) = 0;
    *(void ***)g_lights[0][2].b = g_other_vt;
    pf_lt_pass_begin();
    g_other_calls = 0;
    CHECK(pf_lt_enter(PF_ROO_LISTS, slot_of(stack), NEXT) == NEXT && g_other_calls == 0,
          "another bounding sphere getter: the game's loops, and it isn't called");
    *(void ***)g_lights[0][2].b = g_vt;
    pf_lt_pass_begin();
    CHECK(pf_lt_enter(PF_ROO_LISTS, slot_of(stack), NEXT) == g_d.lists_after, "back to normal");
    pf_lt_take(&c);
    CHECK(c.fallback == 2, "two passes left to the game: %u", c.fallback);

    /* A second scene in the same pass is snapshotted on its own. */
    {
        static unsigned char scene2[0x800];
        static node n2;
        static fake_light l2;
        unsigned ref[ENV_DWORDS];
        node *s = (node *)(scene2 + PF_LT_SCENE_LIGHTS);
        node *dyn = (node *)(scene2 + PF_LT_SCENE_DYNAMIC);
        void **slot = slot_of(stack);
        light(&l2, 0, 0, 0, 0, 1, 1, 0x4242); /* directional: always added */
        n2.obj8 = (unsigned)(UINT_PTR)l2.b + 8;
        s->next = &n2;
        n2.next = s;
        dyn->next = dyn;
        random_scene();
        pf_lt_pass_begin();
        object(5000, 5000, 0, 1, 0, 3);
        pf_lt_enter(PF_ROO_LISTS, slot, NEXT);
        object(5000, 5000, 0, 1, 0, 3);
        game_lists(g_ebp, scene2);
        memcpy(ref, env_of(g_ebp), sizeof ref);
        object(5000, 5000, 0, 1, 0, 3);
        slot[-9] = scene2;
        CHECK(pf_lt_enter(PF_ROO_LISTS, slot, NEXT) == g_d.lists_after &&
                  !memcmp(ref, env_of(g_ebp), sizeof ref),
              "the second scene's object gets that scene's lights");
    }

    /* Off: the game's loops, but still counted. */
    init(0, 0, 0, 0);
    random_scene();
    pf_lt_pass_begin();
    object(100, 100, 10, 30, 0, 9);
    CHECK(pf_lt_enter(PF_ROO_LISTS, slot_of(stack), NEXT) == NEXT, "LightCull=0: the game's loops");
}

static void test_reuse(void)
{
    pf_lt_counts c;

    printf("LightReuse\n");
    init(0, 1, 1, 0);
    g_count[0] = 4;
    light(&g_lights[0][0], 0, 0, 0, 30, 0, 1, 11);    /* near */
    light(&g_lights[0][1], 5000, 0, 0, 30, 0, 1, 12); /* far */
    light(&g_lights[0][2], 0, 0, 0, 0, 1, 1, 13);     /* directional: always */
    light(&g_lights[0][3], 20, 0, 0, 30, 0, 1, 14);   /* near */
    build_list(0);
    g_count[1] = 2;
    light(&g_lights[1][0], 10, 10, 0, 30, 0, 1, 21); /* near */
    light(&g_lights[1][1], 0, 9000, 0, 30, 0, 1, 22); /* far */
    build_list(1);
    pf_lt_take(&c);

    pf_lt_pass_begin();
    object(0, 0, 0, 10, 1, 5);
    same_as_game("first build", 1);
    pf_lt_pass_begin();
    object(0, 0, 0, 10, 1, 5);
    g_nadds = 0;
    same_as_game("same inputs", 2);
    CHECK(g_nadds == 0, "same inputs: reused without Add_Light (%u calls)", g_nadds);
    pf_lt_take(&c);
    CHECK(c.reuse_hits == 1 && c.reuse_misses == 1, "1 hit, 1 miss: %u, %u", c.reuse_hits,
          c.reuse_misses);

    /* A far light changes: still reused. */
    *(unsigned *)(g_lights[0][1].b + 0xE0) = 99;
    pf_lt_pass_begin();
    object(0, 0, 0, 10, 1, 5);
    same_as_game("a far light changed", 2);
    pf_lt_take(&c);
    CHECK(c.reuse_hits == 1, "a far light's change doesn't matter");
    /* A kept light changes, the environment before the lists, the object's
     * sphere, the flag, the cut-off: each one rebuilds. */
    *(unsigned *)(g_lights[1][0].b + 0xE0) = 77;
    pf_lt_pass_begin();
    object(0, 0, 0, 10, 1, 5);
    same_as_game("a kept light changed", 1);
    pf_lt_pass_begin();
    object(0, 0, 0, 10, 1, 6);
    same_as_game("another environment before the lists", 1);
    pf_lt_pass_begin();
    object(0, 0, 0, 11, 1, 6);
    same_as_game("another sphere", 1);
    pf_lt_pass_begin();
    object(0, 0, 0, 11, 0, 6);
    same_as_game("another flag", 1);
    g_threshold = 8;
    pf_lt_pass_begin();
    object(0, 0, 0, 11, 0, 6);
    same_as_game("another cut-off", 1);
    pf_lt_take(&c);
    CHECK(c.reuse_hits == 0 && c.reuse_misses == 5, "five rebuilds: %u hits, %u misses",
          c.reuse_hits, c.reuse_misses);
    pf_lt_pass_begin();
    object(0, 0, 0, 11, 0, 6);
    same_as_game("unchanged again", 2);
    pf_lt_take(&c);
    CHECK(c.reuse_hits == 1, "then reused again");
    g_threshold = 7;
    /* A kept light leaves the list. */
    g_count[0] = 3;
    build_list(0);
    pf_lt_pass_begin();
    object(0, 0, 0, 11, 0, 6);
    same_as_game("a kept light removed", 1);
    pf_lt_take(&c);
    CHECK(c.reuse_hits == 0, "a removed light rebuilds");
}

/* Render objects of their own (LightReuse keeps one build per object). */
static unsigned g_robjs[8][64];

static void object_n(unsigned n, int x, int y, int z, int r, unsigned seed)
{
    object(x, y, z, r, 1, seed);
    *(void **)(g_ebp + 0xC) = g_robjs[n];
}

/* The plugin's lists for object n match the game's, from its stored build
 * (reuse) or from Add_Light calls (every object here keeps a light). */
static int check_object(const char *what, unsigned n, int x, int reuse)
{
    object_n(n, x, 0, 0, 10, 100 + n);
    if (!same_as_game(what, reuse ? 2 : 1))
        return 0;
    CHECK(reuse ? g_nadds == 0 : g_nadds > 0, "%s: %s (%u Add_Light calls)", what,
          reuse ? "rebuilt" : "reused", g_nadds);
    return 1;
}

/* Catch-up skips a render after walking the scene's update list (catchup.c):
 * dynamic lights fade and move outside any pass. The next object, even one
 * before the next pass start, is decided from the lights as they are now,
 * and no build that kept a changed light is reused. */
static void test_skipped_render(void)
{
    pf_lt_counts c;

    printf("a render catch-up skipped\n");
    init(0, 1, 1, 0);
    g_count[0] = 1;
    light(&g_lights[0][0], 0, 0, 0, 0, 1, 1, 31); /* directional: every object */
    build_list(0);
    g_count[1] = 2;
    light(&g_lights[1][0], 10, 0, 0, 30, 0, 1, 41);   /* fading, near object 0 */
    light(&g_lights[1][1], 3000, 0, 0, 30, 0, 1, 42); /* near object 1 */
    build_list(1);
    pf_lt_take(&c);

    /* A drawn frame keeps a build for each of three objects. */
    pf_lt_pass_begin();
    check_object("drawn frame, object 0", 0, 0, 0);
    check_object("drawn frame, object 1", 1, 3000, 0);
    check_object("drawn frame, object 2", 2, -5000, 0);
    /* The skipped frame's walk: the near light fades a step, the other moves
     * from object 1 to object 0. */
    *(unsigned *)(g_lights[1][0].b + 0xE0) = 40;
    ((int *)(g_lights[1][1].b + 0x4C))[0] = 20;
    pf_lt_render_skipped();
    /* No pass start yet: the snapshot is taken again all the same. */
    check_object("after the skip, before a pass: object 0", 0, 0, 0);
    pf_lt_pass_begin();
    check_object("next frame: object 0, as just built", 0, 0, 1);
    check_object("next frame: object 1, its light gone", 1, 3000, 0);
    check_object("next frame: object 2, nothing changed", 2, -5000, 1);
    pf_lt_take(&c);
    CHECK(c.reuse_misses == 5 && c.reuse_hits == 2, "%u misses (want 5), %u hits (want 2)",
          c.reuse_misses, c.reuse_hits);

    /* Two skips in a row (MaxSkips=2) whose walks fade the light and bring
     * it back: the next snapshot finds the values of the stored build, and
     * reusing it gives what the game gives. A skip that does change it
     * rebuilds. */
    *(unsigned *)(g_lights[1][0].b + 0xE0) = 39;
    pf_lt_render_skipped();
    *(unsigned *)(g_lights[1][0].b + 0xE0) = 40;
    pf_lt_render_skipped();
    pf_lt_pass_begin();
    check_object("after two skips, the light as it was: object 0", 0, 0, 1);
    *(unsigned *)(g_lights[1][0].b + 0xE0) = 38;
    pf_lt_render_skipped();
    pf_lt_pass_begin();
    check_object("after a skip that faded it: object 0", 0, 0, 0);
    check_object("after a skip that faded it: object 2", 2, -5000, 1);
    pf_lt_take(&c);
    CHECK(c.reuse_hits == 2 && c.reuse_misses == 1, "%u hits (want 2), %u misses (want 1)",
          c.reuse_hits, c.reuse_misses);
}

/* Extra frames hold the scene's update list (fps.c, scene_update), so they
 * see the lights the game frame's main view left: every object reuses its
 * build there. In a game frame the shadow map and the reflection come before
 * the main view's walk, so they reuse what the last main view built, and
 * only objects the walk's changes reach rebuild. A light that did change in
 * an extra frame is never reused stale. */
static void test_extra_frames(void)
{
    pf_lt_counts c;
    unsigned k;

    printf("extra frames and the held update list\n");
    init(0, 1, 1, 0);
    g_count[0] = 1;
    light(&g_lights[0][0], 0, 0, 0, 0, 1, 1, 31);
    build_list(0);
    g_count[1] = 1;
    light(&g_lights[1][0], 10, 0, 0, 30, 0, 1, 50); /* fading, near object 0 only */
    build_list(1);
    pf_lt_take(&c);

    /* Game frame 1: shadow map, reflection, then the main view's walk. */
    g_pass_now = 0;
    pf_lt_pass_begin();
    check_object("frame 1 shadow: object 0", 0, 0, 0);
    check_object("frame 1 shadow: object 1", 1, -5000, 0);
    g_pass_now = 1;
    pf_lt_pass_begin();
    check_object("frame 1 reflection: object 0", 0, 0, 1);
    check_object("frame 1 reflection: object 1", 1, -5000, 1);
    g_pass_now = 2;
    *(unsigned *)(g_lights[1][0].b + 0xE0) = 49;
    pf_lt_pass_begin();
    check_object("frame 1 views: object 0 (its light faded)", 0, 0, 0);
    check_object("frame 1 views: object 1", 1, -5000, 1);
    pf_lt_take(&c);
    CHECK(c.reuse_hits == 3 && c.reuse_misses == 3 && !c.extra_hits && !c.extra_misses,
          "frame 1: %u hits, %u misses, extra %u/%u", c.reuse_hits, c.reuse_misses,
          c.extra_hits, c.extra_misses);

    /* Three extra frames: the main view, nothing walked. */
    g_in_extra = 1;
    for (k = 0; k < 3; k++) {
        pf_lt_pass_begin();
        check_object("extra frame: object 0", 0, 0, 1);
        check_object("extra frame: object 1", 1, -5000, 1);
    }
    g_in_extra = 0;
    pf_lt_take(&c);
    CHECK(c.extra_hits == 6 && !c.extra_misses && !c.reuse_hits && !c.reuse_misses,
          "extra frames: %u hits, %u misses (game frames' %u/%u)", c.extra_hits,
          c.extra_misses, c.reuse_hits, c.reuse_misses);

    /* Game frame 2: its shadow map sees what the extra frames saw. */
    g_pass_now = 0;
    pf_lt_pass_begin();
    check_object("frame 2 shadow: object 0", 0, 0, 1);
    g_pass_now = 2;
    *(unsigned *)(g_lights[1][0].b + 0xE0) = 48;
    pf_lt_pass_begin();
    check_object("frame 2 views: object 0", 0, 0, 0);
    check_object("frame 2 views: object 1", 1, -5000, 1);

    /* Were a light to step in an extra frame, its builds aren't reused. */
    g_in_extra = 1;
    *(unsigned *)(g_lights[1][0].b + 0xE0) = 47;
    pf_lt_pass_begin();
    check_object("an extra frame whose light changed: object 0", 0, 0, 0);
    g_in_extra = 0;
    pf_lt_take(&c);
    CHECK(c.extra_misses == 1 && c.extra_hits == 0, "extra frame with a change: %u/%u",
          c.extra_hits, c.extra_misses);
}

static void test_skip(void)
{
    pf_lt_counts c;
    void *stack[16], **slot = slot_of(stack);

    printf("LightSkipShadow\n");
    init(1, 1, 0, 0);
    random_scene();
    pf_lt_take(&c);
    g_flag = 1;
    pf_lt_pass_begin();
    object(10, 10, 10, 10, 0, 1);
    CHECK(pf_lt_enter(PF_ROO_GLIGHTS, slot, NEXT) == g_d.glights_skip, "global lights skipped");
    CHECK(pf_lt_enter(PF_ROO_RESET_ND, slot, NEXT) == g_d.lists,
          "no drawable: Reset and the global lights skipped");
    CHECK(pf_lt_enter(PF_ROO_LISTS, slot, NEXT) == g_d.lists_default,
          "lists skipped: the default environment");
    pf_lt_take(&c);
    CHECK(c.skipped == 1, "one build skipped: %u", c.skipped);
    g_flag = 0;
    CHECK(pf_lt_enter(PF_ROO_GLIGHTS, slot, NEXT) == NEXT &&
              pf_lt_enter(PF_ROO_RESET_ND, slot, NEXT) == NEXT,
          "other passes: the game's build");
    g_flag = 1;
    pf_lt_skip_off();
    CHECK(!pf_lt_skip_active() && pf_lt_enter(PF_ROO_GLIGHTS, slot, NEXT) == NEXT,
          "switched off: the game's build");
    init(0, 1, 0, 0);
    g_flag = 1;
    CHECK(pf_lt_enter(PF_ROO_GLIGHTS, slot, NEXT) == NEXT, "LightSkipShadow=0: the game's build");
    g_flag = 0;
}

/* The game's lists and Pre_Render_Update after the plugin handed back the
 * object, then the render info's set (EAX: the environment). */
static void game_rest(void **slot, int corrupt)
{
    unsigned *env = env_of(g_ebp);
    int local;
    game_lists(g_ebp, g_scene);
    if (corrupt)
        env[3] ^= 1;
    local = *(int *)(g_ebp + PF_LT_F_ADDED) || g_ebp[PF_LT_F_LIGHTMAP];
    if (local)
        fake_pru(env, g_camera + PF_LT_CAMERA_TM / 4);
    slot[-3] = local ? (void *)env : (void *)(g_scene + PF_LT_SCENE_DEFAULT_ENV);
    pf_lt_enter(PF_ROO_ENV, slot, NEXT);
}

static void test_verify(void)
{
    pf_lt_counts c;
    void *stack[16], **slot = slot_of(stack);
    int i;

    printf("LightVerify\n");
    g_rinfo[0] = g_camera;
    g_camera[6] = 3;
    g_camera[7] = 4;
    init(0, 1, 1, 2);
    for (i = 0; i < 20; i++) {
        random_scene();
        pf_lt_pass_begin();
        object((int)rnd(400), (int)rnd(400), (int)rnd(100), 1 + (int)rnd(60), (int)rnd(2), i);
        g_ebp[PF_LT_F_LIGHTMAP] = (unsigned char)(i % 3 == 0);
        CHECK(pf_lt_enter(PF_ROO_LISTS, slot, NEXT) == NEXT, "a sample runs the game's loops");
        game_rest(slot, 0);
    }
    pf_lt_take(&c);
    CHECK(c.verify_samples == 20 && c.verify_bad == 0, "20 samples, %u bad (of %u)", c.verify_bad,
          c.verify_samples);
    CHECK(pf_lt_cull_on() && pf_lt_reuse_on(), "both still on");

    /* A game that disagrees, twice: LightCull (and so LightReuse) off. */
    random_scene();
    g_count[0] = 1;
    light(&g_lights[0][0], 0, 0, 0, 0, 1, 1, 5);
    build_list(0);
    for (i = 0; i < 2; i++) {
        pf_lt_pass_begin();
        object(1000 + i, 0, 0, 1, 0, 100 + i);
        pf_lt_enter(PF_ROO_LISTS, slot, NEXT);
        game_rest(slot, 1);
        CHECK((i == 0) == pf_lt_cull_on(), "after %d bad sample(s): LightCull %s", i + 1,
              pf_lt_cull_on() ? "on" : "off");
    }
    CHECK(!pf_lt_reuse_on(), "LightReuse off with LightCull");
    pf_lt_pass_begin();
    CHECK(pf_lt_enter(PF_ROO_LISTS, slot, NEXT) == NEXT, "off: the game's loops");

    /* A reused build the game disagrees with, twice: LightReuse off,
     * LightCull stays. The first sample stores the build (and agrees). */
    init(0, 1, 1, 2);
    g_count[0] = 1;
    light(&g_lights[0][0], 0, 0, 0, 30, 0, 1, 5);
    build_list(0);
    g_count[1] = 0;
    build_list(1);
    pf_lt_take(&c);
    for (i = 0; i < 3; i++) {
        pf_lt_pass_begin();
        object(0, 0, 0, 10, 0, 50);
        pf_lt_enter(PF_ROO_LISTS, slot, NEXT);
        game_rest(slot, i > 0);
    }
    pf_lt_take(&c);
    CHECK(c.verify_samples == 3 && c.verify_bad == 2, "3 samples, 2 bad: %u, %u",
          c.verify_samples, c.verify_bad);
    CHECK(!pf_lt_reuse_on() && pf_lt_cull_on(),
          "two bad reused samples: LightReuse off, LightCull on (%d, %d)", pf_lt_reuse_on(),
          pf_lt_cull_on());
}

static void test_timing(void)
{
    pf_lt_counts c;
    void *stack[16], **slot = slot_of(stack), *ret_stack[8], **ret_slot = ret_stack + 2, *to;
    char ret_marker;

    printf("timers\n");
    init(0, 0, 0, 0);
    random_scene();
    pf_lt_take(&c);
    g_pass_now = 1;
    *ret_slot = &ret_marker;
    pf_lt_enter(PF_ROO, ret_slot, NEXT);
    CHECK(*ret_slot == pf_exit_stubs[PF_ROO], "renderOneObject returns through its exit stub");
    pf_lt_enter(PF_ROO_RESET, slot, NEXT);
    pf_lt_pass_begin();
    object(100, 100, 10, 30, 0, 3);
    CHECK(pf_lt_enter(PF_ROO_LISTS, slot, NEXT) == NEXT, "timers only: the game's loops");
    pf_lt_enter(PF_ROO_ADD_STATIC, slot, NEXT);
    pf_lt_enter(PF_ROO_ADD_DYN, slot, NEXT);
    pf_lt_enter(PF_ROO_ADD_DYN, slot, NEXT);
    slot[-3] = env_of(g_ebp);
    pf_lt_enter(PF_ROO_ENV, slot, NEXT);
    Sleep(2);
    pf_lt_enter(PF_ROO_RENDER, slot, NEXT);
    to = pf_lt_roo_exit((unsigned char *)ret_slot + 4 + 16);
    CHECK(to == &ret_marker, "the real return address comes back");
    Sleep(5);
    pf_lt_take(&c);
    CHECK(c.calls[1] == 1 && c.calls[0] == 0 && c.calls[2] == 0,
          "one call in the reflection: %u %u %u", c.calls[0], c.calls[1], c.calls[2]);
    CHECK(c.us[1] >= 1000 && c.pre_us[1] >= 1000 && c.us[1] >= c.pre_us[1] &&
              c.pre_us[1] >= c.env_us[1],
          "times: %u us, %u before Render, %u light build", (unsigned)c.us[1],
          (unsigned)c.pre_us[1], (unsigned)c.env_us[1]);
    CHECK(c.scene_kept[1] == 1 && c.dyn_kept[1] == 2 && c.scene_tested[1] == g_count[0],
          "kept from the game's Add_Light calls: %u %u, tested %u", c.scene_kept[1],
          c.dyn_kept[1], c.scene_tested[1]);
    g_pass_now = -1;
    pf_lt_enter(PF_ROO, ret_slot, NEXT);
    pf_lt_roo_exit((unsigned char *)ret_slot + 4 + 16);
    pf_lt_take(&c);
    CHECK(!c.calls[0] && !c.calls[1] && !c.calls[2], "outside a pass: not counted");
    g_pass_now = 2;
}

static void test_stale(void)
{
    pf_lt_counts c;
    void *stack[16], **slot = slot_of(stack), *arg[4];
    static unsigned mesh_env[ENV_DWORDS];
    unsigned stack_env[4];

    printf("stale light count\n");
    init(1, 1, 0, 0);
    random_scene();
    pf_lt_take(&c);
    g_lighting = 1;
    g_flag = 1;
    pf_lt_pass_begin();
    object(0, 0, 0, 10, 0, 1);
    pf_lt_enter(PF_ROO_LISTS, slot, NEXT); /* skipped */
    arg[1] = mesh_env;
    pf_lt_enter(PF_LENV_SET, arg, NEXT);
    g_flag = 0;
    pf_lt_draw();
    pf_lt_draw();
    arg[1] = mesh_env;
    pf_lt_enter(PF_LENV_SET, arg, NEXT);
    pf_lt_draw();
    pf_lt_take(&c);
    CHECK(c.stale_draws == 2, "two lit draws before the next light environment: %u",
          c.stale_draws);
    /* The terrain's environment (on the stack) last: built as the game builds it. */
    g_flag = 1;
    pf_lt_pass_begin();
    pf_lt_enter(PF_ROO_LISTS, slot, NEXT);
    arg[1] = mesh_env;
    pf_lt_enter(PF_LENV_SET, arg, NEXT);
    arg[1] = stack_env;
    pf_lt_enter(PF_LENV_SET, arg, NEXT);
    arg[1] = 0;
    pf_lt_enter(PF_LENV_SET, arg, NEXT);
    g_flag = 0;
    pf_lt_draw();
    /* Unlit draws don't count. */
    g_flag = 1;
    arg[1] = mesh_env;
    pf_lt_enter(PF_LENV_SET, arg, NEXT);
    g_flag = 0;
    g_lighting = 0;
    pf_lt_draw();
    pf_lt_take(&c);
    CHECK(c.stale_draws == 0, "the terrain's or unlit: %u", c.stale_draws);
    /* A shadow pass without skipped builds leaves nothing stale. */
    g_lighting = 1;
    init(0, 1, 0, 0);
    g_flag = 1;
    pf_lt_pass_begin();
    arg[1] = mesh_env;
    pf_lt_enter(PF_LENV_SET, arg, NEXT);
    g_flag = 0;
    pf_lt_draw();
    pf_lt_take(&c);
    CHECK(c.stale_draws == 0, "no skipped build: %u", c.stale_draws);
}

/* A device that answers GetRenderState(D3DRS_LIGHTING) and GetVertexShader. */
static void *g_dev_vt[PF_DEV_SLOTS], *g_vs_vt[8];
static void *g_dev_obj = g_dev_vt, *g_vs_obj = g_vs_vt;
static void *g_dev = &g_dev_obj;
static DWORD g_dev_lighting;
static void *g_dev_vs;
static int g_dev_fail, g_vs_released;

static HRESULT __stdcall dev_get_state(void *dev, DWORD state, DWORD *value)
{
    (void)dev;
    if (g_dev_fail || state != PF_D3DRS_LIGHTING)
        return E_FAIL;
    *value = g_dev_lighting;
    return S_OK;
}

static HRESULT __stdcall dev_get_vs(void *dev, void **vs)
{
    (void)dev;
    *vs = g_dev_vs;
    return S_OK;
}

static ULONG __stdcall vs_release(void *vs)
{
    (void)vs;
    g_vs_released++;
    return 1;
}

/* The FX batches hold the lights: their setups only store, the unlock
 * writes the last stored; the same environment again writes nothing; the
 * device says whether a draw lights its vertices with them. */
static void test_stale_device(void)
{
    pf_lt_counts c;
    void *stack[16], **slot = slot_of(stack), *arg[4];
    static unsigned mesh_env[ENV_DWORDS], other_env[ENV_DWORDS];

    printf("stale light count: the lock, the cached environment, the device\n");
    init(1, 1, 0, 0);
    random_scene();
    pf_lt_take(&c);
    g_lighting = 1;
    /* A view pass's environment, then a shadow pass whose setups store a
     * skipped object's environment under the lock: nothing written yet. */
    arg[1] = other_env;
    pf_lt_enter(PF_LENV_SET, arg, NEXT);
    g_flag = 1;
    pf_lt_pass_begin();
    object(0, 0, 0, 10, 0, 1);
    pf_lt_enter(PF_ROO_LISTS, slot, NEXT); /* skipped */
    g_lock = 1;
    arg[1] = mesh_env;
    pf_lt_enter(PF_LENV_SET, arg, NEXT);
    g_flag = 0;
    pf_lt_draw();
    pf_lt_take(&c);
    CHECK(c.stale_draws == 0, "under the lock the device keeps the earlier lights: %u",
          c.stale_draws);
    /* The unlock writes them. */
    g_flag = 1;
    g_lock = 0;
    pf_lt_enter(PF_LENV_SET, arg, NEXT);
    g_flag = 0;
    pf_lt_draw();
    /* The same environment again with byte 0 set writes nothing: still stale. */
    g_cur_env = (const unsigned char *)mesh_env;
    ((unsigned char *)mesh_env)[0] = 1;
    pf_lt_enter(PF_LENV_SET, arg, NEXT);
    pf_lt_draw();
    /* NULL writes nothing either. */
    arg[1] = 0;
    pf_lt_enter(PF_LENV_SET, arg, NEXT);
    pf_lt_draw();
    pf_lt_take(&c);
    CHECK(c.stale_draws == 3, "the unlock's write lasts until another write: %u",
          c.stale_draws);
    /* Without byte 0 the same one is written again. */
    ((unsigned char *)mesh_env)[0] = 0;
    arg[1] = mesh_env;
    pf_lt_enter(PF_LENV_SET, arg, NEXT);
    pf_lt_draw();
    pf_lt_take(&c);
    CHECK(c.stale_draws == 0, "rewritten outside the shadow pass: %u", c.stale_draws);
    g_cur_env = 0;

    /* With the device: only fixed-function lit draws count. */
    g_dev_vt[PF_DEV_GET_RENDER_STATE] = (void *)dev_get_state;
    g_dev_vt[PF_DEV_GET_VS] = (void *)dev_get_vs;
    g_vs_vt[PF_UNK_RELEASE] = (void *)vs_release;
    pf_lt_set_device(&g_dev);
    g_flag = 1;
    pf_lt_pass_begin();
    pf_lt_enter(PF_ROO_LISTS, slot, NEXT);
    arg[1] = mesh_env;
    pf_lt_enter(PF_LENV_SET, arg, NEXT);
    g_flag = 0;
    g_lighting = 0;
    g_dev_lighting = 1;
    g_dev_vs = 0;
    pf_lt_draw(); /* counted: the device lights, the cache doesn't matter */
    g_dev_vs = &g_vs_obj;
    g_vs_released = 0;
    pf_lt_draw(); /* a vertex shader */
    CHECK(g_vs_released == 1, "the shader's reference is released");
    g_dev_lighting = 0;
    g_dev_vs = 0;
    pf_lt_draw(); /* unlit */
    g_dev_fail = 1;
    pf_lt_draw(); /* the device can't say: counted */
    g_dev_fail = 0;
    pf_lt_take(&c);
    CHECK(c.stale_draws == 2, "fixed-function lit or unknown: %u", c.stale_draws);
    pf_lt_set_device(0);
    g_lighting = 1;
}

int main(void)
{
    test_cull();
    test_reuse();
    test_skipped_render();
    test_extra_frames();
    test_skip();
    test_verify();
    test_timing();
    test_stale();
    test_stale_device();
    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: light tests passed\n");
    return 0;
}
