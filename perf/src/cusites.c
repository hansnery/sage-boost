#include "cusites.h"

#include "fpsites.h"
#include "passflag.h"
#include "sites.h"
#include "spsites.h"

/* After the shadow-map and reflection blocks: the GlobalData tests that send
 * the frame to its no-render path (jne rel32, both to the same place). */
static const char g_no_render_sig[] =
    "38 98 36 0D 00 00 0F 85 ?? ?? ?? ?? 38 98 6F 0B 00 00 0F 85";
/* The no-render path: if (byte) byte = 0. */
static const char g_no_render_target[] = "38 1D ?? ?? ?? ?? 74 06 88 1D";
/* scene->pass mode = 0 before the views are drawn. */
static const char g_scene_sig[] = "A1 ?? ?? ?? ?? 89 98 EC 07 00 00";
/* After the UI: TheInGameUI's animation update, then another subsystem's draw. */
static const char g_ui_anim_call[] =
    "8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 3B CB 74 05 8B 01 FF 50 30 39 5E 38";
/* Its body: the list it walks is at +0x8D0, the string before it at +0x8D4. */
static const char g_ui_anim_body[] =
    "55 8B EC 83 EC 0C 53 56 8B D1 57 8D 8A ?? ?? ?? ?? 89 55 FC E8 ?? ?? ?? ?? 8D 9A ?? ?? ?? ?? "
    "8B 03 8B 38 3B F8";
/* Customized_Render's update loop, every offset fixed (cusites.h). */
static const char g_update_loop[] =
    "8B 5F 7C 8D 47 78 3B D8 0F 84 ?? ?? ?? ?? 8B 73 0C 8D 46 F8 F7 DE 1B F6 23 F0 8B 06 8B CE "
    "FF 50 0C 83 F8 04 75 09 83 7D F4 00 89 75 FC 75 23 80 3D ?? ?? ?? ?? 00 75 1A 80 3D ?? ?? ?? "
    "?? 00 75 11 8B 4B 0C 8D 41 F8 F7 D9 1B C9 23 C8 8B 01 FF 50 34 8B 5B 04 8D 47 78 3B D8 75 B2";
/* The limiter: if (limit_on) { now = timeGetTime(); period = ...m_maxFPS... */
static const char g_limit_sig[] = "38 1D ?? ?? ?? ?? 74 6A FF 15 ?? ?? ?? ?? DB 46 0C";
/* W3DDisplay::draw: freezeTime = !TheGameClient->running. */
static const char g_running_sig[] = "A1 ?? ?? ?? ?? 80 B8 ?? ?? ?? ?? 00 8B 07 8B CF 0F 94 45 F3";
/* GameLogic::update, on a tick: if (this->ready() && !held) ++m_frame,
 * ready() being three byte tests of `this`. */
static const char g_logic_frame_sig[] = "8B CE E8 ?? ?? ?? ?? 84 C0 74 07 84 DB 75 07 FF 46 ??";
static const char g_logic_ready_body[] =
    "33 C0 38 41 ?? 74 ?? 38 81 ?? ?? ?? ?? 75 ?? 38 81 ?? ?? ?? ?? 75 ?? 40 C3";
/* GameEngine::update: at the 6th frame of a tick, frames per tick =
 * BfmeClientFramesPerSecond / BfmeLogicFramesPerSecond. */
static const char g_logic_fps_sig[] =
    "8B 4E 34 83 F9 06 75 19 80 7E 40 00 74 13 A1 ?? ?? ?? ?? 99 F7 3D ?? ?? ?? ??";

static unsigned read_le32(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static unsigned image_base(const pf_image *img)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)img->base;
    return ((const IMAGE_NT_HEADERS32 *)(img->base + dos->e_lfanew))->OptionalHeader.ImageBase;
}

static unsigned image_size(const pf_image *img)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)img->base;
    return ((const IMAGE_NT_HEADERS32 *)(img->base + dos->e_lfanew))->OptionalHeader.SizeOfImage;
}

/* A data address (VA operand) as a pointer into the image, or NULL if it isn't
 * past the code section (aligned unless `byte`). */
static unsigned char *data_va(const pf_image *img, unsigned va, int byte)
{
    unsigned rva = va - image_base(img);
    if ((!byte && rva & 3) || rva < img->text_rva + img->text_size || rva + 4 > image_size(img))
        return 0;
    return img->base + rva;
}

static int find_once(const pf_image *img, const char *sig, unsigned *rva)
{
    pf_pattern p;
    return pf_parse_pattern(sig, &p) && pf_scan(img, &p, rva) == 1;
}

/* The first match of sig in [from, to), or 0. */
static unsigned find_in(const pf_image *img, unsigned from, unsigned to, const char *sig)
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

static int matches_at(const pf_image *img, unsigned rva, const char *sig)
{
    pf_pattern p;
    return pf_parse_pattern(sig, &p) && rva >= img->text_rva &&
           rva + p.len <= img->text_rva + img->text_size && pf_pattern_at(img->base + rva, &p);
}

static unsigned rel32_target(const pf_image *img, unsigned at_operand)
{
    return at_operand + 4 + read_le32(img->base + at_operand);
}

static void zero(pf_cu_sites *out)
{
    unsigned char *o = (unsigned char *)out;
    unsigned i;
    for (i = 0; i < sizeof *out; i++)
        o[i] = 0;
}

