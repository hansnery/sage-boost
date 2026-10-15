#include "btnclock.h"

/* 0 none, 1 the clock, 2 the remaining-time clock; the percentage at +4 and
 * the color at +8 stay set after the draw. */
#define PF_BTN_NONE 0
#define PF_BTN_MAX_KIND 2
#define PF_BTN_FUNC_SIZE 0x800u
#define PF_BTN_MAX 64

/* W3DGadgetPushButtonDraw's calls to its draws, from its start: check-like,
 * color, image. */
static const unsigned g_draw_calls[3] = { 0x1A, 0x68, 0x73 };

/* A request the current extra frame's draw found set (and clears). */
typedef struct {
    const void *window;
    unsigned char *data;
    unsigned char kind;
} btn_rec;

static unsigned g_off, g_n, g_req_seq, g_req_n, g_unseen_seq;
static btn_rec g_btn[PF_BTN_MAX];
/* The windows the control bar asked a clock for in game frame g_req_seq;
 * only compared, never read through. */
static const void *g_req[PF_BTN_MAX];
static pf_btn_stats g_stats;

static unsigned read_le32(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static int in_text(const pf_image *img, unsigned rva, unsigned len)
{
    return rva >= img->text_rva && rva + len <= img->text_rva + img->text_size;
}

/* The first match of sig in [from, to), or 0. */
static unsigned find_from(const pf_image *img, unsigned from, unsigned to, const char *sig)
{
    pf_pattern p;
    unsigned end = img->text_rva + img->text_size;
    if (!pf_parse_pattern(sig, &p))
        return 0;
    if (to > end)
        to = end;
    for (; from + p.len <= to; from++)
        if (pf_pattern_at(img->base + from, &p))
            return from;
    return 0;
}

/* One of the three draws: the button data's offset in the window, or 0. */
static const char *check_draw(const pf_image *img, unsigned rva, unsigned *off)
{
    pf_pattern p;
    unsigned end = rva + PF_BTN_FUNC_SIZE, at, get, test, test2;

    /* call getUserData; mov esi, eax; test esi, esi; je */
    at = find_from(img, rva, end, "E8 ?? ?? ?? ?? 8B F0 85 F6 0F 84");
    if (!at)
        return "a button draw's data read";
    get = at + 5 + read_le32(img->base + at + 1);
    /* mov eax, [ecx+off]; ret */
    if (!in_text(img, get, 4) || !pf_parse_pattern("8B 41 ?? C3", &p) ||
        !pf_pattern_at(img->base + get, &p))
        return "the window's user data getter";
    /* mov al/cl, [esi]; test; je; cmp al/cl, 1; jne */
    test = find_from(img, at, end, "8A 06 84 C0 0F 84 ?? ?? ?? ?? 3C 01 75");
    test2 = find_from(img, at, end, "8A 0E 84 C9 0F 84 ?? ?? ?? ?? 80 F9 01 75");
    if (!test || (test2 && test2 < test))
        test = test2;
    if (!test)
        return "a button draw's clock request test";
    /* mov byte [esi], 0; call setUserData */
    if (!find_from(img, test, end, "C6 06 00 E8"))
        return "a button draw's clock request clear";
    *off = img->base[get + 2];
    return 0;
}

const char *pf_btn_find(const pf_image *img, const pf_site *site, unsigned *data_off)
{
    pf_pattern p;
    unsigned rva = 0, i, off, first = 0;
    const char *why;

    *data_off = 0;
    if (!pf_parse_pattern(site->signature, &p) || pf_scan(img, &p, &rva) != 1)
        return "the push button draw";
    rva += site->site_offset;
    for (i = 0; i < 3; i++) {
        unsigned call = rva + g_draw_calls[i], draw;
        if (call + 5 > rva + p.len || img->base[call] != 0xE8)
            return "the push button draw's calls";
        draw = call + 5 + read_le32(img->base + call + 1);
        if (!in_text(img, draw, 1))
            return "the push button draw's calls";
        off = 0;
        why = check_draw(img, draw, &off);
        if (why)
            return why;
        if (i && off != first)
            return "the button data offset (the draws differ)";
        first = off;
    }
    *data_off = first;
    return 0;
}

void pf_btn_init(unsigned data_off)
{
    g_off = data_off;
    g_n = g_req_seq = g_req_n = g_unseen_seq = 0;
    g_stats.noted = g_stats.asked = g_stats.requests = g_stats.unseen = 0;
}

static unsigned char *data_of(const void *window)
{
    return *(unsigned char *const *)((const unsigned char *)window + g_off);
}

void pf_btn_draw(const void *window, int extra)
{
    unsigned char *data;
    unsigned i;

    if (!g_off || !window)
        return;
    data = data_of(window);
    if (!data || *data == PF_BTN_NONE || *data > PF_BTN_MAX_KIND)
        return;
    if (!extra) {
        g_stats.noted++;
        return;
    }
    for (i = 0; i < g_n && g_btn[i].window != window; i++)
        ;
    if (i < g_n || g_n == PF_BTN_MAX)
        return;
    g_btn[g_n].window = window;
    g_btn[g_n].data = data;
    g_btn[g_n].kind = *data;
    g_n++;
}

void pf_btn_request(const void *window, unsigned seq)
{
    unsigned i;
    if (!g_off || !window || !seq)
        return;
    if (seq != g_req_seq) {
        g_req_seq = seq;
        g_req_n = 0;
    }
    g_stats.requests++;
    for (i = 0; i < g_req_n && g_req[i] != window; i++)
        ;
    if (i == g_req_n && g_req_n < PF_BTN_MAX)
        g_req[g_req_n++] = window;
}

/* A window of this frame's requests the extra frame drew no clock on. */
static int request_unseen(void)
{
    unsigned i, j;
    for (i = 0; i < g_req_n; i++) {
        for (j = 0; j < g_n && g_btn[j].window != g_req[i]; j++)
            ;
        if (j == g_n)
            return 1;
    }
    return 0;
}

unsigned pf_btn_extra_end(unsigned seq)
{
    unsigned i, drawn = g_n;
    for (i = 0; i < g_n; i++) {
        btn_rec *b = &g_btn[i];
        if (*b->data == PF_BTN_NONE)
            *b->data = b->kind;
    }
    g_stats.asked += drawn;
    if (seq && seq == g_req_seq && seq != g_unseen_seq && request_unseen()) {
        g_unseen_seq = seq;
        g_stats.unseen++;
    }
    g_n = 0;
    return drawn;
}

void pf_btn_get_stats(pf_btn_stats *s)
{
    *s = g_stats;
}