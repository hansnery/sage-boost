#include "uistate.h"

/* Two callers of TheInGameUI->placeBuildAvailable(0, 0) (vtable +0xDC); both
 * must name the same global. */
static const char *const g_ui_sigs[2] = {
    "8B 0D ?? ?? ?? ?? 85 C9 74 0C 8B 01 6A 00 6A 00 FF 90 DC 00 00 00 33 C0",
    "8B 0D ?? ?? ?? ?? 8B 01 6A 00 6A 00 FF 90 DC 00 00 00 C2 04 00",
};
/* placeBuildAvailable: `lea edi, [esi+pending]`, then the recursive
 * placeBuildAvailable(0, 0) when a build is already pending. */
static const char g_pending_sig[] =
    "8D BE ?? ?? ?? ?? 33 D2 39 17 74 12 3B DA 74 0E 8B 06 52 52 8B CE FF 90 DC 00 00 00";
/* setScrolling(bool): `cmp [esi+scrolling], bl` on entry. */
static const char g_scroll_sig[] = "53 8A 5C 24 08 56 8B F1 38 9E ?? ?? ?? ?? 74 34 84 DB 74 16 8B 0D";

#define PF_UI_MAX_FIELD 0x2000u

static unsigned read_le32(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

/* The 32-bit operand at `at` bytes into the single match of sig; 0 if the
 * signature doesn't match exactly once. */
static int operand(const pf_image *img, const char *sig, unsigned at, unsigned *value)
{
    pf_pattern p;
    unsigned rva = 0;

    if (!pf_parse_pattern(sig, &p) || pf_scan(img, &p, &rva) != 1)
        return 0;
    *value = read_le32(img->base + rva + at);
    return 1;
}

const char *pf_ui_find(const pf_image *img, pf_ui_state *out)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)img->base;
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(img->base + dos->e_lfanew);
    unsigned va0, va1, rva;

    out->ui = 0;
    out->pending_off = out->scroll_off = 0;
    if (!operand(img, g_ui_sigs[0], 2, &va0) || !operand(img, g_ui_sigs[1], 2, &va1))
        return "TheInGameUI signature";
    /* The operand is a VA as loaded; ImageBase in the loaded headers is the
     * actual base, so this is the global's RVA. */
    rva = va0 - nt->OptionalHeader.ImageBase;
    if (va0 != va1 || rva & 3 || rva < img->text_rva + img->text_size ||
        rva + 4 > nt->OptionalHeader.SizeOfImage)
        return "TheInGameUI address";
    if (!operand(img, g_pending_sig, 2, &out->pending_off) || out->pending_off & 3 ||
        out->pending_off >= PF_UI_MAX_FIELD)
        return "the pending build field";
    if (!operand(img, g_scroll_sig, 10, &out->scroll_off) || out->scroll_off >= PF_UI_MAX_FIELD) {
        out->pending_off = 0;
        return "the scrolling field";
    }
    out->ui = (void *const *)(img->base + rva);
    return 0;
}

static const unsigned char *ui_object(const pf_ui_state *s)
{
    return s->ui ? *(const unsigned char *const volatile *)s->ui : 0;
}

int pf_ui_placing(const pf_ui_state *s)
{
    const unsigned char *ui = ui_object(s);
    return ui && *(void *const volatile *)(ui + s->pending_off) != 0;
}

int pf_ui_scrolling(const pf_ui_state *s)
{
    const unsigned char *ui = ui_object(s);
    return ui && *(const volatile unsigned char *)(ui + s->scroll_off) != 0;
}
