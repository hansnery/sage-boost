/* Runs the real patch engine and startup sequence against manually mapped
 * copies of each supported game's baseline executable, and of modified
 * executables that must stay compatible (Age of the Ring's ROTWK game.dat).
 * Nothing is executed from the images (test_hooks.c runs the hooks) and no
 * image file is written; the startup scenarios write test_engine.ini/.log
 * next to the exe.
 * Usage: test_engine.exe --bfme2 <game.dat> [--wb <worldbuilder.exe>]
 *                        [--rotwk <game820.dat>] [--aotr <AotR game.dat>] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "baseline_expect.h"
#include "engine.h"
#include "gameversion.h"
#include "hooks.h"
#include "sides.h"
#include "unlocker.h"

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

typedef struct {
    unsigned char *mem;
    unsigned size;
} mapped;

/* The baseline the current scenarios compare against. */
static const fu_baseline_site *g_sites;

/* Lays the file out at its RVAs in private RW memory, like the loader would
 * (no imports, no relocations, no code run). */
static int map_image(const char *path, mapped *out)
{
    FILE *f = fopen(path, "rb");
    unsigned char *file;
    long len;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS32 *nt;
    IMAGE_SECTION_HEADER *sec;
    unsigned i;

    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    file = malloc((size_t)len);
    if (!file || fread(file, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(file);
        return 0;
    }
    fclose(f);
    dos = (IMAGE_DOS_HEADER *)file;
    nt = (IMAGE_NT_HEADERS32 *)(file + dos->e_lfanew);
    out->size = nt->OptionalHeader.SizeOfImage;
    out->mem = VirtualAlloc(NULL, out->size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!out->mem) {
        free(file);
        return 0;
    }
    memcpy(out->mem, file, nt->OptionalHeader.SizeOfHeaders);
    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        unsigned raw = sec->SizeOfRawData;
        if (sec->PointerToRawData + raw > (unsigned)len)
            raw = (unsigned)len - sec->PointerToRawData;
        if (sec->VirtualAddress + raw > out->size)
            raw = out->size - sec->VirtualAddress;
        memcpy(out->mem + sec->VirtualAddress, file + sec->PointerToRawData, raw);
    }
    free(file);
    return 1;
}

static int index_of(const char *id)
{
    unsigned i;
    for (i = 0; i < fu_patch_count; i++)
        if (strcmp(fu_patch_defs[i].id, id) == 0)
            return (int)i;
    return -1;
}

/* Bytes that differ between two images, outside the given patch sites. */
static unsigned diff_outside_sites(const mapped *a, const unsigned char *b,
                                   const fu_patch_result *r)
{
    unsigned off, n = 0, i;
    for (off = 0; off < a->size; off++) {
        int in_site = 0;
        if (a->mem[off] == b[off])
            continue;
        for (i = 0; i < fu_patch_count; i++)
            if (r[i].status == FU_ST_APPLIED && off >= r[i].site_rva &&
                off < r[i].site_rva + r[i].len)
                in_site = 1;
        if (!in_site)
            n++;
    }
    return n;
}

/* Hook k is installed in the mapped image: the site jumps/calls the stub, an
 * entry hook's trampoline holds the stolen bytes and jumps back, a call hook
 * continues at the original callee. `r` (optional) must agree. */
