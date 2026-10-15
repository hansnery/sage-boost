#include "passflag.h"

/* UpdateShadowMap: `mov byte [flag], 1` before it sets the shadow map as the
 * render target and draws the scene, `mov [flag], bl` (0) after; and
 * MeshClass::Render's `cmp byte [flag], 0` that keeps non-FX meshes out of
 * the shadow map. All three must name the same byte. */
static const struct {
    const char *sig;
    unsigned at;
} g_sigs[3] = {
    { "8D 45 F0 50 8D 4E 14 C6 05 ?? ?? ?? ?? 01 E8 ?? ?? ?? ?? FF 76 1C 8B 00 50", 9 },
    { "8D 8D 98 FE FF FF 88 1D ?? ?? ?? ?? E8", 8 },
    { "80 3D ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? 00 74 63 85 C0", 2 },
};

static unsigned read_le32(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

const char *pf_shadow_flag_find(const pf_image *img, const volatile unsigned char **flag)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)img->base;
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(img->base + dos->e_lfanew);
    unsigned i, va = 0, rva;

    *flag = 0;
    for (i = 0; i < 3; i++) {
        pf_pattern p;
        unsigned at = 0, v;
        if (!pf_parse_pattern(g_sigs[i].sig, &p) || pf_scan(img, &p, &at) != 1)
            return "a shadow-map flag signature";
        v = read_le32(img->base + at + g_sigs[i].at);
        if (i && v != va)
            return "the shadow-map flag (the signatures disagree)";
        va = v;
    }
    rva = va - nt->OptionalHeader.ImageBase;
    if (rva < img->text_rva + img->text_size || rva >= nt->OptionalHeader.SizeOfImage)
        return "the shadow-map flag address";
    *flag = img->base + rva;
    return 0;
}
