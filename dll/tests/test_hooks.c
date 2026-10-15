/* Executes the side hooks. The engine locates and hooks copies of the real
 * S1-S4 code taken from game.dat, with the wildcarded addresses (calls, the
 * name table, a global) pointed at fakes in this process; the copies then
 * run. Also unit-tests the [Sides] parser and the skirmish resolver.
 * Usage: test_hooks.exe <game.dat> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "engine.h"
#include "hooks.h"
#include "sides.h"
#include "skirmish.h"

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

/* The game's AsciiString layout (see sides.c). */
typedef struct {
    LONG ref_count;
    unsigned short length, capacity;
    char data[32];
} ascii_buf;

typedef struct {
    ascii_buf *m_data;
} ascii_string;

typedef struct {
    ascii_buf buf;
    ascii_string str;
} owned_string;

static void make_string(owned_string *s, const char *text)
{
    s->buf.ref_count = 1;
    s->buf.length = (unsigned short)strlen(text);
    s->buf.capacity = (unsigned short)(s->buf.length + 1);
    strcpy(s->buf.data, text);
    s->str.m_data = &s->buf;
}

static const char *str_of(const void *obj)
{
    const ascii_string *s = obj;
    return s && s->m_data ? s->m_data->data : "";
}

/* ------------------------------------------------------------------ game.dat */

static unsigned char *g_file;
static long g_file_len;

static int load_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    g_file_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_file = malloc((size_t)g_file_len);
    if (!g_file || fread(g_file, 1, (size_t)g_file_len, f) != (size_t)g_file_len) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