static void check_hook(const mapped *img, unsigned k, const fu_patch_result *r)
{
    const fu_baseline_site *b = &g_sites[k];
    const fu_patch_def *def = &fu_patch_defs[k];
    unsigned char *site = img->mem + b->rva;
    unsigned char *stub = fu_hook_stub(k), *next = fu_hook_next(k);
    unsigned rel, i;

    CHECK(site[0] == (def->kind == FU_KIND_ENTRY_HOOK ? 0xE9 : 0xE8), "[%s] opcode %02X", b->id,
          site[0]);
    memcpy(&rel, site + 1, 4);
    CHECK((UINT_PTR)site + 5 + rel == (UINT_PTR)stub, "[%s] site does not reach stub %u", b->id, k);
    for (i = 5; i < b->len; i++)
        CHECK(site[i] == 0xCC, "[%s] padding byte %u is %02X", b->id, i, site[i]);
    if (def->kind == FU_KIND_ENTRY_HOOK) {
        CHECK(b->len == def->steal, "[%s] steal %u != len %u", b->id, def->steal, b->len);
        CHECK(next && memcmp(next, b->original, def->steal) == 0, "[%s] trampoline bytes", b->id);
        if (next) {
            memcpy(&rel, next + def->steal + 1, 4);
            CHECK(next[def->steal] == 0xE9 &&
                      (UINT_PTR)next + def->steal + 5 + rel == (UINT_PTR)site + def->steal,
                  "[%s] trampoline does not jump back to site+%u", b->id, def->steal);
        }
    } else {
        CHECK(next == img->mem + b->callee_rva, "[%s] continues at %p, want RVA 0x%08X", b->id,
              (void *)next, b->callee_rva);
    }
    if (r)
        CHECK(r->hook_stub == stub && r->hook_next == next && memcmp(r->patched, site, b->len) == 0,
              "[%s] result disagrees with the image", b->id);
}

/* build/test_engine.exe -> build/test_engine<ext>: where fu_startup looks. */
static void exe_sibling(WCHAR *out, const WCHAR *ext)
{
    WCHAR *dot;
    GetModuleFileNameW(NULL, out, MAX_PATH);
    dot = wcsrchr(out, L'.');
    wcscpy(dot ? dot : out + wcslen(out), ext);
}

static int read_text(const WCHAR *path, char *buf, unsigned cap)
{
    FILE *f = _wfopen(path, L"rb");
    size_t n;
    if (!f)
        return 0;
    n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = 0;
    return 1;
}

