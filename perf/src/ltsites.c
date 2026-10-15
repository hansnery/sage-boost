#include "ltsites.h"

/* From the scene light list's start to the render info's light environment
 * set: both list loops (the plugin repeats their decisions), the "lights
 * added" test, the default and infantry environments and Pre_Render_Update
 * with the camera's transform. Only call displacements are wildcards. */
static const char g_lists_sig[] =
    "8B 9E 94 00 00 00 EB 6F 8B 7B 0C 8D 47 F8 F7 DF 1B FF 23 F8 8B 07 8B CF FF 90 04 01 00 00 "
    "83 BF C4 00 00 00 00 F3 0F 10 00 F3 0F 11 45 B0 F3 0F 10 40 04 F3 0F 11 45 B4 F3 0F 10 40 08 "
    "F3 0F 11 45 B8 F3 0F 10 40 0C F3 0F 11 45 BC 75 13 8D 45 B0 50 8D 45 88 50 E8 ?? ?? ?? ?? 84 C0 "
    "59 59 74 13 57 8D 8D 60 FD FF FF E8 ?? ?? ?? ?? C7 45 AC 01 00 00 00 8B 5B 04 8D 86 90 00 00 00 "
    "3B D8 75 87 8B 9E 1C 01 00 00 E9 86 00 00 00 8B 7B 0C 8D 47 F8 F7 DF 1B FF 23 F8 80 BF 44 01 00 "
    "00 00 74 6E 8B 07 8B CF FF 90 04 01 00 00 83 BF C4 00 00 00 00 F3 0F 10 00 F3 0F 11 45 B0 F3 0F "
    "10 40 04 F3 0F 11 45 B4 F3 0F 10 40 08 F3 0F 11 45 B8 F3 0F 10 40 0C F3 0F 11 45 BC 75 13 8D 45 "
    "B0 50 8D 45 88 50 E8 ?? ?? ?? ?? 84 C0 59 59 74 21 8B 43 0C 8B C8 83 C0 F8 F7 D9 1B C9 23 C8";
/* The rest, right after it (a pattern holds at most 256 bytes). */
static const char g_lists_tail_sig[] =
    "51 8D 8D 60 FD FF FF E8 ?? ?? ?? ?? C7 45 AC 01 00 00 00 8B 5B 04 8D 86 18 01 00 00 3B D8 0F "
    "85 6C FF FF FF 83 7D AC 00 75 2A 80 7D F3 00 75 24 8B 5D 0C 8B 03 8B CB FF 90 C0 01 00 00 85 C0 8B 7D "
    "08 74 08 8D 86 B4 05 00 00 EB 2C 8D 86 64 01 00 00 EB 24 8B 7D 08 8B 1F 8B 03 8B CB FF 50 50 83 "
    "C3 18 53 8D 8D 60 FD FF FF E8 ?? ?? ?? ?? 8B 5D 0C 8D 85 60 FD FF FF 89 47 28";
#define LISTS_TAIL 0xFC
#define LISTS_AFTER 0x120   /* cmp dword [ebp-0x54], 0 */
#define LISTS_DEFAULT 0x12C /* mov ebx, [ebp+0xC]: the infantry or default environment */
#define CALL_SPHERES 0x56
#define CALL_ADD_STATIC 0x68
#define CALL_SPHERES_DYN 0xE3
#define CALL_ADD_DYN 0x103
#define CALL_PRU 0x166

/* The tinted global lights loop (as in the site table), and what its skip
 * lands on: the second material pass check at +0x122. */
static const char g_glights_sig[] =
    "F3 0F 11 45 E4 33 DB 39 9E 60 01 00 00 0F 8E ?? ?? ?? ?? 8B 45 D8 8D 3C 98";
#define GLIGHTS_SITE 5
static const char g_glights_skip_sig[] = "8D 8F 58 03 00 00 F3 0F 10 01 0F 2E 05";
#define GLIGHTS_SKIP 0x122
/* Reset and the global lights for an object without a drawable; its skip
 * lands on the light lists. */
static const char g_reset_nd_sig[] =
    "EB 03 8B 45 D8 89 45 D8 8D 45 C0 50 8D 45 88 50 8D 8D 60 FD FF FF E8 ?? ?? ?? ?? 33 DB 83 3D";
#define RESET_ND_SITE 8
#define RESET_ND_TO_LISTS 0x105
/* RenderObjClass::Get_Bounding_Sphere: unless the cached volumes are valid
 * (+0x10 bit 17), update them (vtable +0x1FC); return the sphere at +0x4C. */
static const char g_bsphere_sig[] =
    "56 8B F1 F6 46 12 02 75 08 8B 06 FF 90 FC 01 00 00 8D 46 4C 5E C3";
/* The point light setup's brightness test: cvtsi2ss xmm0, [global]. */
static const char g_threshold_sig[] = "F3 0F 2A 05 ?? ?? ?? ?? F3 0F 58 DA F3 0F 58 D9 0F 2F D8";
/* Set_Light_Environment's ambient: cmp [RenderStates + 0x8B*4], eax; then
 * Set_DX8_Render_State(D3DRS_AMBIENT = 0x8B, ...). D3DRS_LIGHTING (0x89)
 * sits two dwords before. */
static const char g_ambient_sig[] = "8B 45 FC 39 05 ?? ?? ?? ?? 74 11 50 68 8B 00 00 00 E8";
/* Set_Light_Environment's head: returns if the environment is the cached one
 * and its byte 0 is set; stores it; returns if it is NULL or while the FX
 * batches hold the lights (the lock byte); else writes the lights. */