const char *pf_cu_find(const pf_image *img, pf_cu_sites *out)
{
    pf_fps_sites fs;
    pf_sp_data sp;
    const volatile unsigned char *shadow_flag = 0;
    unsigned site, shadow, cust, wait, views, at, t1, t2, target, va, body;
    const char *why;

    zero(out);
    if ((why = pf_fps_find(img, &fs)))
        return why;
    if ((why = pf_ui_find(img, &out->ui)))
        return why;
    if ((why = pf_shadow_flag_find(img, &shadow_flag)))
        return why;
    if (!find_once(img, pf_sites[PF_CU_RENDER].signature, &site) ||
        !find_once(img, pf_sites[PF_SHADOW].signature, &shadow) ||
        !find_once(img, pf_sites[PF_CUST].signature, &cust) ||
        !find_once(img, pf_sites[PF_FPS_WAIT].signature, &wait) ||
        !find_once(img, pf_sites[PF_VIEWS].signature, &views))
        return "the catch-up sites";
    site += pf_sites[PF_CU_RENDER].site_offset;

    /* The no-render path, after the site in the same function. */
    at = find_in(img, site, site + 0x200, g_no_render_sig);
    if (!at)
        return "the frame's no-render path";
    t1 = rel32_target(img, at + 8);
    t2 = rel32_target(img, at + 20);
    if (t1 != t2 || t1 <= at || t1 > site + 0x600 || !matches_at(img, t1, g_no_render_target) ||
        read_le32(img->base + t1 + 2) != read_le32(img->base + t1 + 10))
        return "the frame's no-render path (its branches disagree)";
    target = t1;

    /* The scene: pushed for UpdateShadowMap, its pass mode cleared before the views. */
    at = find_in(img, site, site + 0x200, g_scene_sig);
    va = at ? read_le32(img->base + at + 1) : 0;
    if (!at || va != read_le32(img->base + shadow + 2) || !data_va(img, va, 0))
        return "the 3D scene";
    out->scene = (unsigned char *const *)data_va(img, va, 0);

    /* The scene's update loop, in Customized_Render. */
    at = find_in(img, cust, cust + 0x200, g_update_loop);
    if (!at || !data_va(img, read_le32(img->base + at + 49), 1) ||
        data_va(img, read_le32(img->base + at + 58), 1) != (const unsigned char *)shadow_flag)
        return "the scene's update loop";
    out->proj_override = data_va(img, read_le32(img->base + at + 49), 1);
    out->shadow_flag = shadow_flag;

    /* InGameUI's animation update, called on TheInGameUI after the UI's draw. */
    at = find_in(img, site, site + 0x400, g_ui_anim_call);
    if (!at || data_va(img, read_le32(img->base + at + 2), 0) != (unsigned char *)out->ui.ui)
        return "InGameUI's animation update";
    body = rel32_target(img, at + 7);
    if (!matches_at(img, body, g_ui_anim_body) ||
        read_le32(img->base + body + 13) != read_le32(img->base + body + 27) + 4)
        return "InGameUI's animation update (its body differs)";
    out->ui_anim = img->base + body;

    /* The limiter: frame start and clock from the wait, its flag just before. */
    at = find_in(img, wait > 0x80 ? wait - 0x80 : 0, wait, g_limit_sig);
    if (!at || read_le32(img->base + at + 10) != read_le32(img->base + wait + 9) ||
        read_le32(img->base + wait + 17) != read_le32(img->base + wait + 30) ||
        !data_va(img, read_le32(img->base + at + 2), 1) ||
        !data_va(img, read_le32(img->base + wait + 9), 0) ||
        !data_va(img, read_le32(img->base + wait + 17), 0))
        return "the frame limiter's state";
    out->limit_on = data_va(img, read_le32(img->base + at + 2), 1);
    out->time_import = (void *const *)data_va(img, read_le32(img->base + wait + 9), 0);
    out->frame_start = (const volatile unsigned *)data_va(img, read_le32(img->base + wait + 17), 0);

    /* TheGameClient's "time running" byte, as the draw tests it. */
    body = (unsigned)((unsigned char *)fs.draw - img->base);
    at = find_in(img, body, body + 0x100, g_running_sig);
    if (!at || data_va(img, read_le32(img->base + at + 1), 0) != (unsigned char *)fs.client ||
        read_le32(img->base + at + 7) >= 0x1000)
        return "the client's time-running flag";
    out->client = (unsigned char *const *)fs.client;
    out->client_running = read_le32(img->base + at + 7);

    /* TheGameLogic's paused byte (+0x125 is fixed in the views site's bytes). */
    if (data_va(img, read_le32(img->base + views + 1), 0) != (unsigned char *)fs.logic)
        return "TheGameLogic (the signatures disagree)";
    out->logic = (unsigned char *const *)fs.logic;
    out->logic_paused = 0x125;
    out->is_in_game = fs.is_in_game;
    /* The logic frame counter, stepped near the start of GameLogic::update,
     * and the logic rate GameEngine::update divides by. */
    if (find_once(img, pf_sites[PF_LOGIC].signature, &at) &&
        (t1 = find_in(img, at, at + 0x100, g_logic_frame_sig)) &&
        matches_at(img, rel32_target(img, t1 + 3), g_logic_ready_body) &&
        img->base[t1 + 17] && img->base[t1 + 17] < 0x80 && !(img->base[t1 + 17] & 3) &&
        find_once(img, pf_sites[PF_FRAME].signature, &at) &&
        (t2 = find_in(img, at, at + 0x100, g_logic_fps_sig)) &&
        data_va(img, read_le32(img->base + t2 + 22), 0)) {
        out->logic_frame = img->base[t1 + 17];
        out->logic_fps = (const volatile unsigned *)data_va(img, read_le32(img->base + t2 + 22), 0);
    }
    out->screenshots = fs.screenshots;
    out->no_render = img->base + target;
    /* The device is only needed by the notice. */
    if (!pf_sp_find(img, &sp) || sp.device)
        out->device = sp.device;
    return 0;
}
