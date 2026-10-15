/* Command button clocks in extra frames, in the game's order: each game
 * frame draws, then the control bar's update asks for the clocks for the
 * next draw, then the limiter's wait draws the extra frames. Every extra
 * frame and the next game draw must show the clock; without the plugin
 * only the first extra frame does (one frame in four at 120 FPS, as the
 * 0.9.1 run showed). Also the request counts and pf_btn_find's checks of the
 * push button draw and its three draws. */
#include <stdio.h>
#include <string.h>

#include "btnclock.h"

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

/* A window: its user data (the button data) at +0x2C. */
typedef struct {
    unsigned char pad[0x2C];
    unsigned char *data;
} window;

/* The data: the request byte, then the percentage and the color. */
typedef struct {
    unsigned char kind, pad[3];
    int percent;
    unsigned color;
} button;

#define NW 3
static window g_w[NW];
static button g_b[NW];
static int g_percent_drawn[NW];

/* The game's push button draw: draws the clock asked for and clears the
 * request. hooked = the plugin's probe at its entry. Returns 1 if drawn. */
static int draw(window *w, int extra, int hooked)
{
    button *b = (button *)w->data;
    if (hooked)
        pf_btn_draw(w, extra);
    if (b && b->kind) {
        g_percent_drawn[w - g_w] = b->percent;
        b->kind = 0;
        return 1;
    }
    return 0;
}

/* One game frame as GameClient::update and the limiter run it: the draw of
 * windows [0, drawn), then the control bar's update asking a clock (kind 2)
 * on the windows with ask[i] >= 0 at that percentage, then `extras` extra
 * frames drawing the same windows. shown[i] counts the frames (the game's
 * and the extra ones) that showed window i's clock. */
static void game_frame(unsigned seq, unsigned drawn, const int *ask, unsigned extras, int hooked,
                       unsigned *shown)
{
    unsigned i, e;
    for (i = 0; i < drawn; i++)
        shown[i] += (unsigned)draw(&g_w[i], 0, hooked);
    for (i = 0; i < NW; i++) {
        if (ask[i] < 0)
            continue;
        g_b[i].kind = 2;
        g_b[i].percent = ask[i];
        if (hooked)
            pf_btn_request(&g_w[i], seq);
    }
    for (e = 0; e < extras; e++) {
        for (i = 0; i < drawn; i++)
            shown[i] += (unsigned)draw(&g_w[i], 1, hooked);
        if (hooked)
            pf_btn_extra_end(seq);
    }
}

static void reset(void)
{
    unsigned i;
    for (i = 0; i < NW; i++) {
        g_w[i].data = (unsigned char *)&g_b[i];
        g_b[i].kind = 0;
        g_b[i].percent = 0;
        g_b[i].color = 0x80FFFFFFu;
        g_percent_drawn[i] = -1;
    }
    pf_btn_init(0x2C);
}

