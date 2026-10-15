/* Every timing site's signature matches exactly once in Age of the Ring's
 * game.dat and in ROTWK 2.01, at the expected RVA, with the expected bytes
 * in its stolen prologue. Images not given are skipped.
 * Usage: test_sites.exe [--aotr <game.dat>] [--rotwk <game820.dat>] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "btnclock.h"
#include "cusites.h"
#include "scan.h"
#include "sites.h"
#include "fpsites.h"
#include "ltsites.h"
#include "passflag.h"
#include "spsites.h"
#include "uistate.h"

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

/* The E8 rel32 calls in .text that land on target (a byte scan: every
 * offset is tried), in address order; at most max are stored. */
static unsigned calls_to(const pf_image *img, unsigned target, unsigned *out, unsigned max)
{
    unsigned i, n = 0, end = img->text_rva + img->text_size;
    for (i = img->text_rva; i + 5 <= end; i++)
        if (img->base[i] == 0xE8 && i + 5 + *(const unsigned *)(img->base + i + 1) == target) {
            if (n < max)
                out[n] = i;
            n++;
        }
    return n;
}

/* Lays the file out at its RVAs in private RW memory (no imports, no
 * relocations, no code run). */
static unsigned char *map_image(const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char *file, *mem;
    long len;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS32 *nt;
    IMAGE_SECTION_HEADER *sec;
    unsigned i, size;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    file = malloc((size_t)len);
    if (!file || fread(file, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(file);
        return NULL;
    }
    fclose(f);
    dos = (IMAGE_DOS_HEADER *)file;
    nt = (IMAGE_NT_HEADERS32 *)(file + dos->e_lfanew);
    size = nt->OptionalHeader.SizeOfImage;
    mem = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem) {
        free(file);
        return NULL;
    }
    memcpy(mem, file, nt->OptionalHeader.SizeOfHeaders);
    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        unsigned raw = sec->SizeOfRawData;
        if (sec->PointerToRawData + raw > (unsigned)len)
            raw = (unsigned)len - sec->PointerToRawData;
        if (sec->VirtualAddress + raw > size)
            raw = size - sec->VirtualAddress;
        memcpy(mem + sec->VirtualAddress, file + sec->PointerToRawData, raw);
    }
    free(file);
    return mem;
}

static const char *const g_prologue[PF_SITE_COUNT] = {
    "55 8B EC 51 53", "B8 DA 41 B8 00", "55 8B EC 83 EC 28", "83 EC 4C 53 55",
    "83 EC 30 56 8B F1", "B8 ?? ?? ?? ??", "55 8B EC 83 EC 0C", "B8 ?? ?? ?? ??",
    "64 A1 00 00 00 00", "8B 44 24 04 85 C0", "E9 ?? ?? ?? ??", "A1 ?? ?? ?? ??",
    "E8 ?? ?? ?? ??", "E8 ?? ?? ?? ??", "E8 ?? ?? ?? ??", "E8 ?? ?? ?? ??",
    "E8 ?? ?? ?? ??", "FF 92 48 01 00 00", "FF 15 ?? ?? ?? ??", "E8 ?? ?? ?? ??",
    "51 53 55 56 57", "6A FF 68 ?? ?? ?? ??", "B8 ?? ?? ?? ??", "FF 90 A0 00 00 00",
    "B8 ?? ?? ?? ??", "B8 ?? ?? ?? ??", "55 8B EC 83 EC 10", "55 8B EC 83 7D 08 00",
    "55 8B EC 83 7D 08 00", "E8 ?? ?? ?? ??", "81 E1 80 03 00 00", "C1 E8 03 83 E0 70",
    "C1 EB 03 83 E3 70", "E8 ?? ?? ?? ??", "A1 ?? ?? ?? ??", "8B 4B 0C 8D 41 F8",
    "B8 ?? ?? ?? ??", "8D 45 C0 50 8D 45 88", "33 DB 39 9E 60 01 00 00", "8D 45 C0 50 8D 45 88",
    "8B 9E 94 00 00 00", "57 8D 8D 60 FD FF FF", "51 8D 8D 60 FD FF FF", "89 47 28 8B 45 9C",
    "8B 03 57 8B CB", "8B 03 57 8B CB", "55 8B EC 81 EC 88 00 00 00",
};