static const unsigned char *at_rva(unsigned rva)
{
    IMAGE_NT_HEADERS32 *nt = (void *)(g_file + ((IMAGE_DOS_HEADER *)g_file)->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    unsigned i;
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
        if (rva >= sec->VirtualAddress && rva < sec->VirtualAddress + sec->SizeOfRawData)
            return g_file + sec->PointerToRawData + (rva - sec->VirtualAddress);
    return NULL;
}

/* ------------------------------------------------------------------ fakes */

static const char *const k_names[7] = { "Men", "Elves", "Dwarves", "Isengard", "Mordor", "Wild",
                                        "Spare" };
static const char k_goblins[] = "Goblins";
static const void *g_zero_global; /* S3's [0xDFE78C]: null skips its first call */
static const void *g_seen_this, *g_seen_arg;

/* AsciiString::compare(const char *): 0 when equal. */
static int __attribute__((thiscall)) fake_compare(const void *self, const char *s)
{
    return _stricmp(str_of(self), s);
}

static int __attribute__((thiscall)) fake_callee(const void *self, const void *arg)
{
    g_seen_this = self;
    g_seen_arg = arg;
    return 0;
}

/* reg_probe(fn, out): calls fn(out) with known ebx/esi/edi and CF set. */
void reg_probe(void *fn, unsigned *out);
__asm__(".text\n"
        "_reg_probe:\n"
        "    pushl %ebx\n"
        "    pushl %esi\n"
        "    pushl %edi\n"
        "    movl 16(%esp), %eax\n"
        "    movl 20(%esp), %ecx\n"
        "    movl $0x11223344, %ebx\n"
        "    movl $0x55667788, %esi\n"
        "    movl $0x99AABBCC, %edi\n"
        "    pushl %ecx\n"
        "    stc\n"
        "    call *%eax\n"
        "    addl $4, %esp\n"
        "    popl %edi\n"
        "    popl %esi\n"
        "    popl %ebx\n"
        "    ret\n");

static void put32(unsigned char *p, unsigned v)
{
    memcpy(p, &v, 4);
}

static void put_rel(unsigned char *insn_end_minus4, const void *dest)
{
    put32(insn_end_minus4,
          (unsigned)((UINT_PTR)dest - (UINT_PTR)(insn_end_minus4 + 4)));
}

static int index_of(const char *id)
{
    unsigned i;
    for (i = 0; i < fu_patch_count; i++)
        if (strcmp(fu_patch_defs[i].id, id) == 0)
            return (int)i;
    return -1;
}

/* findSkirmishSide stand-in: the map has a skirmish player for each Side in
 * g_map; records which `this` it ran with. */
static const char *g_map[4];
static const void *g_find_self;

static unsigned char __attribute__((thiscall)) fake_find(void *self, int *index)
{
    const char *side = str_of((char *)self + FU_PLAYER_SIDE_OFF);
    unsigned i;
    g_find_self = self;
    for (i = 0; i < sizeof g_map / sizeof g_map[0] && g_map[i]; i++)
        if (_stricmp(g_map[i], side) == 0) {
            *index = (int)i;
            return 1;
        }
    return 0;
}

/* Fake .text layout (offsets inside the text section). */
#define TEXT_RVA 0x1000
#define TEXT_SIZE 0x1000
#define OFF_S1 0x100      /* copy of the whole S1 function */
#define OFF_S3 0x200      /* copy of the S3 prologue + our tail */
#define OFF_S2 0x300      /* copy of the S2 signature, then jmp fake_find */
#define OFF_THUNK 0x380   /* jmp fake_callee (the call hook needs a callee in .text) */
#define OFF_S4 0x480      /* copy of the S4 signature region */
#define OFF_LAUNCH 0x4E0  /* jumps into the S4 call site */

typedef int(__cdecl *s1_fn)(const void *side);
typedef unsigned(__attribute__((thiscall)) * s3_fn)(void *self, const void *side);
typedef unsigned char(__attribute__((thiscall)) * s2_fn)(void *self, int *index);
typedef int(__cdecl *launch_fn)(const void *side, const void *self);

static unsigned char *build_fake_text(unsigned char *base)
{
    unsigned char *t = base + TEXT_RVA, *p;
    int s2 = index_of("S2"), s4 = index_of("S4");

    memset(t, 0xCC, TEXT_SIZE);

    /* S1: Rva0033A3F4Lookup, 60 bytes up to its last ret. */
    memcpy(t + OFF_S1, at_rva(0x0033A3F4), 0x3C);
    put32(t + OFF_S1 + 0x05, (unsigned)(UINT_PTR)k_goblins);
    put_rel(t + OFF_S1 + 0x0A, (const void *)fake_compare);
    put32(t + OFF_S1 + 0x1C, (unsigned)(UINT_PTR)k_names);
    put_rel(t + OFF_S1 + 0x25, (const void *)fake_compare);

    /* S3: rank thresholds prologue; [global] = 0 takes the je to
     * `mov ebx,[esp+0x10]` (the Side) and `push imm32`, then our tail returns
     * side ^ this. */
    memcpy(t + OFF_S3, at_rva(0x00200157), 0x2E);
    put32(t + OFF_S3 + 0x06, (unsigned)(UINT_PTR)&g_zero_global);
    p = t + OFF_S3 + 0x2E;
    put32(p, 0x11111111);                     /* push imm32 operand */
    p += 4;
    memcpy(p, "\x58\x8B\xC3\x33\xC6\x5F\x5E\x5B\xC2\x04\x00", 11);
    /* pop eax; mov eax,ebx; xor eax,esi; pop edi; pop esi; pop ebx; ret 4 */

    /* S2: findSkirmishSide's first 38 bytes. The body after the stolen
     * `mov eax,imm32` is replaced by a jmp to fake_find once hooked. */
    memcpy(t + OFF_S2, at_rva(fu_patch_defs[s2].baseline_rva), 38);

    /* S4: the signature region around the inline Side compare. */
    {
        unsigned off = fu_patch_defs[s4].site_offset;
        unsigned char *site = t + OFF_S4 + off;
        memcpy(t + OFF_S4, at_rva(fu_patch_defs[s4].baseline_rva - off), off + 13);
        put_rel(site + 1, t + OFF_THUNK);
        put32(site + 9, 0); /* test eax,eax; je -> the ret below either way */
        site[13] = 0xC3;
        /* mov ecx,[esp+8]; push dword [esp+4]; jmp site */
        p = t + OFF_LAUNCH;
        memcpy(p, "\x8B\x4C\x24\x08\xFF\x74\x24\x04\xE9", 9);
        put_rel(p + 9, site);
        t[OFF_THUNK] = 0xE9;
        put_rel(t + OFF_THUNK + 1, (const void *)fake_callee);
    }
    return t;
}

/* An engine Player with only the Side at +FU_PLAYER_SIDE_OFF filled in. */
typedef struct {
    unsigned char pad[FU_PLAYER_SIDE_OFF];
    ascii_string side;
} fake_player;

/* ------------------------------------------------------------------ tests */

static void test_parser(void)
{
    static const char *const natives[] = { "Men",    "Elves", "Dwarves", "Isengard",
                                           "Mordor", "Wild",  "Neutral", "Goblins" };
    printf("sides parser\n");
    fu_sides_set_native(natives, sizeof natives / sizeof natives[0]);
    CHECK(fu_sides_is_native("mordor") && fu_sides_is_native("Civilian") &&
              !fu_sides_is_native("Angmar"),
          "native list");
    fu_sides_clear();
    CHECK(fu_sides_add("Rohan", "Men") == NULL, "Rohan=Men rejected");
    CHECK(fu_sides_add("Gondor_2", "men") == NULL, "Gondor_2=men rejected");
    CHECK(fu_sides_add("rohan", "Elves") != NULL, "duplicate (case-insensitive) accepted");
    CHECK(fu_sides_add("Mordor", "Men") != NULL, "vanilla new Side accepted");
    CHECK(fu_sides_add("goblins", "Men") != NULL, "vanilla new Side (lower case) accepted");
    CHECK(fu_sides_add("Arnor", "Arnor") != NULL, "self alias accepted");
    CHECK(fu_sides_add("Arnor", "Rohan") != NULL, "alias chain accepted");
    CHECK(fu_sides_add("Bad Name", "Men") != NULL, "space accepted");
    CHECK(fu_sides_add("", "Men") != NULL, "empty accepted");
    CHECK(fu_sides_add("A234567890123456789012345678901X", "Men") != NULL, "32 chars accepted");
    CHECK(fu_sides_add("A23456789012345678901234567890X", "Men") == NULL, "31 chars rejected");
    CHECK(fu_sides_count() == 3, "count %u", fu_sides_count());
    {
        owned_string a, b, c, d;
        ascii_string empty = { NULL };
        const ascii_string *donor;
        make_string(&a, "Rohan");
        make_string(&b, "ROHAN");
        make_string(&c, "Rohann");
        make_string(&d, "Men");
        donor = fu_sides_resolve(&a.str);
        CHECK(donor && strcmp(str_of(donor), "Men") == 0, "Rohan does not resolve to Men");
        CHECK(donor && donor->m_data->length == 3 && donor->m_data->capacity == 4,
              "donor header length/capacity");
        CHECK(donor && donor->m_data->ref_count >= 0x40000000, "donor not permanent");
        CHECK(fu_sides_resolve(&b.str) == donor, "case-insensitive match failed");
        CHECK(fu_sides_resolve(&c.str) == NULL, "prefix matched");
        CHECK(fu_sides_resolve(&d.str) == NULL, "vanilla Side resolved");
        CHECK(fu_sides_resolve(&empty) == NULL, "empty string resolved");
        CHECK(fu_sides_resolve(NULL) == NULL, "NULL resolved");
    }

    printf("sides from INI\n");
    {
        char dir[MAX_PATH];
        WCHAR ini[MAX_PATH];
        FILE *f;
        GetTempPathA(MAX_PATH, dir);
        strcat(dir, "fu_test_sides.ini");
        MultiByteToWideChar(CP_ACP, 0, dir, -1, ini, MAX_PATH);
        f = fopen(dir, "wb");
        fputs("[Sides]\r\n; comment\r\n  Rohan = Men   ; trailing comment\r\nArnor=Dwarves\r\n"
              "Isengard=Men\r\nNoEquals\r\nGondor=Mén\r\n[Other]\r\nX=Men\r\n",
              f);
        fclose(f);
        CHECK(fu_sides_load(ini) == 2, "loaded %u aliases, want 2", fu_sides_count());
        CHECK(strcmp(fu_sides_new_name(0), "Rohan") == 0 && strcmp(fu_sides_donor_name(0), "Men") == 0,
              "entry 0 is %s=%s", fu_sides_new_name(0), fu_sides_donor_name(0));
        CHECK(strcmp(fu_sides_new_name(1), "Arnor") == 0 &&
                  strcmp(fu_sides_donor_name(1), "Dwarves") == 0,
              "entry 1 is %s=%s", fu_sides_new_name(1), fu_sides_donor_name(1));
        DeleteFileA(dir);
        CHECK(fu_sides_load(ini) == 0, "missing INI produced aliases");
    }

    printf("sides capacity (%d)\n", FU_MAX_SIDES);
    {
        char dir[MAX_PATH], name[32];
        WCHAR ini[MAX_PATH];
        owned_string last;
        const ascii_string *donor;
        FILE *f;
        int i;
        GetTempPathA(MAX_PATH, dir);
        strcat(dir, "fu_test_sides_cap.ini");
        MultiByteToWideChar(CP_ACP, 0, dir, -1, ini, MAX_PATH);
        f = fopen(dir, "wb");
        fputs("[Sides]\r\n", f);
        for (i = 0; i <= FU_MAX_SIDES; i++)
            fprintf(f, "Extra_Side_Number_%02d=%s\r\n", i, i % 2 ? "Elves" : "Men");
        fclose(f);
        CHECK(fu_sides_load(ini) == FU_MAX_SIDES, "loaded %u aliases, want %d", fu_sides_count(),
              FU_MAX_SIDES);
        DeleteFileA(dir);
        sprintf(name, "Extra_Side_Number_%02d", FU_MAX_SIDES - 1);
        CHECK(strcmp(fu_sides_new_name(FU_MAX_SIDES - 1), name) == 0, "last entry is %s",
              fu_sides_new_name(FU_MAX_SIDES - 1));
        CHECK(*fu_sides_new_name(FU_MAX_SIDES) == 0, "entry past the cap is %s",
              fu_sides_new_name(FU_MAX_SIDES));
        make_string(&last, name);
        donor = fu_sides_resolve(&last.str);
        CHECK(donor && strcmp(str_of(donor), (FU_MAX_SIDES - 1) % 2 ? "Elves" : "Men") == 0,
              "last entry does not resolve to its donor");
        sprintf(name, "Extra_Side_Number_%02d", FU_MAX_SIDES);
        make_string(&last, name);
        CHECK(fu_sides_resolve(&last.str) == NULL, "entry past the cap resolved");
        CHECK(fu_sides_add("Lorinand", "Elves") != NULL, "add past the cap accepted");
        fu_sides_clear();
        for (i = 0; i < 17; i++) {
            sprintf(name, "Seventeen_%02d", i);
            CHECK(fu_sides_add(name, "Men") == NULL, "entry %d of 17 rejected", i + 1);
        }
        CHECK(fu_sides_count() == 17, "count %u, want 17", fu_sides_count());
    }
}

static const char *g_mock_has;

static int mock_query(const void *side)
{
    return g_mock_has && _stricmp(str_of(side), g_mock_has) == 0;
}

static void test_resolver(void)
{
    owned_string men, neutral, rohan, rohan_uc;
    ascii_string empty = { NULL };
    fu_skirmish_step step;
    const void *r;

    printf("skirmish resolver\n");
    make_string(&men, "Men");
    make_string(&neutral, "Neutral");
    make_string(&rohan, "Rohan");
    make_string(&rohan_uc, "ROHAN");
    fu_sides_clear();
    CHECK(fu_sides_add("Rohan", "Dwarves") == NULL, "alias rejected");

    fu_skirmish_set_query(NULL);
    r = fu_skirmish_resolve(&men.str, &step);
    CHECK(r == &men.str && step == FU_SKIRMISH_NONE, "no query: step %d", step);

    fu_skirmish_set_query(mock_query);
    g_mock_has = "Men";
    r = fu_skirmish_resolve(&men.str, &step);
    CHECK(r == &men.str && step == FU_SKIRMISH_OWN, "own: step %d", step);
    r = fu_skirmish_resolve(&rohan.str, &step);
    CHECK(r == fu_sides_native_string(0) && step == FU_SKIRMISH_FALLBACK, "fallback: %s %d",
          str_of(r), step);
    g_mock_has = "Dwarves";
    r = fu_skirmish_resolve(&rohan.str, &step);
    CHECK(r == fu_sides_resolve(&rohan.str) && step == FU_SKIRMISH_DONOR, "donor: %s %d",
          str_of(r), step);
    g_mock_has = "Neutral";
    r = fu_skirmish_resolve(&men.str, &step);
    CHECK(r == &men.str && step == FU_SKIRMISH_NONE, "fell back to Neutral: %s", str_of(r));
    g_mock_has = "Elves";
    r = fu_skirmish_resolve(&neutral.str, &step);
    CHECK(r == &neutral.str && step == FU_SKIRMISH_NONE, "reserved Side resolved: %s", str_of(r));
    r = fu_skirmish_resolve(&empty, &step);
    CHECK(r == &empty && step == FU_SKIRMISH_NONE, "empty Side resolved");
    CHECK(strcmp(fu_ascii_cstr(&empty), "") == 0 && strcmp(fu_ascii_cstr(NULL), "") == 0 &&
              strcmp(fu_ascii_cstr(&men.str), "Men") == 0,
          "fu_ascii_cstr");
    CHECK(fu_sides_is_reserved("observer") && !fu_sides_is_reserved("Men"), "reserved list");
    {
        const ascii_string *n = fu_sides_native_string(7);
        CHECK(n && strcmp(str_of(n), "Goblins") == 0 && n->m_data->length == 7 &&
                  n->m_data->ref_count >= 0x40000000,
              "native string 7");
        CHECK(fu_sides_native_string(8) == NULL, "native string past the end");
    }

    fu_skirmish_reset();
    CHECK(fu_skirmish_first_unresolved(&rohan.str), "first report");
    CHECK(!fu_skirmish_first_unresolved(&rohan_uc.str), "second report (other case)");
    CHECK(fu_skirmish_first_unresolved(&men.str), "first report of another Side");
    fu_skirmish_reset();
    CHECK(fu_skirmish_first_unresolved(&rohan.str), "report after reset");
    fu_skirmish_set_query(NULL);
}

static void test_execution(void)
{
    unsigned char *base = VirtualAlloc(NULL, TEXT_RVA + TEXT_SIZE, MEM_COMMIT | MEM_RESERVE,
                                       PAGE_EXECUTE_READWRITE);
    unsigned char *t = build_fake_text(base);
    fu_image img = { base, TEXT_RVA, TEXT_SIZE, 0, (unsigned)(UINT_PTR)base, TEXT_RVA + TEXT_SIZE };
    fu_patch_result r[FU_MAX_PATCHES];
    int enabled[FU_MAX_PATCHES] = { 0 };
    int s1 = index_of("S1"), s2 = index_of("S2"), s3 = index_of("S3"), s4 = index_of("S4");
    s1_fn lookup = (s1_fn)(void *)(t + OFF_S1);
    s3_fn ranks = (s3_fn)(void *)(t + OFF_S3);
    s2_fn find = (s2_fn)(void *)(t + OFF_S2);
    launch_fn launch = (launch_fn)(void *)(t + OFF_LAUNCH);
    owned_string men, rohan, rohan_lc, gondor, arnor, goblins, other, mordor;
    const void *donor_men, *donor_mordor, *elves;
    void *self = (void *)(UINT_PTR)0x00ABC000;
    unsigned calls, hits;
    unsigned char s1_prologue[9];
    fake_player player;
    int index;

    make_string(&men, "Men");
    make_string(&rohan, "Rohan");
    make_string(&rohan_lc, "rOhAn");
    make_string(&gondor, "Gondor");
    make_string(&arnor, "Arnor");
    make_string(&goblins, "Goblins");
    make_string(&other, "Harad");
    make_string(&mordor, "Mordor");

    printf("group: S2 and S4 install together or not at all\n");
    {
        unsigned char before[TEXT_SIZE], saved = t[OFF_S4];
        memcpy(before, t, TEXT_SIZE);
        enabled[s2] = 1;
        fu_engine_run(&img, enabled, 0, r);
        CHECK(r[s2].status == FU_ST_GROUP_REFUSED && r[s4].status == FU_ST_OFF,
              "S4 off: [S2] %s, [S4] %s", fu_status_name(r[s2].status),
              fu_status_name(r[s4].status));
        enabled[s4] = 1;
        t[OFF_S4] = 0x90;
        before[OFF_S4] = 0x90;
        fu_engine_run(&img, enabled, 0, r);
        CHECK(r[s2].status == FU_ST_GROUP_REFUSED && r[s4].status == FU_ST_NOT_FOUND,
              "S4 missing: [S2] %s, [S4] %s", fu_status_name(r[s2].status),
              fu_status_name(r[s4].status));
        CHECK(memcmp(before, t, TEXT_SIZE) == 0, "a refused group changed the code");
        t[OFF_S4] = saved;
        enabled[s2] = enabled[s4] = 0;
    }

    printf("unhooked copies behave like the game\n");
    CHECK(lookup(&men.str) == 0, "Men -> %d", lookup(&men.str));
    CHECK(lookup(&goblins.str) == 5, "Goblins -> %d", lookup(&goblins.str));
    CHECK(lookup(&rohan.str) == 7, "Rohan -> %d", lookup(&rohan.str));
    CHECK(ranks(self, &rohan.str) == ((unsigned)(UINT_PTR)&rohan.str ^ 0x00ABC000u),
          "S3 copy returned 0x%08X", ranks(self, &rohan.str));
    g_seen_arg = g_seen_this = NULL;
    launch(&rohan.str, self);
    CHECK(g_seen_arg == &rohan.str && g_seen_this == self, "S4 copy did not reach the callee");

    printf("engine hooks the copies\n");
    fu_sides_clear();
    CHECK(fu_sides_add("Rohan", "Men") == NULL && fu_sides_add("Gondor", "Mordor") == NULL &&
              fu_sides_add("Arnor", "Goblins") == NULL,
          "aliases rejected");
    donor_men = fu_sides_resolve(&rohan.str);
    donor_mordor = fu_sides_resolve(&gondor.str);
    enabled[s1] = enabled[s2] = enabled[s3] = enabled[s4] = 1;
    memcpy(s1_prologue, t + OFF_S1, sizeof s1_prologue);
    fu_hook_reset_counts();
    fu_engine_run(&img, enabled, 0, r);
    CHECK(r[s1].status == FU_ST_APPLIED && r[s1].site_rva == TEXT_RVA + OFF_S1, "[S1] %s at 0x%X",
          fu_status_name(r[s1].status), r[s1].site_rva);
    CHECK(r[s2].status == FU_ST_APPLIED && r[s2].site_rva == TEXT_RVA + OFF_S2, "[S2] %s at 0x%X",
          fu_status_name(r[s2].status), r[s2].site_rva);
    CHECK(r[s3].status == FU_ST_APPLIED && r[s3].site_rva == TEXT_RVA + OFF_S3, "[S3] %s at 0x%X",
          fu_status_name(r[s3].status), r[s3].site_rva);
    CHECK(r[s4].status == FU_ST_APPLIED &&
              r[s4].site_rva == TEXT_RVA + OFF_S4 + fu_patch_defs[s4].site_offset,
          "[S4] %s at 0x%X", fu_status_name(r[s4].status), r[s4].site_rva);
    CHECK(t[OFF_S1] == 0xE9 && t[OFF_S1 + 5] == 0xCC && t[OFF_S1 + 8] == 0xCC, "S1 site bytes");
    CHECK(t[OFF_S3] == 0xE9 && t[OFF_S3 + 9] == 0xCC, "S3 site bytes");
    CHECK(t[OFF_S2] == 0xE9 && t[OFF_S2 + 5] == 0xE8, "S2 site bytes");
    CHECK(r[s4].hook_next == t + OFF_THUNK, "S4 does not continue at the old callee");
    CHECK(memcmp(r[s1].hook_next, s1_prologue, sizeof s1_prologue) == 0, "S1 trampoline bytes");
    t[OFF_S2 + 5] = 0xE9;
    put_rel(t + OFF_S2 + 6, (const void *)fake_find);

    printf("hooked copies resolve aliases\n");
    CHECK(lookup(&men.str) == 0, "Men -> %d", lookup(&men.str));
    CHECK(lookup(&rohan.str) == 0, "Rohan -> %d, want Men's 0", lookup(&rohan.str));
    CHECK(lookup(&rohan_lc.str) == 0, "rOhAn -> %d", lookup(&rohan_lc.str));
    CHECK(lookup(&gondor.str) == 4, "Gondor -> %d, want Mordor's 4", lookup(&gondor.str));
    CHECK(lookup(&arnor.str) == 5, "Arnor -> %d, want Goblins' 5", lookup(&arnor.str));
    CHECK(lookup(&goblins.str) == 5, "Goblins -> %d", lookup(&goblins.str));
    CHECK(lookup(&other.str) == 7, "Harad -> %d, want 7", lookup(&other.str));
    CHECK(strcmp(str_of(&rohan.str), "Rohan") == 0, "caller's string was modified");
    fu_hook_counts((unsigned)s1, &calls, &hits);
    CHECK(calls == 7 && hits == 4, "S1 counts %u/%u, want 7/4", calls, hits);

    CHECK(ranks(self, &gondor.str) == ((unsigned)(UINT_PTR)donor_mordor ^ 0x00ABC000u),
          "S3 did not see the donor");
    CHECK(ranks(self, &men.str) == ((unsigned)(UINT_PTR)&men.str ^ 0x00ABC000u),
          "S3 changed a vanilla Side");

    printf("hooked copies match AI players to the map's skirmish players\n");
    fu_skirmish_set_s2((unsigned)s2);
    fu_skirmish_set_query(fu_skirmish_engine_query);
    fu_skirmish_reset();
    elves = fu_sides_native_string(1);
    g_map[0] = "Elves";
    g_map[1] = "Mordor";
    g_map[2] = NULL;
    CHECK(fu_skirmish_engine_query(&mordor.str) && !fu_skirmish_engine_query(&men.str),
          "engine query through the S2 trampoline");

    memset(&player, 0, sizeof player);
    player.side = men.str; /* no SkirmishMen: the first game Side the map has */
    index = -1;
    CHECK(find(&player, &index) == 1 && index == 0, "S2 Men: %d", index);
    CHECK((const char *)g_find_self + FU_PLAYER_SIDE_OFF == (const char *)elves,
          "S2 Men ran as %s", str_of((const char *)g_find_self + FU_PLAYER_SIDE_OFF));
    player.side = mordor.str; /* own */
    CHECK(find(&player, &index) == 1 && index == 1 && g_find_self == &player, "S2 Mordor: %d",
          index);
    player.side = gondor.str; /* donor Mordor */
    CHECK(find(&player, &index) == 1 && index == 1 &&
              (const char *)g_find_self + FU_PLAYER_SIDE_OFF == (const char *)donor_mordor,
          "S2 Gondor: %d", index);
    player.side = arnor.str; /* donor Goblins missing: first game Side */
    CHECK(find(&player, &index) == 1 && index == 0, "S2 Arnor: %d", index);
    g_map[0] = NULL;
    player.side = men.str; /* nothing on the map: unchanged */
    CHECK(find(&player, &index) == 0 && g_find_self == &player, "S2 empty map");
    CHECK(strcmp(str_of(&player.side), "Men") == 0, "player's Side was modified");
    fu_hook_counts((unsigned)s2, &calls, &hits);
    CHECK(calls == 5 && hits == 3, "S2 counts %u/%u, want 5/3", calls, hits);

    g_map[0] = "Elves";
    g_seen_arg = g_seen_this = NULL;
    launch(&men.str, self);
    CHECK(g_seen_arg == elves && g_seen_this == self, "S4 Men: callee saw %s",
          str_of(g_seen_arg));
    launch(&mordor.str, self);
    CHECK(g_seen_arg == &mordor.str, "S4 changed a Side the map has");
    launch(&gondor.str, self);
    CHECK(g_seen_arg == donor_mordor, "S4 Gondor: %s", str_of(g_seen_arg));
    g_map[0] = "Men";
    launch(&rohan.str, self);
    CHECK(g_seen_arg == donor_men, "S4 Rohan: %s", str_of(g_seen_arg));
    g_map[0] = g_map[1] = NULL;
    launch(&other.str, self);
    CHECK(g_seen_arg == &other.str, "S4 changed a Side with nothing to match");
    fu_hook_counts((unsigned)s4, &calls, &hits);
    CHECK(calls == 5 && hits == 3, "S4 counts %u/%u, want 5/3", calls, hits);
    fu_skirmish_set_query(NULL);

    printf("registers and flags survive the stub\n");
    {
        /* jmp-hook a `mov eax,esi; ret` body by hand: ebx/esi/edi/ebp and
         * DF-free flags must arrive unchanged. */
        unsigned out[4] = { 0 }; /* out[0] doubles as a null AsciiString for the dispatcher */
        unsigned char *fn = t + 0x400, *tr;
        int k = s1;
        memcpy(fn, "\x8B\x4C\x24\x04\x68\x00\x00\x00\x00\x58\x89\x19\x89\x71\x04\x89\x79\x08"
                   "\x9C\x58\x89\x41\x0C\xC3",
               24);
        /* mov ecx,[esp+4]; push 0; pop eax; mov [ecx],ebx; mov [ecx+4],esi;
         * mov [ecx+8],edi; pushfd; pop eax; mov [ecx+12],eax; ret */
        tr = fu_hook_trampoline(fn, 9, fn + 9);
        fu_hook_set_next((unsigned)k, tr);
        fn[0] = 0xE9;
        put_rel(fn + 1, fu_hook_stub((unsigned)k));
        reg_probe(fn, out);
        CHECK(out[0] == 0x11223344 && out[1] == 0x55667788 && out[2] == 0x99AABBCC,
              "callee-saved registers changed: %08X %08X %08X", out[0], out[1], out[2]);
        CHECK(out[3] & 1, "carry flag lost");
        fu_hook_set_next((unsigned)k, r[s1].hook_next);
    }

    printf("second run refuses hooked sites\n");
    {
        unsigned char before[TEXT_SIZE];
        memcpy(before, t, TEXT_SIZE);
        fu_engine_run(&img, enabled, 0, r);
        CHECK(r[s1].status == FU_ST_NOT_FOUND, "[S1] %s", fu_status_name(r[s1].status));
        CHECK(r[s2].status == FU_ST_NOT_FOUND, "[S2] %s", fu_status_name(r[s2].status));
        CHECK(r[s3].status == FU_ST_NOT_FOUND, "[S3] %s", fu_status_name(r[s3].status));
        CHECK(r[s4].status == FU_ST_BAD_TARGET, "[S4] %s", fu_status_name(r[s4].status));
        CHECK(memcmp(before, t, TEXT_SIZE) == 0, "second run changed the code");
    }
    CHECK(lookup(&rohan.str) == 0, "hooks broken after the second run");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: %s <game.dat>\n", argv[0]);
        return 2;
    }
    if (!load_file(argv[1]) || !at_rva(0x0033A3F4)) {
        printf("cannot read %s\n", argv[1]);
        return 2;
    }
    test_parser();
    test_resolver();
    test_execution();
    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: hook tests passed\n");
    return 0;
}