static void test_frames(void)
{
    static const int ask_all[NW] = { 10, 20, 30 }, ask_none[NW] = { -1, -1, -1 };
    unsigned shown[NW], i, f, seq = 1;

    printf("without the plugin: one frame in four shows the clock\n");
    reset();
    game_frame(seq++, NW, ask_all, 3, 0, shown);
    memset(shown, 0, sizeof shown);
    for (f = 0; f < 30; f++)
        game_frame(seq++, NW, ask_all, 3, 0, shown);
    CHECK(shown[0] == 30 && shown[1] == 30 && shown[2] == 30,
          "unhooked: shown %u %u %u of 120 frames, want 30 (the first extra frame only)",
          shown[0], shown[1], shown[2]);

    printf("with it: every frame, at the percentage the control bar set last\n");
    reset();
    game_frame(seq++, NW, ask_all, 3, 1, shown);
    memset(shown, 0, sizeof shown);
    for (f = 0; f < 30; f++) {
        int ask[NW] = { (int)f, 50, 99 };
        game_frame(seq++, NW, ask, 3, 1, shown);
        CHECK(g_percent_drawn[0] == (int)f, "frame %u drew %d%%", f, g_percent_drawn[0]);
    }
    CHECK(shown[0] == 120 && shown[1] == 120 && shown[2] == 120,
          "hooked: shown %u %u %u of 120 frames", shown[0], shown[1], shown[2]);
    for (i = 0; i < NW; i++)
        CHECK(g_b[i].kind == 2, "window %u: request %u left for the game's next draw", i,
              g_b[i].kind);

    printf("any number of extra frames\n");
    memset(shown, 0, sizeof shown);
    for (f = 0; f < 8; f++)
        game_frame(seq++, NW, ask_all, f % 4, 1, shown);
    CHECK(shown[0] == 8 + 0 + 1 + 2 + 3 + 0 + 1 + 2 + 3, "shown %u", shown[0]);

    printf("production done: the game's next draw clears it, extra frames show none\n");
    memset(shown, 0, sizeof shown);
    game_frame(seq++, NW, ask_none, 3, 1, shown);
    CHECK(shown[0] == 1 && !g_b[0].kind, "last frame: shown %u (the game's draw), request %u",
          shown[0], g_b[0].kind);
    game_frame(seq++, NW, ask_none, 3, 1, shown);
    CHECK(shown[0] == 1, "after: shown %u", shown[0]);

    printf("a button an extra frame doesn't draw keeps its request\n");
    memset(shown, 0, sizeof shown);
    game_frame(seq++, 2, ask_all, 3, 1, shown);
    CHECK(shown[0] == 3 && shown[2] == 0 && g_b[2].kind == 2, "shown %u %u, request %u",
          shown[0], shown[2], g_b[2].kind);
    g_b[2].kind = 0;

    printf("drawn twice in one extra frame: once, as the game's draw\n");
    {
        unsigned twice;
        reset();
        g_b[0].kind = 2;
        twice = (unsigned)draw(&g_w[0], 1, 1) + (unsigned)draw(&g_w[0], 1, 1);
        CHECK(twice == 1 && pf_btn_extra_end(0) == 1 && g_b[0].kind == 2, "drawn %u, request %u",
              twice, g_b[0].kind);
    }

    printf("a request the draw didn't clear is left alone\n");
    reset();
    g_b[0].kind = 1;
    pf_btn_draw(&g_w[0], 1);
    CHECK(pf_btn_extra_end(0) == 1 && g_b[0].kind == 1, "request %u", g_b[0].kind);

    printf("the game's own draws are counted, not changed\n");
    {
        pf_btn_stats s;
        reset();
        g_b[0].kind = 2;
        CHECK(draw(&g_w[0], 0, 1) && !g_b[0].kind && pf_btn_extra_end(0) == 0 && !g_b[0].kind,
              "the game's draw was set back");
        pf_btn_get_stats(&s);
        CHECK(s.noted == 1 && s.asked == 0, "noted %u asked %u", s.noted, s.asked);
    }

    printf("no window, no data, unknown kind, off\n");
    reset();
    pf_btn_draw(0, 1);
    g_w[1].data = 0;
    pf_btn_draw(&g_w[1], 1);
    g_w[1].data = (unsigned char *)&g_b[1];
    g_b[0].kind = 3;
    pf_btn_draw(&g_w[0], 1);
    CHECK(pf_btn_extra_end(0) == 0, "noted without a window, data or known kind");
    g_b[0].kind = 0;
    pf_btn_init(0);
    g_b[0].kind = 2;
    draw(&g_w[0], 1, 1);
    CHECK(pf_btn_extra_end(0) == 0 && !g_b[0].kind, "set back while off");

    printf("a full table\n");
    {
        static window many[70];
        static button mb[70];
        pf_btn_init(0x2C);
        for (i = 0; i < 70; i++) {
            many[i].data = (unsigned char *)&mb[i];
            mb[i].kind = 1;
            pf_btn_draw(&many[i], 1);
            mb[i].kind = 0;
        }
        CHECK(pf_btn_extra_end(0) == 64, "set back other than 64");
        CHECK(mb[0].kind == 1 && mb[63].kind == 1 && !mb[64].kind,
              "the first 64 are set back: %u %u %u", mb[0].kind, mb[63].kind, mb[64].kind);
    }
}

static void test_requests(void)
{
    static const int ask_all[NW] = { 10, 20, 30 };
    unsigned shown[NW] = { 0 };
    pf_btn_stats s;

    printf("the control bar's requests\n");
    reset();
    /* Asked on all three, all drawn by the extra frames: not unseen. */
    game_frame(3, NW, ask_all, 2, 1, shown);
    pf_btn_get_stats(&s);
    CHECK(s.requests == 3 && s.asked == 6 && s.unseen == 0, "frame 3: %u %u %u", s.requests,
          s.asked, s.unseen);
    /* Window 2 isn't drawn by the extra frames: unseen once, however many. */
    game_frame(4, 2, ask_all, 3, 1, shown);
    pf_btn_get_stats(&s);
    CHECK(s.requests == 6 && s.unseen == 1, "frame 4: requests %u unseen %u", s.requests,
          s.unseen);
    /* No requests, no extra frames: nothing unseen. */
    g_b[2].kind = 0;
    game_frame(5, 2, (const int[NW]){ -1, -1, -1 }, 3, 1, shown);
    game_frame(6, 2, ask_all, 0, 1, shown);
    pf_btn_get_stats(&s);
    CHECK(s.unseen == 1, "unseen %u", s.unseen);
    pf_btn_request(0, 7);
    pf_btn_request(&g_w[1], 0);
    pf_btn_get_stats(&s);
    CHECK(s.requests == 9, "requests without a window or frame counted: %u", s.requests);
    pf_btn_init(0);
    pf_btn_request(&g_w[1], 7);
    pf_btn_get_stats(&s);
    CHECK(s.requests == 0, "counted while off");
}
/* A code section shaped like W3DGadgetPushButtonDraw (the real signature)
 * and its three draws, for pf_btn_find. */