static void check_image(const char *label, const char *path)
{
    unsigned char *mem = map_image(path);
    pf_image img;
    unsigned i;

    printf("%s: %s\n", label, path);
    CHECK(mem != NULL, "can't map %s", path);
    if (!mem)
        return;
    CHECK(pf_image_open(&img, mem), "no .text in %s", path);
    for (i = 0; i < PF_SITE_COUNT; i++) {
        const pf_site *s = &pf_sites[i];
        pf_pattern sig, pro;
        unsigned rva = 0, n;
        CHECK(pf_parse_pattern(s->signature, &sig), "%s: bad signature", s->id);
        CHECK(pf_parse_pattern(g_prologue[i], &pro) && pro.len == s->steal,
              "%s: prologue length %u != steal %u", s->id, pro.len, s->steal);
        n = pf_scan(&img, &sig, &rva);
        rva += s->site_offset;
        printf("  %-11s %u match(es) at %08X\n", s->id, n, rva);
        CHECK(n == 1, "%s: %u matches", s->id, n);
        CHECK(rva == s->rva_201, "%s: at %08X, expected %08X", s->id, rva, s->rva_201);
        CHECK(n == 1 && pf_pattern_at(mem + rva, &pro), "%s: stolen bytes differ", s->id);
    }
    {
        pf_ui_state ui;
        const char *why = pf_ui_find(&img, &ui);
        unsigned ui_rva = ui.ui ? (unsigned)((const unsigned char *)ui.ui - mem) : 0;
        printf("  ui          TheInGameUI at RVA %08X, pending +%X, scrolling +%X (%s)\n", ui_rva,
               ui.pending_off, ui.scroll_off, why ? why : "ok");
        CHECK(!why, "ui state: %s", why);
        CHECK(ui_rva == 0x009E4830 && ui.pending_off == 0x53C && ui.scroll_off == 0x7F8,
              "ui state: RVA %08X, +%X, +%X", ui_rva, ui.pending_off, ui.scroll_off);
    }
    {
        /* ~HLodClass puts back the HLod vtable; its Set_Transform slot (+0x54)
         * must be the hooked HLodClass::Set_Transform. */
        const IMAGE_NT_HEADERS32 *nt =
            (const IMAGE_NT_HEADERS32 *)(mem + ((const IMAGE_DOS_HEADER *)mem)->e_lfanew);
        unsigned image_base = nt->OptionalHeader.ImageBase;
        unsigned vt = *(const unsigned *)(mem + pf_sites[PF_HLOD_DTOR].rva_201 + 31) - image_base;
        unsigned set = *(const unsigned *)(mem + vt + 0x54) - image_base;
        printf("  hlod        vtable %08X, Set_Transform slot %08X\n", vt, set);
        CHECK(set == pf_sites[PF_HLOD_SET].rva_201, "HLod vtable slot 0x54 is %08X", set);
    }
    {
        pf_fps_sites fs;
        const char *why = pf_fps_find(&img, &fs);
#define RVA(p) ((p) ? (unsigned)((const unsigned char *)(p) - mem) : 0u)
        printf("  fps         %s: display %08X draw %08X client %08X sync %08X/%08X seed %08X "
               "crc flag %08X getCRC %08X\n",
               why ? why : "ok", RVA(fs.display), RVA(fs.draw), RVA(fs.client), RVA(fs.sync_time),
               RVA(fs.prev_sync_time), RVA(fs.logic_seed), RVA(fs.verify_crc), RVA(fs.get_crc));
        CHECK(!why, "fps sites: %s", why);
        CHECK(RVA(fs.display) == 0x009E4418 && RVA(fs.draw) == 0x0004B788 &&
                  RVA(fs.hwnd) == 0x009C3C64 && RVA(fs.client) == 0x009E4388 &&
                  RVA(fs.last_client_frame) == 0x00998C8C && RVA(fs.sync_acc) == 0x009C7580 &&
                  RVA(fs.sync_time) == 0x009D1E0C && RVA(fs.prev_sync_time) == 0x009D1E10 &&
                  RVA(fs.last_draw_ms) == 0x009C7700 && RVA(fs.skip_count) == 0x00998CB4 &&
                  RVA(fs.init_flags) == 0x009C7704 && RVA(fs.screenshots) == 0x009C7568 &&
                  RVA(fs.logic_seed) == 0x009A1CA4 && RVA(fs.verify_crc) == 0x009E87C5 &&
                  RVA(fs.logic) == 0x009E412C && RVA(fs.get_crc) == 0x00225886 &&
                  RVA(fs.is_in_game) == 0x00041B60 && RVA(fs.tactical_view) == 0x009E447C &&
                  fs.camera_off == 0x104 && fs.set_transform == 0x54 &&
                  RVA(fs.particles) == 0x009E3744 && fs.particles_slot == 0x28,
              "fps sites differ from the AotR/2.01 layout");
        CHECK(fs.draw == mem + pf_sites[PF_DISPLAY_DRAW].rva_201,
              "the wrapped display draw isn't the checked W3DDisplay::draw");
#undef RVA
    }
    {
        /* The particle manager's vtables (the W3D one and its base) hold the
         * hooked update at the slot the draw calls; nothing calls it directly. */
        const IMAGE_NT_HEADERS32 *nt =
            (const IMAGE_NT_HEADERS32 *)(mem + ((const IMAGE_DOS_HEADER *)mem)->e_lfanew);
        unsigned va = nt->OptionalHeader.ImageBase + pf_sites[PF_PARTICLES].rva_201, i, slots = 0,
                 calls = 0;
        for (i = 0; i + 4 <= nt->OptionalHeader.SizeOfImage; i += 4)
            if (*(const unsigned *)(mem + i) == va)
                slots++;
        for (i = img.text_rva; i + 5 <= img.text_rva + img.text_size; i++)
            if ((mem[i] == 0xE8 || mem[i] == 0xE9) &&
                i + 5 + *(const int *)(mem + i + 1) == pf_sites[PF_PARTICLES].rva_201)
                calls++;
        printf("  particles   update at %08X: %u vtable slot(s), %u direct call(s)\n",
               pf_sites[PF_PARTICLES].rva_201, slots, calls);
        CHECK(slots == 2 && calls == 0, "particle update: %u slots, %u direct calls", slots, calls);
    }
    {
        /* The push button draw's three draws (check-like, color, image) read
         * the button data at window +0x2C. They are called only from it, and
         * hold every `mov byte [esi], 0; call setUserData` that clears a
         * clock request, so its entry sees every push button clock. */
        static const unsigned calls_at[3] = { 0x1A, 0x68, 0x73 };
        static const unsigned want_draw[3] = { 0x000A4A25, 0x000A4585, 0x000A5112 };
        const unsigned base = pf_sites[PF_BUTTON_DRAW].rva_201;
        unsigned off = 0, draw[3], setter, i, j, callers[3] = { 0 }, clears = 0, clears_in = 0;
        const char *why = pf_btn_find(&img, &pf_sites[PF_BUTTON_DRAW], &off);
        printf("  buttons     %s: button data at window +%X\n", why ? why : "ok", off);
        CHECK(!why && off == 0x2C, "button clock draw: %s, +%X", why ? why : "ok", off);
        for (i = 0; i < 3; i++) {
            unsigned at = base + calls_at[i];
            draw[i] = at + 5 + *(const unsigned *)(mem + at + 1);
            CHECK(draw[i] == want_draw[i], "draw %u at %08X, want %08X", i, draw[i],
                  want_draw[i]);
        }
        /* The setter the request setters and the draws' clears call. */
        setter = pf_sites[PF_CLOCK_SET].rva_201 + 0x2D;
        setter = setter + 5 + *(const unsigned *)(mem + setter + 1);
        for (i = img.text_rva; i + 5 <= img.text_rva + img.text_size; i++) {
            unsigned to;
            if (mem[i] != 0xE8)
                continue;
            to = i + 5 + *(const unsigned *)(mem + i + 1);
            for (j = 0; j < 3; j++)
                if (to == draw[j])
                    callers[j] += i == base + calls_at[j] ? 1 : 100;
            if (to == setter && i >= 3 && mem[i - 3] == 0xC6 && mem[i - 2] == 0x06 &&
                mem[i - 1] == 0x00) {
                clears++;
                for (j = 0; j < 3; j++)
                    clears_in += i > draw[j] && i < draw[j] + 0x800;
            }
        }
        printf("  buttons     draws %08X %08X %08X, callers %u %u %u, request clears %u (%u in "
               "the draws)\n",
               draw[0], draw[1], draw[2], callers[0], callers[1], callers[2], clears, clears_in);
        CHECK(callers[0] == 1 && callers[1] == 1 && callers[2] == 1,
              "the draws are called from elsewhere too: %u %u %u", callers[0], callers[1],
              callers[2]);
        CHECK(clears == 3 && clears_in == 3, "request clears: %u, %u in the draws", clears,
              clears_in);
        CHECK(img.base[pf_sites[PF_CLOCK_SET].rva_201 + 0x2C] == 1 &&
                  img.base[pf_sites[PF_CLOCK_SET_LEFT].rva_201 + 0x2C] == 2,
              "the request setters don't set 1 and 2");
    }
    {
        /* GameClient::update: TheDisplay->draw() (vtable +0x30), then a few
         * calls later TheInGameUI->update() (+0x28), whose control bar update
         * asks for the button clocks: a request is made for the next draw. */
        const IMAGE_NT_HEADERS32 *nt =
            (const IMAGE_NT_HEADERS32 *)(mem + ((const IMAGE_DOS_HEADER *)mem)->e_lfanew);
        unsigned base = nt->OptionalHeader.ImageBase, at = 0, n, ui_at = 0, i;
        pf_pattern sig;
        CHECK(pf_parse_pattern("8B 0D ?? ?? ?? ?? 8B 01 FF 50 30 8B 0D ?? ?? ?? ?? 8B 01 FF 50 28 A1",
                               &sig),
              "draw order sig");
        n = pf_scan(&img, &sig, &at);
        for (i = at + 22; n == 1 && i < at + 0x60; i++)
            if (mem[i] == 0x8B && mem[i + 1] == 0x0D &&
                *(const unsigned *)(mem + i + 2) == base + 0x009E4830 && mem[i + 6] == 0x8B &&
                mem[i + 7] == 0x01 && mem[i + 8] == 0xFF && mem[i + 9] == 0x50 &&
                mem[i + 10] == 0x28) {
                ui_at = i;
                break;
            }
        printf("  ui order    display draw at %08X, in-game UI update at %08X\n", at, ui_at);
        CHECK(n == 1 && *(const unsigned *)(mem + at + 2) == base + 0x009E4418 && ui_at,
              "GameClient::update: %u match(es), display %08X, UI update at %08X", n,
              n ? *(const unsigned *)(mem + at + 2) - base : 0, ui_at);
    }
    {
        /* UpdateShadowMap's flag: raised, lowered and tested by MeshClass::Render. */
        const volatile unsigned char *flag;
        const char *why = pf_shadow_flag_find(&img, &flag);
        unsigned rva = flag ? (unsigned)((const unsigned char *)flag - mem) : 0;
        printf("  shadowflag  at RVA %08X (%s)\n", rva, why ? why : "ok");
        CHECK(!why && rva == 0x009D1E44, "shadow-map flag: %s, RVA %08X", why ? why : "ok", rva);
    }
    {
        /* The shadow-pass fixes' data, and where their call sites go. */
        pf_sp_data d;
        const char *why = pf_sp_find(&img, &d);
        const IMAGE_NT_HEADERS32 *nt =
            (const IMAGE_NT_HEADERS32 *)(mem + ((const IMAGE_DOS_HEADER *)mem)->e_lfanew);
        unsigned image_base = nt->OptionalHeader.ImageBase, rva, same, pcall, fwd, i, slots = 0;
        unsigned char *const *mgr;
#define RVA(p) ((p) ? (unsigned)((const unsigned char *)(p) - mem) : 0u)
        printf("  shadowpass  %s: GlobalData %08X device %08X particle render %08X smudge "
               "manager %08X smudge count %08X\n",
               why ? why : "ok", RVA(d.gd), RVA(d.device), RVA(d.particle_render), RVA(d.smudge),
               RVA(d.smudge_count));
        CHECK(!why && RVA(d.gd) == 0x009E4364 && RVA(d.device) == 0x009D3474 &&
                  RVA(d.particle_render) == 0x0004C84A && RVA(d.smudge) == 0x009CBB6C &&
                  RVA(d.smudge_count) == 0x009C7730,
              "shadow-pass data: %s", why ? why : "differs from the AotR/2.01 layout");
        /* same_batch calls Is_Rendering_Identical; the particle call goes to the
         * forwarder, whose manager's W3D vtables hold the checked render at +0x40. */
        rva = pf_sites[PF_SAME_BATCH].rva_201;
        same = rva + 5 + *(const unsigned *)(mem + rva + 1);
        pcall = pf_sites[PF_SHADOW_PARTICLES].rva_201;
        fwd = pcall + 5 + *(const unsigned *)(mem + pcall + 1);
        mgr = pf_sp_forwarder(&img, mem + fwd);
        printf("  shadowpass  batch test %08X, particle forwarder %08X (manager %08X)\n", same,
               fwd, RVA(mgr));
        CHECK(same == 0x00149430, "same_batch calls %08X", same);
        CHECK(fwd == 0x0004C3EA && RVA(mgr) == 0x009E3744, "particle forwarder %08X, manager %08X",
              fwd, RVA(mgr));
        for (i = img.text_rva + img.text_size; i + 4 <= nt->OptionalHeader.SizeOfImage; i += 4)
            if (*(const unsigned *)(mem + i) == image_base + 0x0004C84Au)
                slots++;
        CHECK(slots == 1, "the particle render is in %u vtable slot(s)", slots);
        /* The group scan ShadowBatchIgnoreLight's light fix reads ([EBP+8],
         * EBX), with the batch test and the setup call where the sites are. */
        printf("  shadowpass  group scan %08X\n", d.batch_scan);
        CHECK(d.batch_scan == 0x00173C14 &&
                  d.batch_scan + PF_SP_SCAN_SAME == pf_sites[PF_SAME_BATCH].rva_201 &&
                  d.batch_scan + PF_SP_SCAN_SETUP == pf_sites[PF_FX_GROUP].rva_201,
              "group scan %08X", d.batch_scan);
#undef RVA
    }
    {
        /* What the light fix and the stale count rely on (design doc,
         * Milestone 8, "The device state the shadow pass leaves"):
         * PerformRendering (0x173BEB) is reached only from the three batch
         * list renders, each called once, from the FX flush between
         * Set_Light_Lock(1) and Set_Light_Lock(0); nothing holds their
         * addresses. */
        static const unsigned perf = 0x00173BEB, lists[3] = { 0x00174113, 0x00174148, 0x001742B7 },
                              list_calls[3] = { 0x0017442A, 0x00174431, 0x00174438 },
                              perf_calls[3] = { 0x00174135, 0x00174284, 0x001742DB };
        const IMAGE_NT_HEADERS32 *nt =
            (const IMAGE_NT_HEADERS32 *)(mem + ((const IMAGE_DOS_HEADER *)mem)->e_lfanew);
        unsigned image_base = nt->OptionalHeader.ImageBase, found[8], n, i, j;
        pf_pattern p;
        n = calls_to(&img, perf, found, 8);
        CHECK(n == 3 && found[0] == perf_calls[0] && found[1] == perf_calls[1] &&
                  found[2] == perf_calls[2],
              "PerformRendering has %u direct callers", n);
        for (i = 0; i < 3; i++) {
            n = calls_to(&img, lists[i], found, 8);
            CHECK(n == 1 && found[0] == list_calls[i], "list render %08X: %u callers",
                  lists[i], n);
            CHECK(perf_calls[i] > lists[i] && perf_calls[i] < 0x00174406,
                  "%08X isn't in a list render", perf_calls[i]);
        }
        for (i = img.text_rva + img.text_size, n = 0; i + 4 <= nt->OptionalHeader.SizeOfImage;
             i += 4)
            for (j = 0; j < 3; j++)
                n += *(const unsigned *)(mem + i) == image_base + lists[j] ||
                     *(const unsigned *)(mem + i) == image_base + perf;
        CHECK(n == 0, "%u data references to the batch renders", n);
        /* The FX flush: lock, the three lists, unlock. Set_Light_Lock stores
         * the byte and, on unlock, clears the cached environment and calls
         * Set_Light_Environment with the one it had. */
        CHECK(pf_parse_pattern("6A 01 E8 ?? ?? ?? ?? 59 8B CE E8 ?? ?? ?? ?? 8B CE E8 ?? ?? ?? ?? "
                               "8B CE E8 ?? ?? ?? ?? 33 F6 56 E8 ?? ?? ?? ?? 59",
                               &p) &&
                  pf_pattern_at(mem + 0x00174420, &p) &&
                  0x00174422 + 5 + *(const unsigned *)(mem + 0x00174423) == 0x00124DE0 &&
                  0x00174440 + 5 + *(const unsigned *)(mem + 0x00174441) == 0x00124DE0,
              "the FX flush doesn't lock the lights around its lists");
        CHECK(pf_parse_pattern("8A 44 24 04 84 C0 A2 60 34 DD 00 75 18 A1 54 34 DD 00 C7 05 54 34 "
                               "DD 00 00 00 00 00 89 44 24 04 E9",
                               &p) &&
                  pf_pattern_at(mem + 0x00124DE0, &p) &&
                  0x00124E00 + 5 + *(const unsigned *)(mem + 0x00124E01) ==
                      pf_sites[PF_LENV_SET].rva_201,
              "Set_Light_Lock differs");
        /* WW3D::End_Render (every render-target pass, the shadow map's too):
         * the sorting flush, End_Scene, then the cache invalidation, which
         * zeroes the cached environment and the pending state bits. */
        CHECK(pf_parse_pattern("80 3D ?? ?? ?? ?? 00 74 3C 53 E8 ?? ?? ?? ?? 8B 5C 24 08 53 C6 05 ?? "
                               "?? ?? ?? 00 E8",
                               &p) &&
                  pf_pattern_at(mem + 0x00116DA0, &p) &&
                  0x00116DBB + 5 + *(const unsigned *)(mem + 0x00116DBC) ==
                      pf_sites[PF_END_SCENE].rva_201 &&
                  mem[0x00116DE0] == 0xE8 &&
                  0x00116DE0 + 5 + *(const unsigned *)(mem + 0x00116DE1) == 0x0011F690,
              "End_Render differs");
        CHECK(pf_parse_pattern("53 56 57 33 DB B8 78 56 34 12 B9 00 01 00 00 BF 38 30 DD 00 89 1D "
                               "34 1F DD 00 89 1D 54 34 DD 00",
                               &p) &&
                  pf_pattern_at(mem + 0x0011F690, &p),
              "the cache invalidation differs");
    }
    {
        /* The light cuts' data: renderOneObject's continuation points and the
         * callees they rely on (see the design doc's Milestone 8 proofs). */
        pf_lt_data d;
        const char *why = pf_lt_find(&img, &d);
        const unsigned roo = pf_sites[PF_ROO].rva_201, roo_end = 0x0006FE84;
        pf_pattern body;
        unsigned k;
#define RVA(p) ((p) ? (unsigned)((const unsigned char *)(p) - mem) : 0u)
        printf("  lights      %s: global skip %08X lists %08X after %08X default %08X; Add_Light "
               "%08X Pre_Render_Update %08X sphere test %08X bounding sphere %08X; cut-off %08X "
               "lighting %08X\n",
               why ? why : "ok", RVA(d.glights_skip), RVA(d.lists), RVA(d.lists_after),
               RVA(d.lists_default), RVA(d.add_light), RVA(d.pre_render_update),
               RVA(d.spheres_intersect), RVA(d.bsphere), RVA(d.threshold), RVA(d.lighting));
        CHECK(!why && RVA(d.glights_skip) == 0x0006F9F6 && RVA(d.lists) == 0x0006FC16 &&
                  RVA(d.lists_after) == 0x0006FD36 && RVA(d.lists_default) == 0x0006FD42 &&
                  RVA(d.add_light) == 0x00140250 && RVA(d.pre_render_update) == 0x00140500 &&
                  RVA(d.spheres_intersect) == 0x0006E93D && RVA(d.bsphere) == 0x0006C850 &&
                  RVA(d.threshold) == 0x009D8E4C && RVA(d.lighting) == 0x009D325C &&
                  RVA(d.light_env) == 0x009D3454 && RVA(d.light_lock) == 0x009D3460,
              "light data: %s", why ? why : "differs from the AotR/2.01 layout");
        CHECK(RVA(d.lists) == pf_sites[PF_ROO_LISTS].rva_201,
              "the lists template isn't at the lists site");
        for (k = PF_LIGHT_FIRST; k < PF_LENV_SET; k++)
            CHECK(pf_sites[k].rva_201 >= roo && pf_sites[k].rva_201 < roo_end,
                  "%s isn't inside renderOneObject", pf_sites[k].id);
        /* The sphere test reads both spheres and returns (r1+r2)^2 > |c1-c2|^2:
         * no other memory, no side effect. */
        CHECK(pf_parse_pattern("8B 44 24 04 8B 4C 24 08 F3 0F 10 00 F3 0F 5C 01 F3 0F 10 50 08 F3 0F "
                               "5C 51 08 F3 0F 10 48 04 F3 0F 5C 49 04 F3 0F 10 58 0C F3 0F 58 59 0C "
                               "0F 28 E0 F3 0F 59 E0 0F 28 C2 F3 0F 59 C2 F3 0F 58 E0 0F 28 C1 F3 0F "
                               "59 C1 F3 0F 58 E0 0F 28 C3 F3 0F 59 C3 0F 2F C4 76 03 B0 01 C3 32 C0 "
                               "C3",
                               &body) &&
                  pf_pattern_at(mem + 0x0006E93D, &body),
              "the sphere test's body differs");
        /* The FX group setup hands each group's light environment to
         * Set_Light_Environment (the device state the stale count watches). */
        CHECK(mem[0x0014A0C1] == 0xE8 &&
                  0x0014A0C1 + 5 + *(const unsigned *)(mem + 0x0014A0C1 + 1) ==
                      pf_sites[PF_LENV_SET].rva_201,
              "the FX group setup doesn't call Set_Light_Environment");
        /* LightClass's and W3DDynamicLight's Get_Transform (+0x50) is the
         * container walk that does nothing without a container. */
        CHECK(pf_parse_pattern("56 8B F1 8B 4E 7C 85 C9 74 41", &body) &&
                  pf_pattern_at(mem + 0x0013B320, &body),
              "RenderObjClass's Get_Transform differs");
#undef RVA
    }
    {
        /* Catch-up: the render's start, the frame's no-render path, what a
         * skipped frame still runs and the limiter's state. */
        pf_cu_sites cu;
        const char *why = pf_cu_find(&img, &cu);
        const unsigned char *site = mem + pf_sites[PF_CU_RENDER].rva_201, *t;
        unsigned i, from_site = 0;
#define RVA(p) ((p) ? (unsigned)((const unsigned char *)(p) - mem) : 0u)
        printf("  catchup     %s: no-render %08X scene %08X proj %08X shadow %08X ui anim %08X "
               "start %08X limit %08X time %08X client %08X+%X logic %08X+%X device %08X\n",
               why ? why : "ok", RVA(cu.no_render), RVA(cu.scene), RVA(cu.proj_override),
               RVA(cu.shadow_flag), RVA(cu.ui_anim), RVA(cu.frame_start), RVA(cu.limit_on),
               RVA(cu.time_import), RVA(cu.client), cu.client_running, RVA(cu.logic),
               cu.logic_paused, RVA(cu.device));
        CHECK(!why, "catch-up sites: %s", why);
        CHECK(RVA(cu.no_render) == 0x0004A230 && RVA(cu.scene) == 0x009C7574 &&
                  RVA(cu.proj_override) == 0x009D3445 && RVA(cu.shadow_flag) == 0x009D1E44 &&
                  RVA(cu.ui_anim) == 0x002A536C && RVA(cu.frame_start) == 0x009E4318 &&
                  RVA(cu.limit_on) == 0x009E4320 && RVA(cu.time_import) == 0x007D0920 &&
                  RVA(cu.client) == 0x009E4388 && cu.client_running == 0xC8 &&
                  RVA(cu.logic) == 0x009E412C && cu.logic_paused == 0x125 &&
                  RVA(cu.device) == 0x009D3474 && RVA(cu.ui.ui) == 0x009E4830,
              "catch-up sites differ from the AotR/2.01 layout");
        /* GameLogic::update's `inc [esi+0x40]` (0x0022E577) after its
         * isInGame call, and BfmeLogicFramesPerSecond (5 in the image). */
        printf("  catchup     logic frame counter +%X, logic fps %08X = %u\n", cu.logic_frame,
               RVA(cu.logic_fps), cu.logic_fps ? *cu.logic_fps : 0);
        CHECK(cu.logic_frame == 0x40 && RVA(cu.logic_fps) == 0x0099F608 && *cu.logic_fps == 5 &&
                  mem[0x0022E577] == 0xFF && mem[0x0022E578] == 0x46 && mem[0x0022E579] == 0x40,
              "the logic frame counter: +%X, logic fps at %08X", cu.logic_frame,
              RVA(cu.logic_fps));
        /* The site is `mov eax, [TheGameLogic]` with EBX = 0 (the function's
         * xor ebx, ebx before it); the no-render path tests BL. */
        {
            const IMAGE_NT_HEADERS32 *nt =
                (const IMAGE_NT_HEADERS32 *)(mem + ((const IMAGE_DOS_HEADER *)mem)->e_lfanew);
            CHECK(site[0] == 0xA1 && *(const unsigned *)(site + 1) - nt->OptionalHeader.ImageBase ==
                                         0x009E412C,
                  "the site isn't mov eax, [TheGameLogic]");
        }
        t = cu.no_render;
        CHECK(t && t[0] == 0x38 && t[1] == 0x1D && t[6] == 0x74 && t[8] == 0x88 && t[9] == 0x1D,
              "the no-render path isn't `cmp [x], bl; je; mov [x], bl`");
        for (i = 0x49CF8; i < 0x49D20; i++)
            from_site += mem[i] == 0x33 && mem[i + 1] == 0xDB;
        CHECK(from_site == 1, "the frame's xor ebx, ebx: %u", from_site);
        /* The frame's own three jcc rel32 go to the no-render path; no branch
         * lands inside the stolen instruction (every rel8 and rel32 jump
         * shape in the function, byte by byte, so this over-counts if anything). */
        {
            unsigned to_path = 0, into_site = 0, fn = 0x49CF8, end = 0x4A290;
            for (i = fn; i < end; i++) {
                unsigned dst = 0;
                if ((mem[i] & 0xF0) == 0x70 || mem[i] == 0xEB)
                    dst = i + 2 + (unsigned)(int)(signed char)mem[i + 1];
                else if (mem[i] == 0xE9)
                    dst = i + 5 + *(const unsigned *)(mem + i + 1);
                if (mem[i] == 0x0F && (mem[i + 1] & 0xF0) == 0x80) {
                    dst = i + 6 + *(const unsigned *)(mem + i + 2);
                    to_path += dst == RVA(cu.no_render);
                }
                into_site += dst > pf_sites[PF_CU_RENDER].rva_201 &&
                             dst < pf_sites[PF_CU_RENDER].rva_201 + 5;
            }
            printf("  catchup     branches to the no-render path %u, into the stolen bytes %u\n",
                   to_path, into_site);
            CHECK(to_path == 3 && into_site == 0, "branches: %u to the path, %u into the site",
                  to_path, into_site);
        }
#undef RVA
    }
    {
        /* The views hook's register: call [eax+0xA0]. */
        pf_pattern sig;
        CHECK(pf_parse_pattern(pf_sites[PF_VIEWS].signature, &sig) &&
                  sig.bytes[pf_sites[PF_VIEWS].site_offset + 1] == 0x90,
              "views site isn't call [eax+disp32]");
    }
    {
        /* scene_update: the object's On_Frame_Update call 14 bytes on, the
         * next node 17 bytes on (where extra frames continue); nothing in
         * Customized_Render branches into the 6 stolen bytes. The loop the
         * signature covers has its branches fixed in it (its je's rel32 is
         * checked); the rest of the function is scanned byte by byte. */
        const unsigned site = pf_sites[PF_SCENE_UPDATE].rva_201, fn = 0x0006FE84;
        const unsigned loop = site - pf_sites[PF_SCENE_UPDATE].site_offset;
        unsigned i, into = 0, je = loop + 14 + *(const unsigned *)(mem + loop + 10), loop_end;
        pf_pattern sig;
        CHECK(pf_parse_pattern(pf_sites[PF_SCENE_UPDATE].signature, &sig), "scene_update sig");
        loop_end = loop + sig.len;
        CHECK(mem[loop + 8] == 0x0F && mem[loop + 9] == 0x84 && (je <= site || je >= site + 6),
              "scene_update: the loop's je lands at %08X", je);
        CHECK(mem[site + 14] == 0xFF && mem[site + 15] == 0x50 && mem[site + 16] == 0x34 &&
                  mem[site + 17] == 0x8B && mem[site + 18] == 0x5B && mem[site + 19] == 0x04,
              "scene_update: no call [eax+0x34] / mov ebx, [ebx+4] after the site");
        CHECK(fn == pf_sites[PF_CUST].rva_201, "Customized_Render at %08X",
              pf_sites[PF_CUST].rva_201);
        for (i = fn; i < fn + 0x400; i++) {
            unsigned dst = 0;
            if (i >= loop && i < loop_end)
                continue;
            if ((mem[i] & 0xF0) == 0x70 || mem[i] == 0xEB)
                dst = i + 2 + (unsigned)(int)(signed char)mem[i + 1];
            else if (mem[i] == 0xE9)
                dst = i + 5 + *(const unsigned *)(mem + i + 1);
            else if (mem[i] == 0x0F && (mem[i + 1] & 0xF0) == 0x80)
                dst = i + 6 + *(const unsigned *)(mem + i + 2);
            into += dst > site && dst < site + 6;
        }
        printf("  scene_upd   branches into the stolen bytes %u\n", into);
        CHECK(into == 0, "scene_update: %u branches into the stolen bytes", into);
    }
    VirtualFree(mem, 0, MEM_RELEASE);
}

