#include "fpsites.h"

static unsigned read_le32(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

typedef struct {
    const pf_image *img;
    unsigned image_base, image_size;
} finder;

/* RVA of the single match of sig, or 0 if it doesn't match exactly once. */
static unsigned match(const finder *f, const char *sig)
{
    pf_pattern p;
    unsigned rva = 0;
    if (!pf_parse_pattern(sig, &p) || pf_scan(f->img, &p, &rva) != 1)
        return 0;
    return rva;
}

/* The absolute address at rva+at as a pointer into the loaded image, or NULL
 * if it isn't a 4-aligned address in a data section. */
static void *data_at(const finder *f, unsigned rva, unsigned at)
{
    unsigned target = read_le32(f->img->base + rva + at) - f->image_base;
    if (target & 3 || target < f->img->text_rva + f->img->text_size || target + 4 > f->image_size)
        return 0;
    return f->img->base + target;
}

/* The same for a byte flag (no alignment). */
static void *byte_at(const finder *f, unsigned rva, unsigned at)
{
    unsigned target = read_le32(f->img->base + rva + at) - f->image_base;
    if (target < f->img->text_rva + f->img->text_size || target >= f->image_size)
        return 0;
    return f->img->base + target;
}

/* The target RVA of the E8 at rva+at, 0 if outside .text. */
static unsigned call_at(const finder *f, unsigned rva, unsigned at)
{
    unsigned target = rva + at + 5 + read_le32(f->img->base + rva + at + 1);
    if (target < f->img->text_rva || target >= f->img->text_rva + f->img->text_size)
        return 0;
    return target;
}

const char *pf_fps_find(const pf_image *img, pf_fps_sites *out)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)img->base;
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(img->base + dos->e_lfanew);
    finder f;
    unsigned r, draw, sync, crc;
    unsigned char *o = (unsigned char *)out;
    unsigned i;

    for (i = 0; i < sizeof *out; i++)
        o[i] = 0;
    f.img = img;
    f.image_base = nt->OptionalHeader.ImageBase;
    f.image_size = nt->OptionalHeader.SizeOfImage;

    /* The loading screen loop: TheDisplay->draw(); Sleep(100). */
    r = match(&f, "8B 0D ?? ?? ?? ?? 8B 01 FF 50 30 6A 64 FF 15");
    if (!r || !(out->display = data_at(&f, r, 2)))
        return "TheDisplay";
    /* W3DDisplay::draw: EH prolog, then `if (hwnd && IsIconic(hwnd)) return`. */
    draw = match(&f, "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 EC 30 A1 ?? ?? ?? ?? 85 C0 57 8B F9 89 7D "
                     "EC 74 0F 50 FF 15");
    if (!draw || !(out->hwnd = data_at(&f, draw, 14)) ||
        !(out->is_iconic = data_at(&f, draw, 31)))
        return "W3DDisplay::draw";
    out->draw = img->base + draw;
    out->is_iconic = *(void *const *)out->is_iconic;
    r = match(&f, "8B 0D ?? ?? ?? ?? 89 45 E4 8B 01 FF 50 7C 39 05 ?? ?? ?? ?? 76 10");
    if (!r || r < draw || r > draw + 0x200 || !(out->client = data_at(&f, r, 2)) ||
        !(out->last_client_frame = data_at(&f, r, 16)))
        return "draw's client frame";
    /* sync_acc += frame length * client frames elapsed; WW3D::Sync(sync_acc). */
    r = match(&f, "A1 ?? ?? ?? ?? 0F AF C6 01 05 ?? ?? ?? ?? FF 35 ?? ?? ?? ?? E8");
    if (!r || r < draw || r > draw + 0x200 || !(out->sync_acc = data_at(&f, r, 10)) ||
        data_at(&f, r, 16) != (void *)out->sync_acc || !(sync = call_at(&f, r, 20)))
        return "draw's sync time";
    /* WW3D::Sync(t): previous = current; current = t. */
    if (match(&f, "A1 ?? ?? ?? ?? 8B 4C 24 04 A3 ?? ?? ?? ?? 89 0D ?? ?? ?? ?? C3") != sync ||
        !(out->sync_time = data_at(&f, sync, 1)) ||
        !(out->prev_sync_time = data_at(&f, sync, 10)) ||
        data_at(&f, sync, 16) != (void *)out->sync_time)
        return "WW3D::Sync";
    r = match(&f, "8B 4D E8 2B 0D ?? ?? ?? ?? 83 F9 1D 7D 14");
    if (!r || r < draw || r > draw + 0x300 || !(out->last_draw_ms = data_at(&f, r, 5)))
        return "draw's frame-limit clock";
    r = match(&f, "FF 0D ?? ?? ?? ?? 83 3D ?? ?? ?? ?? 01 0F 8F");
    if (!r || r < draw || r > draw + 0x300 || !(out->skip_count = data_at(&f, r, 2)) ||
        data_at(&f, r, 8) != (void *)out->skip_count)
        return "draw's frame-skip count";
    r = match(&f, "F6 05 ?? ?? ?? ?? 01 59 75 12 83 0D");
    if (!r || r < draw || r > draw + 0x300 || !(out->init_flags = data_at(&f, r, 2)))
        return "draw's init flags";
    r = match(&f, "83 3D ?? ?? ?? ?? 00 0F 8E ?? ?? ?? ?? 83 B8 A0 0E 00 00 00");
    if (!r || r < draw || r > draw + 0x300 || !(out->screenshots = data_at(&f, r, 2)))
        return "draw's screenshot count";
    /* GetGameLogicRandomValueReal: the only one of the three Real variants that
     * jumps 0x5F on an empty range. */
    r = match(&f, "55 8B EC 51 F3 0F 10 45 0C F3 0F 5C 45 08 0F 57 C9 0F 2F C8 F3 0F 11 45 FC 73 "
                  "5F B9");
    if (!r || !(out->logic_seed = data_at(&f, r, 28)))
        return "the logic random seed";
    /* BFMEDesyncCheck's constructor: if (flag && logic->isInGame()) crc = logic->getCRC(0). */
    crc = match(&f, "80 3D ?? ?? ?? ?? 00 56 57 8B F1 74 1E 8B 3D ?? ?? ?? ?? 8B CF E8 ?? ?? ?? ?? "
                    "84 C0 74 0D 6A 00 8B CF E8 ?? ?? ?? ?? 89 06");
    if (!crc || !(out->verify_crc = byte_at(&f, crc, 2)) || !(out->logic = data_at(&f, crc, 15)) ||
        !(r = call_at(&f, crc, 21)))
        return "the client CRC check";
    out->is_in_game = img->base + r;
    if (!(r = call_at(&f, crc, 34)))
        return "the client CRC check";
    out->get_crc = img->base + r;
    /* A forwarder: mov ecx, [TheTacticalView]; mov eax, [ecx]; jmp [eax+0x258]. */
    r = match(&f, "8B 0D ?? ?? ?? ?? 8B 01 FF A0 58 02 00 00");
    if (!r || !(out->tactical_view = data_at(&f, r, 2)))
        return "TheTacticalView";
    /* W3DView::setCameraTransform, both camera->Set_Transform(transform) calls:
     * mov ecx, [ebx+camera]; mov eax, [ecx]; lea edx, [ebp-0x54]; push edx;
     * call [eax+Set_Transform]. */
    {
        pf_pattern p;
        unsigned at = 0;
        if (!pf_parse_pattern("8B 8B 04 01 00 00 8B 01 8D 55 AC 52 FF 50 54", &p) ||
            pf_scan(img, &p, &at) != 2)
            return "the view camera's Set_Transform calls";
        out->camera_off = read_le32(img->base + at + 2);
        out->set_transform = img->base[at + 14];
    }
    /* W3DDisplay's frame: if (!logic->paused) TheDisplay->updateViews(); ...;
     * TheFXParticleSystemManager->update() (the `views` hook site). */
    r = match(&f, "A1 ?? ?? ?? ?? 38 98 25 01 00 00 75 0F 8B 06 8B CE FF 90 A0 00 00 00 E8 ?? ?? ?? "
                  "?? 8B 0D ?? ?? ?? ?? 8B 01 FF 50 28");
    if (!r || !(out->particles = data_at(&f, r, 30)))
        return "TheFXParticleSystemManager";
    out->particles_slot = img->base[r + 38];
    return 0;
}