static void write_text(const WCHAR *path, const char *text)
{
    FILE *f = _wfopen(path, L"wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void set_all(int *enabled, int value)
{
    unsigned i;
    for (i = 0; i < fu_patch_count; i++)
        enabled[i] = value;
}

static void set_defaults(int *enabled)
{
    unsigned i;
    for (i = 0; i < fu_patch_count; i++)
        enabled[i] = fu_patch_defs[i].default_on;
}

/* "sides: the game's own Sides (n): a, b, ..." as fu_startup logs it. */
static void native_line(char *out, const char *const *names, unsigned n)
{
    unsigned i;
    sprintf(out, "sides: the game's own Sides (%u): ", n);
    for (i = 0; i < n; i++) {
        if (i)
            strcat(out, ", ");
        strcat(out, names[i]);
    }
    strcat(out, "\r\n");
}

static const fu_baseline_game *baseline_for(const fu_game *g)
{
    unsigned i;
    for (i = 0; i < sizeof fu_baseline_games / sizeof fu_baseline_games[0]; i++)
        if (strcmp(fu_baseline_games[i].title, g->title) == 0)
            return &fu_baseline_games[i];
    return NULL;
}

/* Maps `path`, identifies it as `bg`'s game and selects its table. */
static int open_game(const char *path, const fu_baseline_game *bg, mapped *img, fu_image *fimg)
{
    fu_game_version ver;
    const fu_game *g;

    if (!map_image(path, img)) {
        printf("  cannot map %s\n", path);
        g_failures++;
        return 0;
    }
    fu_game_version_read((HMODULE)img->mem, &ver);
    printf("  %s: %u.%u.%u.%u \"%ls\"\n", path, ver.v[0], ver.v[1], ver.v[2], ver.v[3],
           ver.internal_name);
    g = fu_game_identify(&ver);
    CHECK(g && strcmp(g->title, bg->title) == 0, "not identified as %s", bg->title);
    if (!g || !fu_image_open(fimg, img->mem)) {
        g_failures++;
        return 0;
    }
    fu_select_game(g);
    CHECK(fu_patch_count == g->count, "count");
    g_sites = bg->sites;
    printf("  .text RVA 0x%08X size 0x%X\n", fimg->text_rva, fimg->text_size);
    return 1;
}

static void test_baseline(const char *path, const fu_baseline_game *bg)
{
    mapped img;
    unsigned char *pristine;
    fu_image fimg;
    fu_patch_result r[FU_MAX_PATCHES];
    int enabled[FU_MAX_PATCHES];
    unsigned i;
    const fu_game *g;

    printf("=== %s baseline\n", bg->title);
    if (!open_game(path, bg, &img, &fimg))
        return;
    g = &fu_games[0];
    while (strcmp(g->title, bg->title) != 0)
        g++;
    CHECK(fimg.timestamp == g->baseline_timestamp, "TimeDateStamp is not the baseline's");
    pristine = malloc(img.size);
    memcpy(pristine, img.mem, img.size);

    printf("game's Side table\n");
    CHECK(fu_sides_read_native(&fimg, g) == bg->native_count, "read %u Sides, want %u",
          fu_sides_native_count(), bg->native_count);
    for (i = 0; i < bg->native_count && i < fu_sides_native_count(); i++)
        CHECK(strcmp(fu_sides_native_name(i), bg->native_sides[i]) == 0, "Side %u is %s, want %s", i,
              fu_sides_native_name(i), bg->native_sides[i]);

    printf("all patches on\n");
    set_all(enabled, 1);
    fu_engine_run(&fimg, enabled, 0, r);
    for (i = 0; i < fu_patch_count; i++) {
        const fu_baseline_site *b = &g_sites[i];
        CHECK(strcmp(b->id, fu_patch_defs[i].id) == 0, "row %u order mismatch", i);
        printf("  [%-2s] %-30s RVA 0x%08X\n", fu_patch_defs[i].id, fu_status_name(r[i].status),
               r[i].site_rva);
        CHECK(r[i].status == FU_ST_APPLIED, "[%s] not applied", b->id);
        CHECK(r[i].matches == 1, "[%s] %u matches", b->id, r[i].matches);
        CHECK(r[i].site_rva == b->rva, "[%s] site 0x%08X != 0x%08X", b->id, r[i].site_rva, b->rva);
        CHECK(r[i].len == b->len, "[%s] len %u != %u", b->id, r[i].len, b->len);
        CHECK(memcmp(r[i].original, b->original, b->len) == 0, "[%s] original bytes", b->id);
        CHECK(memcmp(pristine + b->rva, b->original, b->len) == 0, "[%s] pristine bytes", b->id);
        if (b->is_hook)
            check_hook(&img, i, &r[i]);
        else
            CHECK(memcmp(img.mem + b->rva, b->patched, b->len) == 0, "[%s] patched bytes in image",
                  b->id);
    }
    CHECK(diff_outside_sites(&img, pristine, r) == 0, "bytes changed outside the patch sites");

    printf("second run on the patched image\n");
    {
        unsigned char *before = malloc(img.size);
        memcpy(before, img.mem, img.size);
        fu_engine_run(&fimg, enabled, 0, r);
        for (i = 0; i < fu_patch_count; i++) {
            /* A hooked call still matches its signature but now calls outside .text. */
            fu_status want =
                fu_patch_defs[i].kind == FU_KIND_CALL_HOOK ? FU_ST_BAD_TARGET : FU_ST_NOT_FOUND;
            CHECK(r[i].status == want, "[%s] status %s, want %s", fu_patch_defs[i].id,
                  fu_status_name(r[i].status), fu_status_name(want));
        }
        CHECK(memcmp(before, img.mem, img.size) == 0, "second run changed the image");
        CHECK(fu_sides_read_native(&fimg, g) == 0, "Side table read through a hooked S1");
        free(before);
    }

    printf("defaults (A C D E S1 S2 S3 S4 on, B C2 off)\n");
    memcpy(img.mem, pristine, img.size);
    set_defaults(enabled);
    fu_engine_run(&fimg, enabled, 0, r);
    for (i = 0; i < fu_patch_count; i++) {
        const fu_baseline_site *b = &g_sites[i];
        if (fu_patch_defs[i].default_on) {
            CHECK(r[i].status == FU_ST_APPLIED, "[%s] not applied", b->id);
        } else {
            CHECK(r[i].status == FU_ST_OFF, "[%s] should be off", b->id);
            CHECK(memcmp(img.mem + b->rva, b->original, b->len) == 0, "[%s] touched while off", b->id);
        }
    }
    CHECK(index_of("A") >= 0 && fu_patch_defs[index_of("A")].default_on, "A not default-on");
    CHECK(index_of("B") >= 0 && !fu_patch_defs[index_of("B")].default_on, "B default-on");

    printf("everything off\n");
    memcpy(img.mem, pristine, img.size);
    set_all(enabled, 0);
    fu_engine_run(&fimg, enabled, 0, r);
    for (i = 0; i < fu_patch_count; i++)
        CHECK(r[i].status == FU_ST_OFF, "[%s] not off", fu_patch_defs[i].id);
    CHECK(memcmp(img.mem, pristine, img.size) == 0, "image changed with everything off");

    printf("dry run\n");
    set_all(enabled, 1);
    fu_engine_run(&fimg, enabled, 1, r);
    for (i = 0; i < fu_patch_count; i++)
        CHECK(r[i].status == FU_ST_LOCATED, "[%s] not located", fu_patch_defs[i].id);
    CHECK(memcmp(img.mem, pristine, img.size) == 0, "dry run changed the image");

    printf("ambiguous signature (A duplicated elsewhere in .text)\n");
    {
        int a = index_of("A");
        unsigned start = g_sites[a].rva - fu_patch_defs[a].site_offset;
        memcpy(img.mem + fimg.text_rva + 0x100, img.mem + start, 32);
        fu_engine_run(&fimg, enabled, 0, r);
        CHECK(r[a].status == FU_ST_AMBIGUOUS && r[a].matches == 2, "[A] status %s matches %u",
              fu_status_name(r[a].status), r[a].matches);
        CHECK(memcmp(img.mem + g_sites[a].rva, g_sites[a].original, 2) == 0,
              "[A] written although ambiguous");
        for (i = 0; i < fu_patch_count; i++)
            if ((int)i != a)
                CHECK(r[i].status == FU_ST_APPLIED, "[%s] not applied next to a refused A",
                      fu_patch_defs[i].id);
    }

    printf("branch target outside .text (E)\n");
    {
        int e = index_of("E");
        unsigned site = g_sites[e].rva;
        static const unsigned char far_rel[4] = { 0xF0, 0xFF, 0xFF, 0x7F };
        memcpy(img.mem, pristine, img.size);
        memcpy(img.mem + site + 2, far_rel, 4);
        fu_engine_run(&fimg, enabled, 0, r);
        CHECK(r[e].status == FU_ST_BAD_TARGET, "[E] status %s", fu_status_name(r[e].status));
        CHECK(memcmp(img.mem + site + 2, far_rel, 4) == 0 && img.mem[site] == 0x0F,
              "[E] written although the target is bad");
    }

    printf("site already patched by someone else (D)\n");
    {
        int d = index_of("D");
        unsigned site = g_sites[d].rva;
        memcpy(img.mem, pristine, img.size);
        img.mem[site] = 0xEB;
        fu_engine_run(&fimg, enabled, 0, r);
        CHECK(r[d].status == FU_ST_NOT_FOUND, "[D] status %s", fu_status_name(r[d].status));
        CHECK(img.mem[site] == 0xEB && img.mem[site + 1] == pristine[site + 1], "[D] rewritten");
    }

    printf("full startup with defaults (no INI)\n");
    {
        WCHAR ini[MAX_PATH], logp[MAX_PATH];
        char log[16384], want[1024];
        int d = index_of("D");
        exe_sibling(ini, L".ini");
        exe_sibling(logp, L".log");
        DeleteFileW(ini);
        memcpy(img.mem, pristine, img.size);
        fu_startup(GetModuleHandleW(NULL), (HMODULE)img.mem);
        CHECK(read_text(logp, log, sizeof log), "no log written");
        printf("%s", log);
        sprintf(want, "game: %s, TimeDateStamp", bg->title);
        CHECK(strstr(log, want) != NULL, "game not named in the log");
        CHECK(strstr(log, "the verified baseline build") != NULL, "baseline build not identified");
        native_line(want, bg->native_sides, bg->native_count);
        CHECK(strstr(log, want) != NULL, "native Sides not logged: %s", want);
        CHECK(strstr(log, "[A] KeepSelectedFaction: applied") != NULL, "A not logged as applied");
        CHECK(strstr(log, "[B] NoRandomFallback: off") != NULL, "B not logged as off");
        CHECK(strstr(log, "[S1] SideFactionId: not installed, [Sides] has no entries") != NULL,
              "S1 not skipped without [Sides]");
        CHECK(strstr(log, "[S2] SkirmishSlot: applied") != NULL &&
                  strstr(log, "[S4] SkirmishAI: applied") != NULL,
              "S2/S4 not applied without [Sides]");
        CHECK(strstr(log, "skirmish: an AI whose Side has no skirmish player") != NULL,
              "skirmish fallback not logged");
        CHECK(strstr(log, "summary: 6 applied, 0 refused, 4 off") != NULL, "wrong summary");
        CHECK(memcmp(img.mem + g_sites[d].rva, g_sites[d].patched, g_sites[d].len) == 0,
              "[D] not written by fu_startup");
        for (i = 0; i < fu_patch_count; i++)
            if (fu_patch_defs[i].needs_sides)
                CHECK(memcmp(img.mem + g_sites[i].rva, g_sites[i].original, g_sites[i].len) == 0,
                      "[%s] hooked without [Sides]", fu_patch_defs[i].id);
        fu_shutdown();
        CHECK(read_text(logp, log, sizeof log) &&
                  strstr(log, "[S4] SkirmishAI: 0 calls, 0 with the Side replaced") != NULL &&
                  strstr(log, "[S1] SideFactionId: 0 calls") == NULL,
              "exit statistics wrong without [Sides]");

        printf("full startup, INI: DryRun=1, B=1, C2=1\n");
        memcpy(img.mem, pristine, img.size);
        write_text(ini, "[FactionUnlocker]\r\nDryRun=1\r\n[Patches]\r\nB=1\r\nC2=1\r\n");
        fu_startup(GetModuleHandleW(NULL), (HMODULE)img.mem);
        CHECK(read_text(logp, log, sizeof log), "no log written");
        CHECK(strstr(log, "summary: 8 located, 0 refused, 2 off") != NULL, "wrong dry-run summary");
        CHECK(memcmp(img.mem, pristine, img.size) == 0, "dry run via INI changed the image");

        printf("full startup, INI: S4=0\n");
        memcpy(img.mem, pristine, img.size);
        write_text(ini, "[Patches]\r\nS4=0\r\n");
        fu_startup(GetModuleHandleW(NULL), (HMODULE)img.mem);
        CHECK(read_text(logp, log, sizeof log), "no log written");
        CHECK(strstr(log, "[S2] SkirmishSlot: not installed (its partner patch is off or refused)") !=
                  NULL,
              "S2 not refused without S4");
        CHECK(strstr(log, "summary: 4 applied, 1 refused, 5 off") != NULL, "wrong S4=0 summary");
        CHECK(memcmp(img.mem + g_sites[index_of("S2")].rva, g_sites[index_of("S2")].original,
                     g_sites[index_of("S2")].len) == 0,
              "[S2] written without S4");
        fu_shutdown();

        printf("full startup, INI: [Sides] Rohan=Men, Isengard=Men, E=0\n");
        memcpy(img.mem, pristine, img.size);
        write_text(ini, "[Patches]\r\nE=0\r\n[Sides]\r\nRohan = Men\r\nIsengard=Men\r\n");
        fu_startup(GetModuleHandleW(NULL), (HMODULE)img.mem);
        CHECK(read_text(logp, log, sizeof log), "no log written");
        printf("%s", log);
        CHECK(strstr(log, "sides: Rohan -> Men\r\n") != NULL, "alias not logged");
        CHECK(strstr(log, "sides: rejected Isengard=Men: already a Side in this game") != NULL,
              "native alias not rejected");
        CHECK(strstr(log, "sides: 1 alias(es)") != NULL, "alias count not logged");
        CHECK(strstr(log, "WARNING D or E is off") != NULL, "missing D/E warning");
        CHECK(strstr(log, "[S1] SideFactionId: applied") != NULL, "S1 not applied");
        CHECK(strstr(log, "stolen bytes run from trampoline") != NULL, "trampoline not logged");
        CHECK(strstr(log, "continues at the original callee") != NULL, "callee not logged");
        CHECK(strstr(log, "summary: 7 applied, 0 refused, 3 off") != NULL, "wrong sides summary");
        for (i = 0; i < fu_patch_count; i++)
            if (fu_patch_defs[i].needs_sides)
                check_hook(&img, i, NULL);
        fu_shutdown();
        CHECK(read_text(logp, log, sizeof log), "no log after shutdown");
        CHECK(strstr(log, "summary: 7 applied") != NULL, "shutdown truncated the log");
        CHECK(strstr(log, "exit: hook activity this session") != NULL, "no exit statistics");
        CHECK(strstr(log, "[S1] SideFactionId: 0 calls, 0 with the Side replaced") != NULL &&
                  strstr(log, "[S2] SkirmishSlot: 0 calls, 0 with the Side replaced") != NULL,
              "no S1/S2 statistics");
        DeleteFileW(ini);
    }

    VirtualFree(img.mem, 0, MEM_RELEASE);
    free(pristine);
}

/* Age of the Ring's game.dat: ROTWK 2.01 with its own hooks and a 14-entry
 * Side table. Every patch must still land on the clean 2.01 sites, and its
 * own Sides must not be aliasable. */
static void test_aotr(const char *path, const fu_baseline_game *bg)
{
    mapped img;
    fu_image fimg;
    fu_patch_result r[FU_MAX_PATCHES];
    int enabled[FU_MAX_PATCHES];
    unsigned char *pristine;
    unsigned i, n;
    const fu_game *g = &fu_games[0];

    printf("=== Age of the Ring game.dat (modified %s)\n", bg->title);
    if (!open_game(path, bg, &img, &fimg))
        return;
    while (strcmp(g->title, bg->title) != 0)
        g++;
    pristine = malloc(img.size);
    memcpy(pristine, img.mem, img.size);

    n = fu_sides_read_native(&fimg, g);
    printf("  %u Sides:", n);
    for (i = 0; i < n; i++)
        printf(" %s", fu_sides_native_name(i));
    printf("\n");
    CHECK(n > bg->native_count, "the mod's Side table is not larger than vanilla's");
    CHECK(fu_sides_is_native("Rohan") && fu_sides_is_native("Dunland") && fu_sides_is_native("Men") &&
              fu_sides_is_native("Goblins") && !fu_sides_is_native("Harad"),
          "the mod's Sides were not read");

    set_all(enabled, 1);
    fu_engine_run(&fimg, enabled, 0, r);
    for (i = 0; i < fu_patch_count; i++) {
        const fu_baseline_site *b = &g_sites[i];
        printf("  [%-2s] %-30s RVA 0x%08X\n", fu_patch_defs[i].id, fu_status_name(r[i].status),
               r[i].site_rva);
        CHECK(r[i].status == FU_ST_APPLIED && r[i].site_rva == b->rva &&
                  memcmp(r[i].original, b->original, b->len) == 0,
              "[%s] not applied at the clean 2.01 site with the clean bytes", b->id);
    }
    CHECK(diff_outside_sites(&img, pristine, r) == 0, "bytes changed outside the patch sites");

    printf("full startup, INI: [Sides] Rohan=Men, Harad=Mordor\n");
    {
        WCHAR ini[MAX_PATH], logp[MAX_PATH];
        char log[16384];
        exe_sibling(ini, L".ini");
        exe_sibling(logp, L".log");
        memcpy(img.mem, pristine, img.size);
        write_text(ini, "[Sides]\r\nRohan=Men\r\nHarad=Mordor\r\n");
        fu_startup(GetModuleHandleW(NULL), (HMODULE)img.mem);
        CHECK(read_text(logp, log, sizeof log), "no log written");
        printf("%s", log);
        CHECK(strstr(log, "Rohan, Arnor, Goblins") != NULL, "the mod's Sides not logged");
        CHECK(strstr(log, "sides: rejected Rohan=Men: already a Side in this game") != NULL,
              "the mod's own Rohan was aliasable");
        CHECK(strstr(log, "sides: Harad -> Mordor\r\n") != NULL, "Harad not accepted");
        CHECK(strstr(log, "summary: 8 applied, 0 refused, 2 off") != NULL, "wrong summary");
        DeleteFileW(ini);
    }
    VirtualFree(img.mem, 0, MEM_RELEASE);
    free(pristine);
}

int main(int argc, char **argv)
{
    const char *bfme2 = NULL, *wb = NULL, *rotwk = NULL, *aotr = NULL;
    fu_game_version ver;
    int i;

    for (i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "--bfme2") == 0)
            bfme2 = argv[i + 1];
        else if (strcmp(argv[i], "--wb") == 0)
            wb = argv[i + 1];
        else if (strcmp(argv[i], "--rotwk") == 0)
            rotwk = argv[i + 1];
        else if (strcmp(argv[i], "--aotr") == 0)
            aotr = argv[i + 1];
    }
    if (!bfme2) {
        printf("usage: %s --bfme2 <game.dat> [--wb <worldbuilder.exe>] [--rotwk <game820.dat>] "
               "[--aotr <AotR game.dat>]\n",
               argv[0]);
        return 2;
    }

    printf("version gate\n");
    fu_game_version_read(GetModuleHandleW(NULL), &ver);
    CHECK(!fu_game_identify(&ver), "test harness recognised as a game");
    if (wb) {
        mapped m;
        if (map_image(wb, &m)) {
            fu_game_version_read((HMODULE)m.mem, &ver);
            printf("  %s: %u.%u.%u.%u \"%ls\"\n", wb, ver.v[0], ver.v[1], ver.v[2], ver.v[3],
                   ver.internal_name);
            CHECK(!fu_game_identify(&ver), "WorldBuilder recognised as a game");
            VirtualFree(m.mem, 0, MEM_RELEASE);
        }
    }
    CHECK(fu_game_count == sizeof fu_baseline_games / sizeof fu_baseline_games[0],
          "baseline_expect.h and patch_table.c disagree on the games");
    for (i = 0; i < (int)fu_game_count; i++)
        CHECK(baseline_for(&fu_games[i]) != NULL, "no baseline for %s", fu_games[i].title);

    test_baseline(bfme2, baseline_for(&fu_games[0]));
    if (rotwk)
        test_baseline(rotwk, baseline_for(&fu_games[1]));
    else
        printf("=== ROTWK 2.01 baseline: not given, skipped\n");
    if (aotr)
        test_aotr(aotr, baseline_for(&fu_games[1]));
    else
        printf("=== Age of the Ring game.dat: not given, skipped\n");

    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: engine tests passed\n");
    return 0;
}