static unsigned char g_code[0x4000];
static const unsigned g_disp = 0x100, g_draw_at[3] = { 0x1000, 0x1800, 0x2000 },
                      g_call_at[3] = { 0x1A, 0x68, 0x73 }, g_getter = 0x3F00;

static void put_rel(unsigned at, unsigned to)
{
    int rel = (int)to - (int)(at + 5);
    g_code[at] = 0xE8;
    memcpy(g_code + at + 1, &rel, 4);
}

static void build(void)
{
    static const unsigned char test_al[] = { 0x8A, 0x06, 0x84, 0xC0, 0x0F, 0x84, 0, 0, 0, 0,
                                             0x3C, 0x01, 0x75 };
    static const unsigned char test_cl[] = { 0x8A, 0x0E, 0x84, 0xC9, 0x0F, 0x84, 0, 0, 0, 0,
                                             0x80, 0xF9, 0x01, 0x75 };
    pf_pattern p;
    unsigned i;

    memset(g_code, 0xCC, sizeof g_code);
    CHECK(pf_parse_pattern(pf_sites[PF_BUTTON_DRAW].signature, &p), "signature");
    for (i = 0; i < p.len; i++)
        g_code[g_disp + i] = p.fixed[i] ? p.bytes[i] : 0;
    memcpy(g_code + g_getter, "\x8B\x41\x2C\xC3", 4);
    for (i = 0; i < 3; i++) {
        unsigned d = g_draw_at[i];
        put_rel(g_disp + g_call_at[i], d);
        /* mov ecx, edi | mov ecx, [ebp+8]; call getUserData; mov esi, eax; test; je */
        put_rel(d + 0x40, g_getter);
        memcpy(g_code + d + 0x45, "\x8B\xF0\x85\xF6\x0F\x84\0\0\0\0", 10);
        if (i < 2)
            memcpy(g_code + d + 0x100, test_al, sizeof test_al);
        else
            memcpy(g_code + d + 0x100, test_cl, sizeof test_cl);
        memcpy(g_code + d + 0x200, "\xC6\x06\x00\xE8\0\0\0\0", 8);
    }
}

static void test_find(void)
{
    pf_image img = { g_code, 0, sizeof g_code, 0 };
    unsigned off = 1;
    const char *why;

    printf("finding the clock draws\n");
    build();
    why = pf_btn_find(&img, &pf_sites[PF_BUTTON_DRAW], &off);
    CHECK(!why && off == 0x2C, "%s, +%X", why ? why : "ok", off);

    /* The check-like draw (likely the command bar's buttons) is checked as the
     * others are. */
    g_code[g_draw_at[0] + 0x202] = 0x01;
    why = pf_btn_find(&img, &pf_sites[PF_BUTTON_DRAW], &off);
    CHECK(why && !strcmp(why, "a button draw's clock request clear") && !off,
          "the check-like draw without a clear: %s", why ? why : "ok");
    build();
    memset(g_code + g_draw_at[1] + 0x100, 0xCC, 14);
    why = pf_btn_find(&img, &pf_sites[PF_BUTTON_DRAW], &off);
    CHECK(why && !strcmp(why, "a button draw's clock request test"),
          "the color draw without a test: %s", why ? why : "ok");
    build();
    memcpy(g_code + g_getter + 0x80, "\x8B\x41\x30\xC3", 4);
    put_rel(g_draw_at[2] + 0x40, g_getter + 0x80);
    why = pf_btn_find(&img, &pf_sites[PF_BUTTON_DRAW], &off);
    CHECK(why && !strcmp(why, "the button data offset (the draws differ)"),
          "draws reading different offsets: %s", why ? why : "ok");
    build();
    g_code[g_getter + 3] = 0x90;
    why = pf_btn_find(&img, &pf_sites[PF_BUTTON_DRAW], &off);
    CHECK(why && !strcmp(why, "the window's user data getter"), "a getter without ret: %s",
          why ? why : "ok");
    build();
    put_rel(g_disp + g_call_at[1], sizeof g_code + 0x100);
    why = pf_btn_find(&img, &pf_sites[PF_BUTTON_DRAW], &off);
    CHECK(why && !strcmp(why, "the push button draw's calls"), "a call out of the code: %s",
          why ? why : "ok");
    build();
    memcpy(g_code + 0x3000, g_code + g_disp, 0x80);
    why = pf_btn_find(&img, &pf_sites[PF_BUTTON_DRAW], &off);
    CHECK(why && !strcmp(why, "the push button draw"), "two dispatchers: %s", why ? why : "ok");
}

int main(void)
{
    test_frames();
    test_requests();
    test_find();
    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: button clock tests passed\n");
    return 0;
}