int main(int argc, char **argv)
{
    pf_pattern p;
    unsigned char code[] = { 0x90, 0x55, 0x8B, 0xEC, 0x90, 0x55, 0x8B, 0x00 };
    pf_image fake = { code, 0, sizeof code, 0 };
    unsigned rva = 0;
    int i, images = 0;

    printf("pattern parser and scanner\n");
    CHECK(pf_parse_pattern("55 8B ??", &p) && p.len == 3 && p.fixed[0] && !p.fixed[2],
          "parse");
    CHECK(!pf_parse_pattern("55 8", &p), "half byte accepted");
    CHECK(!pf_parse_pattern("?? ??", &p) || pf_scan(&fake, &p, &rva) == 0, "all-wildcard scan");
    CHECK(pf_parse_pattern("55 8B ??", &p) && pf_scan(&fake, &p, &rva) == 2 && rva == 1,
          "scan count/first");
    CHECK(pf_crc32((const unsigned char *)"123456789", 9) == 0xCBF43926u, "crc32");

    for (i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--aotr") || !strcmp(argv[i], "--rotwk")) {
            check_image(argv[i] + 2, argv[i + 1]);
            images++;
        }
    }
    if (!images)
        printf("no images given: site checks skipped\n");
    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: site tests passed\n");
    return 0;
}
