#include "spsites.h"

/* UpdateShadowMap opens with TheWritableGlobalData->m_useShadowMap (+0x62);
 * flushOccludedObjectsIntoStencil tests m_useShadowVolumes (+0x60) on the same
 * global. */
static const char g_gd_map_sig[] =
    "81 EC 5C 01 00 00 A1 ?? ?? ?? ?? 53 56 33 DB 38 58 62 57 8B F1 0F 84";
static const char g_gd_volumes_sig[] = "89 42 08 8B 15 ?? ?? ?? ?? 80 7A 60 00 74";
/* RTS3DScene::Flush: device->SetVertexShader(0) (vtable +0x170). */
static const char g_device_sig[] = "E8 ?? ?? ?? ?? A1 ?? ?? ?? ?? 8B 08 53 50 FF 91 70 01 00 00 FF 05";
/* The particle manager's render: the +0xA8 gate, then the smudge counter and
 * TheSmudgeManager +0x20 zeroed. */
static const char g_render_sig[] =
    "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 EC 78 53 57 8B F9 8D 87 A8 00 00 00 33 DB 38 18 89 7D EC 0F "
    "84 ?? ?? ?? ?? 88 18 A1 ?? ?? ?? ?? 3B C3 89 1D ?? ?? ?? ?? 74 03 89 58 20";
/* Its smudge branch: TheSmudgeManager present, its +4 not 1, GlobalData +0x25. */
static const char g_smudge_sig[] =
    "A1 ?? ?? ?? ?? 3B C3 0F 84 ?? ?? ?? ?? 83 78 04 01 0F 84 ?? ?? ?? ?? A1 ?? ?? ?? ?? 38 58 25 "
    "0F 84";
/* mov ecx, [TheParticleSystemManager]; test; jz; push [esp+4]; call [eax+0x40]; ret */
static const char g_forwarder_sig[] = "8B 0D ?? ?? ?? ?? 85 C9 74 09 FF 74 24 04 8B 01 FF 50 40 C3";
/* FXShaderRenderer::PerformRendering from the next group's scan to its setup
 * call: ESI walks the 32-byte records from EBX (the group's first) and
 * stores each in [EBP+8]; a record stays in the group while the batch test
 * holds and its method count and methods equal the first's. EBX and [EBP+8]
 * (the first record past the group) are then left alone up to the setup. */
static const char g_scan_sig[] =
    "8B 5D E0 83 C6 20 3B 75 0C 89 75 08 74 46 8B FB 2B FE 8B 06 8B 0B 50 E8 ?? ?? ?? ?? 84 C0 74 "
    "34 8B 43 04 3B 46 04 75 2C 8D 48 FF 85 C9 7C 17 8D 44 8E 08 8B 14 07 3B 10 75 08 49 83 E8 04 "
    "85 C9 7D F1 85 C9 7D 0E 83 C6 20 83 EF 20 3B 75 0C 89 75 08 75 BE 8B 43 04 85 C0 0F 8E ?? ?? "
    "?? ?? 8D 7B 08 50 57 89 7D D0 E8 ?? ?? ?? ?? 8B 37 33 C0 3B F0 59 59 89 75 CC 74 03 FF 46 04 "
    "3B F0 89 45 FC 75 09 83 4D FC FF E9 ?? ?? ?? ?? 50 50 68 ?? ?? ?? ?? 8D 8D 8C FD FF FF E8 ?? "
    "?? ?? ?? 8B 0B C6 45 FC 01 E8";

static unsigned read_le32(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static int find_once(const pf_image *img, const char *sig, unsigned *rva)
{
    pf_pattern p;
    return pf_parse_pattern(sig, &p) && pf_scan(img, &p, rva) == 1;
}

/* The data address (a VA operand) as a pointer into the image, or NULL if it
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

const char *pf_sp_find(const pf_image *img, pf_sp_data *d)
{
    unsigned a = 0, b = 0, r = 0, s = 0;
    unsigned gd, smudge;

    d->gd = 0;
    d->device = 0;
    d->particle_render = 0;
    d->smudge = 0;
    d->smudge_count = 0;
    if (!find_once(img, g_scan_sig, &d->batch_scan))
        d->batch_scan = 0;
    if (!find_once(img, g_gd_map_sig, &a) || !find_once(img, g_gd_volumes_sig, &b))
        return "TheWritableGlobalData";
    gd = read_le32(img->base + a + 7);
    if (gd != read_le32(img->base + b + 5) || !data_at(img, gd))
        return "TheWritableGlobalData (the signatures disagree)";
    d->gd = (const unsigned char *const *)data_at(img, gd);
    if (!find_once(img, g_device_sig, &a) || !data_at(img, read_le32(img->base + a + 6)))
        return "the D3D device";
    d->device = (void *const *)data_at(img, read_le32(img->base + a + 6));
    if (!find_once(img, g_render_sig, &r) || !find_once(img, g_smudge_sig, &s))
        return "the particle render";
    smudge = read_le32(img->base + r + 39);
    if (smudge != read_le32(img->base + s + 1) || read_le32(img->base + s + 24) != gd ||
        !data_at(img, smudge) || !data_at(img, read_le32(img->base + r + 47)) || s <= r ||
        s > r + 0x400)
        return "the particle render (its globals disagree)";
    d->particle_render = img->base + r;
    d->smudge = (unsigned char *const *)data_at(img, smudge);
    d->smudge_count = (volatile unsigned *)data_at(img, read_le32(img->base + r + 47));
    return 0;
}

unsigned char *const *pf_sp_forwarder(const pf_image *img, const unsigned char *callee)
{
    pf_pattern p;
    if (!callee || !pf_parse_pattern(g_forwarder_sig, &p) || !pf_pattern_at(callee, &p))
        return 0;
    return (unsigned char *const *)data_at(img, read_le32(callee + 2));
}