static const char g_lenv_head_sig[] =
    "55 8B EC 81 EC 88 00 00 00 8B 45 08 39 05 ?? ?? ?? ?? 75 0D 85 C0 74 09 80 38 00 0F 85 ?? ?? "
    "?? ?? 85 C0 A3 ?? ?? ?? ?? 0F 84 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? 00 0F 85";
#define LENV_CMP 14
#define LENV_STORE 36
#define LENV_LOCK 48

static unsigned read_le32(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static int find_once(const pf_image *img, const char *sig, unsigned *rva)
{
    pf_pattern p;
    return pf_parse_pattern(sig, &p) && pf_scan(img, &p, rva) == 1;
}

static int matches_at(const pf_image *img, const char *sig, unsigned rva)
{
    pf_pattern p;
    return pf_parse_pattern(sig, &p) && rva + p.len <= img->text_rva + img->text_size &&
           pf_pattern_at(img->base + rva, &p);
}

/* The target of the E8 at rva, or 0 if it leaves .text. */
static unsigned call_target(const pf_image *img, unsigned rva)
{
    unsigned t = rva + 5 + read_le32(img->base + rva + 1);
    return t >= img->text_rva && t < img->text_rva + img->text_size ? t : 0;
}

/* A data address (VA operand) as a pointer into the image, or NULL if it
 * isn't an aligned address past the code section. */
static void *data_at(const pf_image *img, unsigned va)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)img->base;
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(img->base + dos->e_lfanew);
    unsigned rva = va - nt->OptionalHeader.ImageBase;
    if (rva & 3 || rva < img->text_rva + img->text_size || rva + 4 > nt->OptionalHeader.SizeOfImage)
        return 0;
    return img->base + rva;
}

/* The same for a byte. */
static void *byte_at(const pf_image *img, unsigned va)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)img->base;
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(img->base + dos->e_lfanew);
    unsigned rva = va - nt->OptionalHeader.ImageBase;
    if (rva < img->text_rva + img->text_size || rva >= nt->OptionalHeader.SizeOfImage)
        return 0;
    return img->base + rva;
}

const char *pf_lt_find(const pf_image *img, pf_lt_data *d)
{
    unsigned lists = 0, glights = 0, reset_nd = 0, bsphere = 0, thr = 0, amb = 0, lenv = 0;
    unsigned si, si_dyn, add, add_dyn, pru;
    unsigned char *ambient;

    d->glights_skip = d->lists = d->lists_after = d->lists_default = 0;
    d->add_light = d->pre_render_update = d->spheres_intersect = d->bsphere = 0;
    d->threshold = 0;
    d->lighting = 0;
    d->light_env = 0;
    d->light_lock = 0;
    if (!find_once(img, g_lists_sig, &lists) ||
        !matches_at(img, g_lists_tail_sig, lists + LISTS_TAIL))
        return "renderOneObject's light lists";
    si = call_target(img, lists + CALL_SPHERES);
    si_dyn = call_target(img, lists + CALL_SPHERES_DYN);
    add = call_target(img, lists + CALL_ADD_STATIC);
    add_dyn = call_target(img, lists + CALL_ADD_DYN);
    pru = call_target(img, lists + CALL_PRU);
    if (!si || si != si_dyn || !add || add != add_dyn || !pru)
        return "renderOneObject's light lists (their calls disagree)";
    if (!find_once(img, g_glights_sig, &glights) ||
        !matches_at(img, g_glights_skip_sig, glights + GLIGHTS_SITE + GLIGHTS_SKIP))
        return "renderOneObject's global lights loop";
    if (!find_once(img, g_reset_nd_sig, &reset_nd) ||
        reset_nd + RESET_ND_SITE + RESET_ND_TO_LISTS != lists)
        return "renderOneObject's light environment Reset (no drawable)";
    if (!find_once(img, g_bsphere_sig, &bsphere))
        return "RenderObjClass::Get_Bounding_Sphere";
    if (!find_once(img, g_threshold_sig, &thr) || !data_at(img, read_le32(img->base + thr + 4)))
        return "the point light setup's brightness test";
    if (!find_once(img, g_ambient_sig, &amb) ||
        !(ambient = data_at(img, read_le32(img->base + amb + 5))))
        return "DX8Wrapper's render state cache";
    if (!find_once(img, g_lenv_head_sig, &lenv) ||
        read_le32(img->base + lenv + LENV_CMP) != read_le32(img->base + lenv + LENV_STORE) ||
        !data_at(img, read_le32(img->base + lenv + LENV_CMP)) ||
        !byte_at(img, read_le32(img->base + lenv + LENV_LOCK)))
        return "DX8Wrapper::Set_Light_Environment's cached environment and lock";
    d->glights_skip = img->base + glights + GLIGHTS_SITE + GLIGHTS_SKIP;
    d->lists = img->base + lists;
    d->lists_after = img->base + lists + LISTS_AFTER;
    d->lists_default = img->base + lists + LISTS_DEFAULT;
    d->add_light = img->base + add;
    d->pre_render_update = img->base + pru;
    d->spheres_intersect = img->base + si;
    d->bsphere = img->base + bsphere;
    d->threshold = (const volatile int *)data_at(img, read_le32(img->base + thr + 4));
    d->lighting = (const volatile unsigned *)(ambient - 8);
    d->light_env = (const unsigned char *const volatile *)data_at(
        img, read_le32(img->base + lenv + LENV_CMP));
    d->light_lock = (const volatile unsigned char *)byte_at(
        img, read_le32(img->base + lenv + LENV_LOCK));
    return 0;
}
