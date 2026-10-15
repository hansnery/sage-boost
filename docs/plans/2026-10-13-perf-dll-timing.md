# Performance DLL, plan 3: Milestone 1 (timing)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** plugin 0.2.0 times the client frame, the logic update, the scene
render and the skeleton update (split into during the render and elsewhere)
and writes `bfme2perf_timing.csv`, one row per second, without changing what
the game does.

**Architecture:** five entry hooks, found by signature in the running
`game.dat`. Each site's first bytes become a `jmp` to an entry stub. On the
main thread the stub swaps the caller's return address for an exit stub and
starts a timer; the stolen bytes run from a trampoline. When the function
returns into the exit stub, the timer stops and the stub returns to the real
caller with every register, the flags and the return value as the function
left them. Nested calls of the same group (an HLod updating its sub-objects)
pass straight through, so only the outermost call is timed. A shadow stack
records each wrapped call's return-address slot, so an exception that
unwinds past a wrapped call only leaves a stale entry, which the next exit
drops.

**Tech Stack:** C (freestanding, 32-bit MinGW-w64, kernel32 only, GNU
assembler for the stubs), GNU make from Git Bash, test programs with the
static C runtime.

Spec: `docs/specs/2026-10-13-perf-dll-design.md` (Milestone 1, R0–R6).

## Global Constraints

- Nothing under `C:\repositories\Open-BFME-2\` is written.
- No code from SAGE-Unlocked (GPL-3) or bfme2-accelerator (MIT).
- `game.dat` and other EA assets are never redistributed; the DLL ships alone and changes nothing on disk.
- No hardcoded absolute addresses: every hook is found by signature in the running executable.
- The faction unlocker is a separate project: nothing in `dll/` changes; code needed from it is copied into `perf/`.
- The build stays freestanding (`-ffreestanding -nostdlib`, kernel32 only) with `-Werror`.
- No behaviour change in the game: the hooks only read the clock and count.
- The only files written in the game folder are `mss\bfme2perf.asi` and `mss\bfme2perf.ini`.
- Conventional commits; branch `feat/perf-dll`; don't push.

## Sites (AotR = 2.01 RVAs, from the R1 findings plus `RTS3DScene::Render`)

| Id | Function | RVA | Steal | `ret` pops | Group |
|---|---|---|---|---|---|
| `frame` | `GameEngine::update` | 0x002325A0 | 5 (`55 8B EC 51 53`) | 0 | 1, required |
| `logic` | `GameLogic::update(int)` | 0x0022E4E8 | 5 (`B8 imm32`) | 4 | 2 |
| `render` | `RTS3DScene::Render` (wraps `Customized_Render` and `Flush`) | 0x0007177E | 6 (`55 8B EC 83 EC 28`) | 4 | 3 |
| `skel_hlod` | `HLodClass::Update_Sub_Object_Transforms` | 0x0019CD70 | 5 (`83 EC 4C 53 55`) | 0 | 4 |
| `skel_anim` | `Animatable3DObjClass::Update_Sub_Object_Transforms` | 0x001A5050 | 6 (`83 EC 30 56 8B F1`) | 0 | 4 |

No branch lands inside any stolen range (checked with
`verify_sites.branches_into`). `RTS3DScene::Render` is 1.06 0x00071E40;
its AotR signature (unique in `game.dat` and `game820.dat`) is
`55 8B EC 83 EC 28 53 56 8B 35 ?? ?? ?? ?? 85 F6 57 8B F9 89 7D E4 0F 84`.

## CSV columns

One row per second of play, from the frame hook's exit. ms columns are
per-frame averages over that second.

| Column | Meaning |
|---|---|
| `time_s` | seconds since the first frame, at the row's start |
| `frames` | client frames in the row |
| `frame_avg_ms`, `frame_max_ms` | time inside `GameEngine::update` (the work of one frame, not the frame limiter's wait) |
| `logic_ms` | time in `GameLogic::update` (all its phases in that frame) |
| `render_ms` | time in `RTS3DScene::Render`: choosing what to draw, the skeleton updates it triggers, and the draw calls (`Flush`) |
| `skel_render_ms` | outermost skeleton updates during the render |
| `skel_other_ms` | outermost skeleton updates elsewhere (effects, attachments, logic queries) |
| `anim_on_screen` | outermost skeleton updates during the render per frame: the animated objects whose pose the draw computed (static, already-posed objects aren't counted) |
| `to_workers`, `main_thread` | 0 in this milestone |
| `skel_us_per_obj` | `skel_render` time per counted object, in microseconds |

A column whose hook isn't installed is empty.

## File structure

| File | Responsibility |
|---|---|
| `perf/src/scan.c`, `scan.h` | Pattern parse and scan over `.text`, PE section lookup, CRC32 (copied and trimmed from `dll/src/engine.c`) |
| `perf/src/sites.c`, `sites.h` | The five sites |
| `perf/src/patch.c`, `patch.h` | Locate all, group all-or-nothing, trampolines, write the `jmp` |
| `perf/src/stubs.c`, `stubs.h` | Assembly entry and exit stubs |
| `perf/src/timing.c`, `timing.h` | Shadow stack, attribution, per-second rows |
| `perf/src/csvrow.c`, `csvrow.h` | Row formatting (integer only) |
| `perf/src/config.c`, `config.h` | `bfme2perf.ini`, game detection |
| `perf/src/main.c` | `DllMain`, log, wiring; version 0.2.0 |
| `perf/bfme2perf.ini` | Default settings shipped with `make dist` |
| `perf/tests/test_*.c` | `test_row`, `test_config`, `test_sites`, `test_hooks`, `test_load` |

---

### Task 1: scanner and sites

**Files:**
- Create: `perf/src/scan.h`, `perf/src/scan.c`, `perf/src/sites.h`, `perf/src/sites.c`, `perf/tests/test_sites.c`
- Modify: `perf/Makefile`

**Interfaces:**
- Produces: `pf_pattern`, `pf_image { base, text_rva, text_size, timestamp }`; `int pf_parse_pattern(const char *, pf_pattern *)`; `int pf_pattern_at(const unsigned char *, const pf_pattern *)`; `unsigned pf_scan(const pf_image *, const pf_pattern *, unsigned *rva)`; `int pf_image_open(pf_image *, void *module_base)`; `unsigned pf_crc32(const unsigned char *, unsigned)`. `enum { PF_FRAME, PF_LOGIC, PF_RENDER, PF_SKEL_HLOD, PF_SKEL_ANIM, PF_SITE_COUNT }`; `enum { PF_GROUP_FRAME = 1, PF_GROUP_LOGIC, PF_GROUP_RENDER, PF_GROUP_SKEL }`; `pf_site { id, function, signature, steal, arg_bytes, group, rva_201 }`; `extern const pf_site pf_sites[PF_SITE_COUNT]`.

- [x] **Step 1: Write `perf/tests/test_sites.c`**

```c
/* Every timing site's signature matches exactly once in Age of the Ring's
 * game.dat and in ROTWK 2.01, at the expected RVA, with the expected bytes
 * in its stolen prologue. Images not given are skipped.
 * Usage: test_sites.exe [--aotr <game.dat>] [--rotwk <game820.dat>] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "scan.h"
#include "sites.h"

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
    "55 8B EC 51 53",
    "B8 DA 41 B8 00",
    "55 8B EC 83 EC 28",
    "83 EC 4C 53 55",
    "83 EC 30 56 8B F1",
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
        printf("  %-9s %u match(es) at %08X\n", s->id, n, rva);
        CHECK(n == 1, "%s: %u matches", s->id, n);
        CHECK(rva == s->rva_201, "%s: at %08X, expected %08X", s->id, rva, s->rva_201);
        CHECK(n == 1 && pf_pattern_at(mem + rva, &pro), "%s: stolen bytes differ", s->id);
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
```

- [x] **Step 2: Add the test to `perf/Makefile`**

After the `TEST_CFLAGS` line add:

```make
AOTR_DAT  ?= C:/Jogos/AgeoftheRing/rotwk/game.dat
ROTWK_DAT ?= C:/Jogos/AgeoftheRing/rotwk/game820.dat
SITE_TEST_ARGS := $(if $(wildcard $(AOTR_DAT)),--aotr "$(AOTR_DAT)") \
                  $(if $(wildcard $(ROTWK_DAT)),--rotwk "$(ROTWK_DAT)")
```

After the `test_load.exe` rule add:

```make
$(BUILD)/test_sites.exe: tests/test_sites.c src/scan.c src/sites.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)
```

and make `test` depend on and run it before `test_load`:

```make
test: all $(BUILD)/test_sites.exe $(BUILD)/test_load.exe
	./$(BUILD)/test_sites.exe $(SITE_TEST_ARGS)
	./$(BUILD)/test_load.exe "$(BUILD)/bfme2perf.asi"
```

- [x] **Step 3: Run it to see it fail**

Run: `mingw32-make test` (Git Bash, `PATH=/c/tools/winlibs-i686/mingw32/bin:$PATH`, in `perf/`).
Expected: `scan.h: No such file or directory`.

- [x] **Step 4: Write `perf/src/scan.h`**

```c
/* Signature scanner over a loaded PE image's code section. CRT-free. */
#ifndef PF_SCAN_H
#define PF_SCAN_H

#include <windows.h>

#define PF_MAX_PATTERN 96

typedef struct {
    unsigned len;
    unsigned char bytes[PF_MAX_PATTERN];
    unsigned char fixed[PF_MAX_PATTERN]; /* 1 = must match, 0 = wildcard */
} pf_pattern;

typedef struct {
    unsigned char *base;
    unsigned text_rva;
    unsigned text_size;
    unsigned timestamp;
} pf_image;

/* "8B ?? 0F" -> pattern. Returns 0 on malformed input. */
int pf_parse_pattern(const char *s, pf_pattern *p);
int pf_pattern_at(const unsigned char *at, const pf_pattern *p);
/* Matches of p in the code section; *rva gets the first one's start. */
unsigned pf_scan(const pf_image *img, const pf_pattern *p, unsigned *rva);
/* Fills img from an in-memory PE image's executable ".text" section.
 * Returns 0 if the headers are not usable. */
int pf_image_open(pf_image *img, void *module_base);
unsigned pf_crc32(const unsigned char *p, unsigned n);

#endif
```

- [x] **Step 5: Write `perf/src/scan.c`**

```c
#include "scan.h"

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int pf_parse_pattern(const char *s, pf_pattern *p)
{
    p->len = 0;
    for (;;) {
        while (*s == ' ')
            s++;
        if (!*s)
            return p->len > 0;
        if (p->len == PF_MAX_PATTERN)
            return 0;
        if (s[0] == '?' && s[1] == '?') {
            p->bytes[p->len] = 0;
            p->fixed[p->len] = 0;
        } else {
            int hi = hex_nibble(s[0]), lo = hi < 0 ? -1 : hex_nibble(s[1]);
            if (lo < 0)
                return 0;
            p->bytes[p->len] = (unsigned char)(hi << 4 | lo);
            p->fixed[p->len] = 1;
        }
        p->len++;
        s += 2;
        if (*s && *s != ' ')
            return 0;
    }
}

int pf_pattern_at(const unsigned char *at, const pf_pattern *p)
{
    unsigned i;
    for (i = 0; i < p->len; i++)
        if (p->fixed[i] && at[i] != p->bytes[i])
            return 0;
    return 1;
}

unsigned pf_scan(const pf_image *img, const pf_pattern *p, unsigned *rva)
{
    const unsigned char *start = img->base + img->text_rva;
    unsigned anchor = 0, count = 0, i;

    while (anchor < p->len && !p->fixed[anchor])
        anchor++;
    if (anchor == p->len || img->text_size < p->len)
        return 0;
    for (i = 0; i + p->len <= img->text_size; i++) {
        if (start[i + anchor] != p->bytes[anchor] || !pf_pattern_at(start + i, p))
            continue;
        if (count++ == 0)
            *rva = img->text_rva + i;
    }
    return count;
}

int pf_image_open(pf_image *img, void *module_base)
{
    unsigned char *base = (unsigned char *)module_base;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS32 *nt;
    IMAGE_SECTION_HEADER *sec;
    unsigned i;

    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;
    nt = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return 0;
    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        static const char text[8] = ".text";
        unsigned k;
        for (k = 0; k < 8 && sec->Name[k] == (unsigned char)text[k]; k++)
            ;
        if (k < 8 || !(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE))
            continue;
        img->base = base;
        img->text_rva = sec->VirtualAddress;
        img->text_size = sec->Misc.VirtualSize;
        img->timestamp = nt->FileHeader.TimeDateStamp;
        return img->text_size > 0;
    }
    return 0;
}

unsigned pf_crc32(const unsigned char *p, unsigned n)
{
    static unsigned table[256];
    unsigned c, i, k;

    if (!table[1]) {
        for (i = 0; i < 256; i++) {
            c = i;
            for (k = 0; k < 8; k++)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    c = 0xFFFFFFFFu;
    for (i = 0; i < n; i++)
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
```

- [x] **Step 6: Write `perf/src/sites.h`**

```c
/* The functions the timing hooks wrap, found by signature at their entry. */
#ifndef PF_SITES_H
#define PF_SITES_H

enum { PF_FRAME, PF_LOGIC, PF_RENDER, PF_SKEL_HLOD, PF_SKEL_ANIM, PF_SITE_COUNT };

/* A group's hooks are installed together or not at all. */
enum { PF_GROUP_FRAME = 1, PF_GROUP_LOGIC, PF_GROUP_RENDER, PF_GROUP_SKEL, PF_GROUP_COUNT };

typedef struct {
    const char *id;
    const char *function;
    const char *signature; /* starts at the function's first byte */
    unsigned steal;        /* prologue bytes run from the trampoline: whole, position-independent instructions */
    unsigned arg_bytes;    /* bytes the function's ret pops */
    unsigned group;
    unsigned rva_201;      /* ROTWK 2.01 and Age of the Ring; log and tests only */
} pf_site;

extern const pf_site pf_sites[PF_SITE_COUNT];

#endif
```

- [x] **Step 7: Write `perf/src/sites.c`**

```c
#include "sites.h"

const pf_site pf_sites[PF_SITE_COUNT] = {
    { "frame", "GameEngine::update",
      "55 8B EC 51 53 56 8B F1 8B 0D ?? ?? ?? ?? 8B 01 FF 50 28 8B 0D ?? ?? ??",
      5, 0, PF_GROUP_FRAME, 0x002325A0 },
    { "logic", "GameLogic::update",
      "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 81 EC 74 01 00 00 53 56 8B F1 57 8D 4D D8",
      5, 4, PF_GROUP_LOGIC, 0x0022E4E8 },
    { "render", "RTS3DScene::Render",
      "55 8B EC 83 EC 28 53 56 8B 35 ?? ?? ?? ?? 85 F6 57 8B F9 89 7D E4 0F 84",
      6, 4, PF_GROUP_RENDER, 0x0007177E },
    { "skel_hlod", "HLodClass::Update_Sub_Object_Transforms",
      "83 EC 4C 53 55 56 57 8B D9 E8 ?? ?? ?? ?? 33 C9 39 8B 20 01 00 00 89 4C",
      5, 0, PF_GROUP_SKEL, 0x0019CD70 },
    { "skel_anim", "Animatable3DObjClass::Update_Sub_Object_Transforms",
      "83 EC 30 56 8B F1 8B 8E FC 00 00 00 85 C9 74 71 80 B9 F4 00 00 00 00 75",
      6, 0, PF_GROUP_SKEL, 0x001A5050 },
};
```

- [x] **Step 8: Run the tests to see them pass**

Run: `mingw32-make test`
Expected: `OK: site tests passed` with each site at its RVA once in both
images, then `OK: load tests passed`.

- [x] **Step 9: Commit** `feat(perf): signature scanner and the timing sites`

### Task 2: settings

**Files:**
- Create: `perf/src/config.h`, `perf/src/config.c`, `perf/tests/test_config.c`
- Modify: `perf/Makefile`

**Interfaces:**
- Produces: `pf_config { ini_found, enabled, log, timing, threads, verify }`; `void pf_config_load(pf_config *, const WCHAR *ini)`; `int pf_parse_int(const WCHAR *, int def)`; `const char *pf_detect_game(const WCHAR *command_line, const WCHAR *exe_path)` returning `"Realms in Exile"`, `"Age of the Ring"` or `"unknown"`.

`GetPrivateProfileIntW` returns 0 for negative values, so `Threads=-1`
needs the string read and parsed here.

- [x] **Step 1: Write `perf/tests/test_config.c`**

```c
/* bfme2perf.ini parsing and game detection.
 * Usage: test_config.exe (writes INIs in %TEMP%\bfme2perf-config-test) */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#include "config.h"

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

static WCHAR g_ini[MAX_PATH];

static void write_ini(const char *text)
{
    HANDLE h = CreateFileW(g_ini, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD put;
    WriteFile(h, text, (DWORD)strlen(text), &put, NULL);
    CloseHandle(h);
}

int main(void)
{
    WCHAR dir[MAX_PATH];
    pf_config c;

    GetTempPathW(MAX_PATH, dir);
    wcscat(dir, L"bfme2perf-config-test");
    CreateDirectoryW(dir, NULL);
    swprintf(g_ini, MAX_PATH, L"%ls\\bfme2perf.ini", dir);

    printf("pf_parse_int\n");
    CHECK(pf_parse_int(L"0", 7) == 0, "0");
    CHECK(pf_parse_int(L" 42 ", 7) == 42, "spaces");
    CHECK(pf_parse_int(L"-1", 7) == -1, "-1");
    CHECK(pf_parse_int(L"", 7) == 7, "empty");
    CHECK(pf_parse_int(L"abc", 7) == 7, "abc");
    CHECK(pf_parse_int(L"3x", 7) == 7, "3x");
    CHECK(pf_parse_int(L"-", 7) == 7, "-");

    printf("missing file\n");
    DeleteFileW(g_ini);
    pf_config_load(&c, g_ini);
    CHECK(!c.ini_found && c.enabled == 1 && c.log == 1 && c.timing == 1 && c.threads == 0 &&
              c.verify == 0,
          "defaults: found=%d enabled=%d log=%d timing=%d threads=%d verify=%d", c.ini_found,
          c.enabled, c.log, c.timing, c.threads, c.verify);

    printf("every key set\n");
    write_ini("[Perf]\r\nEnabled=0\r\nLog=0\r\n[Timing]\r\nEnabled=0\r\n"
              "[Skeleton]\r\nThreads=-1\r\nVerify=1\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.ini_found && c.enabled == 0 && c.log == 0 && c.timing == 0 && c.threads == -1 &&
              c.verify == 1,
          "set: found=%d enabled=%d log=%d timing=%d threads=%d verify=%d", c.ini_found,
          c.enabled, c.log, c.timing, c.threads, c.verify);

    printf("out of range and junk\n");
    write_ini("[Skeleton]\r\nThreads=9\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.threads == 4, "Threads=9 -> %d", c.threads);
    write_ini("[Skeleton]\r\nThreads=-5\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.threads == 0, "Threads=-5 -> %d", c.threads);
    write_ini("[Skeleton]\r\nThreads=two\r\n[Perf]\r\nEnabled=yes\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.threads == 0 && c.enabled == 1, "junk -> threads=%d enabled=%d", c.threads, c.enabled);

    printf("game detection\n");
    CHECK(!strcmp(pf_detect_game(L"\"C:\\Jogos\\AgeoftheRing\\rotwk\\game.dat\" -mod "
                                 L"\"C:\\Program Files (x86)\\Realms in Exile Nightly\\x\"",
                                 L"C:\\Jogos\\AgeoftheRing\\rotwk\\game.dat"),
                  "Realms in Exile"),
          "Realms");
    CHECK(!strcmp(pf_detect_game(L"game.dat", L"C:\\Jogos\\AgeoftheRing\\rotwk\\game.dat"),
                  "Age of the Ring"),
          "AotR");
    CHECK(!strcmp(pf_detect_game(L"x.exe", L"C:\\tmp\\x.exe"), "unknown"), "unknown");

    DeleteFileW(g_ini);
    RemoveDirectoryW(dir);
    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: config tests passed\n");
    return 0;
}
```

- [x] **Step 2: Add to `perf/Makefile`**

```make
$(BUILD)/test_config.exe: tests/test_config.c src/config.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)
```

and in `test`: depend on `$(BUILD)/test_config.exe` and run `./$(BUILD)/test_config.exe` first.

- [x] **Step 3: Run it to see it fail** — `config.h: No such file or directory`.

- [x] **Step 4: Write `perf/src/config.h`**

```c
/* bfme2perf.ini next to the plugin. A missing file means "all defaults". */
#ifndef PF_CONFIG_H
#define PF_CONFIG_H

#include <windows.h>

typedef struct {
    int ini_found;
    int enabled; /* [Perf] Enabled, default 1 */
    int log;     /* [Perf] Log, default 1 */
    int timing;  /* [Timing] Enabled, default 1 */
    int threads; /* [Skeleton] Threads, -1..4, default 0 */
    int verify;  /* [Skeleton] Verify, default 0 */
} pf_config;

void pf_config_load(pf_config *cfg, const WCHAR *ini_path);

/* Decimal with an optional '-', spaces around it allowed; def otherwise. */
int pf_parse_int(const WCHAR *s, int def);

const char *pf_detect_game(const WCHAR *command_line, const WCHAR *exe_path);

#endif
```

- [x] **Step 5: Write `perf/src/config.c`**

```c
#include "config.h"

int pf_parse_int(const WCHAR *s, int def)
{
    int neg = 0, any = 0;
    unsigned v = 0;

    while (*s == L' ' || *s == L'\t')
        s++;
    if (*s == L'-') {
        neg = 1;
        s++;
    }
    while (*s >= L'0' && *s <= L'9') {
        if (v < 100000000u)
            v = v * 10 + (unsigned)(*s - L'0');
        s++;
        any = 1;
    }
    while (*s == L' ' || *s == L'\t')
        s++;
    if (!any || *s)
        return def;
    return neg ? -(int)v : (int)v;
}

static int read_int(const WCHAR *section, const WCHAR *key, int def, const WCHAR *path)
{
    WCHAR buf[32];
    GetPrivateProfileStringW(section, key, L"", buf, 32, path);
    return pf_parse_int(buf, def);
}

void pf_config_load(pf_config *cfg, const WCHAR *ini_path)
{
    cfg->ini_found = GetFileAttributesW(ini_path) != INVALID_FILE_ATTRIBUTES;
    cfg->enabled = read_int(L"Perf", L"Enabled", 1, ini_path) != 0;
    cfg->log = read_int(L"Perf", L"Log", 1, ini_path) != 0;
    cfg->timing = read_int(L"Timing", L"Enabled", 1, ini_path) != 0;
    cfg->threads = read_int(L"Skeleton", L"Threads", 0, ini_path);
    if (cfg->threads < -1)
        cfg->threads = 0;
    if (cfg->threads > 4)
        cfg->threads = 4;
    cfg->verify = read_int(L"Skeleton", L"Verify", 0, ini_path) != 0;
}

static WCHAR lower(WCHAR c)
{
    return c >= L'A' && c <= L'Z' ? (WCHAR)(c - L'A' + L'a') : c;
}

static int contains(const WCHAR *hay, const WCHAR *needle)
{
    unsigned i, k;
    if (!hay)
        return 0;
    for (i = 0; hay[i]; i++) {
        for (k = 0; needle[k] && lower(hay[i + k]) == needle[k]; k++)
            ;
        if (!needle[k])
            return 1;
    }
    return 0;
}

const char *pf_detect_game(const WCHAR *command_line, const WCHAR *exe_path)
{
    if (contains(command_line, L"realms"))
        return "Realms in Exile";
    if (contains(command_line, L"aotr") || contains(exe_path, L"ageofthering") ||
        contains(exe_path, L"age of the ring"))
        return "Age of the Ring";
    return "unknown";
}
```

- [x] **Step 6: Run the tests to see them pass** — `OK: config tests passed`.

- [x] **Step 7: Commit** `feat(perf): read bfme2perf.ini and detect the game`

### Task 3: the CSV row

**Files:**
- Create: `perf/src/csvrow.h`, `perf/src/csvrow.c`, `perf/tests/test_row.c`
- Modify: `perf/Makefile`

**Interfaces:**
- Produces: `PF_HAVE_LOGIC 1`, `PF_HAVE_RENDER 2`, `PF_HAVE_SKEL 4`; `pf_row { time_s, frames, frame_us, frame_max_us, logic_us, render_us, skel_render_us, skel_other_us, skel_render_calls, to_workers, main_thread, have }` (µs fields `unsigned long long`); `extern const char pf_csv_header[]`; `unsigned pf_format_row(const pf_row *, char *out, unsigned cap)`.

- [x] **Step 1: Write `perf/tests/test_row.c`**

```c
/* The CSV header and row formatter. */
#include <stdio.h>
#include <string.h>

#include "csvrow.h"

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

int main(void)
{
    char buf[256];
    unsigned n;
    pf_row r;

    CHECK(!strcmp(pf_csv_header,
                  "time_s,frames,frame_avg_ms,frame_max_ms,logic_ms,render_ms,skel_render_ms,"
                  "skel_other_ms,anim_on_screen,to_workers,main_thread,skel_us_per_obj\r\n"),
          "header");

    memset(&r, 0, sizeof r);
    r.time_s = 3;
    r.frames = 30;
    r.frame_us = 500010;
    r.frame_max_us = 25000;
    r.logic_us = 60000;
    r.render_us = 300000;
    r.skel_render_us = 90000;
    r.skel_other_us = 3000;
    r.skel_render_calls = 12000;
    r.have = PF_HAVE_LOGIC | PF_HAVE_RENDER | PF_HAVE_SKEL;
    n = pf_format_row(&r, buf, sizeof buf);
    CHECK(n == strlen(buf) &&
              !strcmp(buf, "3,30,16.667,25.000,2.000,10.000,3.000,0.100,400,0,0,7.50\r\n"),
          "full row: %s", buf);

    memset(&r, 0, sizeof r);
    r.time_s = 7;
    r.frames = 30;
    r.frame_us = 300000;
    r.frame_max_us = 12345;
    n = pf_format_row(&r, buf, sizeof buf);
    CHECK(n && !strcmp(buf, "7,30,10.000,12.345,,,,,,0,0,\r\n"), "frame only: %s", buf);

    r.have = PF_HAVE_SKEL;
    n = pf_format_row(&r, buf, sizeof buf);
    CHECK(n && !strcmp(buf, "7,30,10.000,12.345,,,0.000,0.000,0,0,0,\r\n"),
          "no skeleton calls: %s", buf);

    CHECK(pf_format_row(&r, buf, 10) == 0, "too small a buffer is refused");

    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: row tests passed\n");
    return 0;
}
```

- [x] **Step 2: Add to `perf/Makefile`**

```make
$(BUILD)/test_row.exe: tests/test_row.c src/csvrow.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)
```

and run `./$(BUILD)/test_row.exe` first in `test`.

- [x] **Step 3: Run it to see it fail** — `csvrow.h: No such file or directory`.

- [x] **Step 4: Write `perf/src/csvrow.h`**

```c
/* One second of timing as a CSV row. Integer only. */
#ifndef PF_CSVROW_H
#define PF_CSVROW_H

#define PF_HAVE_LOGIC 1u
#define PF_HAVE_RENDER 2u
#define PF_HAVE_SKEL 4u

typedef struct {
    unsigned time_s;
    unsigned frames;
    unsigned long long frame_us, frame_max_us;
    unsigned long long logic_us, render_us;
    unsigned long long skel_render_us, skel_other_us;
    unsigned skel_render_calls; /* outermost skeleton updates during the render */
    unsigned to_workers, main_thread;
    unsigned have;              /* PF_HAVE_*: columns that were measured */
} pf_row;

extern const char pf_csv_header[];

/* NUL- and "\r\n"-terminated row; columns not measured are empty. Returns
 * its length, or 0 if it doesn't fit in cap. */
unsigned pf_format_row(const pf_row *r, char *out, unsigned cap);

#endif
```

- [x] **Step 5: Write `perf/src/csvrow.c`**

```c
#include "csvrow.h"

const char pf_csv_header[] =
    "time_s,frames,frame_avg_ms,frame_max_ms,logic_ms,render_ms,skel_render_ms,"
    "skel_other_ms,anim_on_screen,to_workers,main_thread,skel_us_per_obj\r\n";

typedef struct {
    char *p;
    unsigned left;
    int full;
} out_buf;

static void put_c(out_buf *o, char c)
{
    if (o->left > 1) {
        *o->p++ = c;
        o->left--;
    } else {
        o->full = 1;
    }
}

static void put_u(out_buf *o, unsigned long long v)
{
    char tmp[24];
    unsigned n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        put_c(o, tmp[--n]);
}

/* v / 10^digits, with `digits` decimals. */
static void put_fixed(out_buf *o, unsigned long long v, unsigned digits)
{
    unsigned long long scale = 1;
    unsigned i;
    for (i = 0; i < digits; i++)
        scale *= 10;
    put_u(o, v / scale);
    put_c(o, '.');
    v %= scale;
    for (i = 0; i < digits; i++) {
        scale /= 10;
        put_c(o, (char)('0' + v / scale));
        v %= scale;
    }
}

static void per_frame_ms(out_buf *o, unsigned long long us, unsigned frames)
{
    put_fixed(o, (us + frames / 2) / frames, 3);
}

unsigned pf_format_row(const pf_row *r, char *out, unsigned cap)
{
    out_buf o = { out, cap, 0 };
    unsigned f = r->frames ? r->frames : 1;
    int skel = (r->have & PF_HAVE_SKEL) != 0;

    if (cap == 0)
        return 0;
    put_u(&o, r->time_s);
    put_c(&o, ',');
    put_u(&o, r->frames);
    put_c(&o, ',');
    per_frame_ms(&o, r->frame_us, f);
    put_c(&o, ',');
    put_fixed(&o, r->frame_max_us, 3);
    put_c(&o, ',');
    if (r->have & PF_HAVE_LOGIC)
        per_frame_ms(&o, r->logic_us, f);
    put_c(&o, ',');
    if (r->have & PF_HAVE_RENDER)
        per_frame_ms(&o, r->render_us, f);
    put_c(&o, ',');
    if (skel)
        per_frame_ms(&o, r->skel_render_us, f);
    put_c(&o, ',');
    if (skel)
        per_frame_ms(&o, r->skel_other_us, f);
    put_c(&o, ',');
    if (skel)
        put_u(&o, (r->skel_render_calls + f / 2) / f);
    put_c(&o, ',');
    put_u(&o, r->to_workers);
    put_c(&o, ',');
    put_u(&o, r->main_thread);
    put_c(&o, ',');
    if (skel && r->skel_render_calls)
        put_fixed(&o, (r->skel_render_us * 100 + r->skel_render_calls / 2) / r->skel_render_calls,
                  2);
    put_c(&o, '\r');
    put_c(&o, '\n');
    if (o.full)
        return 0;
    *o.p = 0;
    return cap - o.left;
}
```

- [x] **Step 6: Run the tests to see them pass** — `OK: row tests passed`.

- [x] **Step 7: Commit** `feat(perf): format a timing row`

### Task 4: hooks, stubs and timing

**Files:**
- Create: `perf/src/patch.h`, `perf/src/patch.c`, `perf/src/stubs.h`, `perf/src/stubs.c`, `perf/src/timing.h`, `perf/src/timing.c`, `perf/tests/test_hooks.c`
- Modify: `perf/Makefile`

**Interfaces:**
- Consumes: Task 1's scanner and `pf_site`; Task 3's `pf_row`, `pf_format_row`, `pf_csv_header`.
- Produces:
  - `pf_status` (`PF_ST_OFF`, `PF_ST_BAD_DEF`, `PF_ST_NOT_FOUND`, `PF_ST_AMBIGUOUS`, `PF_ST_GROUP_REFUSED`, `PF_ST_NO_MEMORY`, `PF_ST_PROTECT_FAILED`, `PF_ST_LOCATED`, `PF_ST_APPLIED`), `pf_result { status, matches, rva, trampoline }`, `void pf_install(const pf_image *, const pf_site *, unsigned count, const int *enabled, unsigned required_group, void *const *stubs, void (*set_next)(unsigned, void *), pf_result *)`, `const char *pf_status_name(pf_status)`.
  - `extern void *const pf_enter_stubs[PF_SITE_COUNT]`, `pf_exit_stubs[PF_SITE_COUNT]`.
  - `int pf_timing_open(const pf_site *sites, const WCHAR *csv_path)`, `void pf_timing_set_next(unsigned k, void *trampoline)`, `void pf_timing_set_installed(unsigned mask)`, `void pf_timing_discard(const WCHAR *csv_path)`, `void pf_timing_flush(void)`, `void pf_timing_get_stats(pf_timing_stats *)` with `pf_timing_stats { frames, logic_calls, render_calls, skel_render_calls, skel_other_calls, passed_through, stale, rows }`; and `void *pf_enter(unsigned k, void **slot)`, `void *pf_exit(unsigned k, unsigned char *esp)` (cdecl, called from the stubs only).

- [x] **Step 1: Write `perf/tests/test_hooks.c`**

```c
/* Installs the timing hooks on five small assembly functions in this exe
 * (prologues shaped like the game's), calls them and checks the return
 * values, registers, call counts and the CSV row. */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#include "patch.h"
#include "stubs.h"
#include "timing.h"

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

/* frame calls logic(7), render(9) and anim; render calls hlod, hlod calls
 * anim. Each marker `mov eax, 0x5EED000n` makes its signature unique. */
__asm__(".text\n"
        ".p2align 4\n"
        "_fake_frame:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    pushl %ecx\n"
        "    pushl %ebx\n"
        "    movl $0x5EED0001, %eax\n"
        "    pushl $7\n"
        "    call _fake_logic\n"
        "    pushl $9\n"
        "    call _fake_render\n"
        "    call _fake_anim\n"
        "    movl $41, %eax\n"
        "    popl %ebx\n"
        "    popl %ecx\n"
        "    popl %ebp\n"
        "    ret\n"
        ".p2align 4\n"
        "_fake_logic:\n"
        "    movl $0x5EED0002, %eax\n"
        "    movl 4(%esp), %eax\n"
        "    addl $1, %eax\n"
        "    ret $4\n"
        ".p2align 4\n"
        "_fake_render:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    subl $0x28, %esp\n"
        "    movl $0x5EED0003, %eax\n"
        "    call _fake_hlod\n"
        "    leave\n"
        "    ret $4\n"
        ".p2align 4\n"
        "_fake_hlod:\n"
        "    subl $0x4C, %esp\n"
        "    pushl %ebx\n"
        "    pushl %ebp\n"
        "    movl $0x5EED0004, %eax\n"
        "    call _fake_anim\n"
        "    popl %ebp\n"
        "    popl %ebx\n"
        "    addl $0x4C, %esp\n"
        "    ret\n"
        ".p2align 4\n"
        "_fake_anim:\n"
        "    subl $0x30, %esp\n"
        "    pushl %esi\n"
        "    movl %ecx, %esi\n"
        "    movl $0x5EED0005, %eax\n"
        "    popl %esi\n"
        "    addl $0x30, %esp\n"
        "    ret\n"
        /* 0 if ebx/esi/edi/ebp and anim's eax survive a wrapped call. */
        ".p2align 4\n"
        "_check_regs:\n"
        "    pushl %ebx\n"
        "    pushl %esi\n"
        "    pushl %edi\n"
        "    pushl %ebp\n"
        "    movl $0x11111111, %ebx\n"
        "    movl $0x22222222, %esi\n"
        "    movl $0x33333333, %edi\n"
        "    movl $0x44444444, %ebp\n"
        "    call _fake_anim\n"
        "    cmpl $0x5EED0005, %eax\n"
        "    jne 1f\n"
        "    cmpl $0x11111111, %ebx\n"
        "    jne 1f\n"
        "    cmpl $0x22222222, %esi\n"
        "    jne 1f\n"
        "    cmpl $0x33333333, %edi\n"
        "    jne 1f\n"
        "    cmpl $0x44444444, %ebp\n"
        "    jne 1f\n"
        "    xorl %eax, %eax\n"
        "    jmp 2f\n"
        "1:  movl $1, %eax\n"
        "2:  popl %ebp\n"
        "    popl %edi\n"
        "    popl %esi\n"
        "    popl %ebx\n"
        "    ret\n");

/* Assembly labels, called through typed pointers: a __stdcall prototype
 * would link against the decorated _fake_logic@4. */
extern char fake_frame[], fake_logic[], fake_anim[], check_regs[];
typedef int (*int_fn)(void);
typedef int(__stdcall *logic_fn)(int);

static int call_frame(void) { return ((int_fn)(void *)fake_frame)(); }
static int call_logic(int x) { return ((logic_fn)(void *)fake_logic)(x); }
static int call_anim(void) { return ((int_fn)(void *)fake_anim)(); }
static int call_check_regs(void) { return ((int_fn)(void *)check_regs)(); }

static const pf_site g_sites[PF_SITE_COUNT] = {
    { "frame", "fake_frame", "55 89 E5 51 53 B8 01 00 ED 5E", 5, 0, PF_GROUP_FRAME, 0 },
    { "logic", "fake_logic", "B8 02 00 ED 5E 8B 44 24 04", 5, 4, PF_GROUP_LOGIC, 0 },
    { "render", "fake_render", "55 89 E5 83 EC 28 B8 03 00 ED 5E", 6, 4, PF_GROUP_RENDER, 0 },
    { "skel_hlod", "fake_hlod", "83 EC 4C 53 55 B8 04 00 ED 5E", 5, 0, PF_GROUP_SKEL, 0 },
    { "skel_anim", "fake_anim", "83 EC 30 56 89 CE B8 05 00 ED 5E", 6, 0, PF_GROUP_SKEL, 0 },
};

static DWORD WINAPI other_thread(LPVOID unused)
{
    (void)unused;
    return (DWORD)call_anim();
}

static int read_file(const WCHAR *path, char *buf, unsigned cap)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, 0, NULL);
    DWORD got = 0;
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    ReadFile(h, buf, cap - 1, &got, NULL);
    CloseHandle(h);
    buf[got] = 0;
    return 1;
}

/* The n-th comma-separated field of line (0-based) into out. */
static void field(const char *line, unsigned n, char *out, unsigned cap)
{
    unsigned i = 0;
    while (n && *line) {
        if (*line++ == ',')
            n--;
    }
    while (*line && *line != ',' && *line != '\r' && i + 1 < cap)
        out[i++] = *line++;
    out[i] = 0;
}

int main(void)
{
    WCHAR csv[MAX_PATH];
    char text[4096], f[32];
    const char *row;
    pf_image img;
    pf_result res[PF_SITE_COUNT];
    int enabled[PF_SITE_COUNT] = { 1, 1, 1, 1, 1 };
    pf_timing_stats s;
    HANDLE t;
    DWORD code = 0;
    unsigned i;

    GetTempPathW(MAX_PATH, csv);
    wcscat(csv, L"bfme2perf-hooks-test.csv");

    printf("unhooked\n");
    CHECK(call_frame() == 41 && call_logic(5) == 6 && call_check_regs() == 0, "plain calls");

    printf("install\n");
    CHECK(pf_image_open(&img, GetModuleHandleW(NULL)), "no .text in the test exe");
    CHECK(pf_timing_open(g_sites, csv), "can't create %ls", csv);
    pf_install(&img, g_sites, PF_SITE_COUNT, enabled, PF_GROUP_FRAME, pf_enter_stubs,
               pf_timing_set_next, res);
    for (i = 0; i < PF_SITE_COUNT; i++)
        CHECK(res[i].status == PF_ST_APPLIED, "%s: %s (%u matches)", g_sites[i].id,
              pf_status_name(res[i].status), res[i].matches);
    pf_timing_set_installed((1u << PF_SITE_COUNT) - 1);

    printf("calls\n");
    CHECK(call_logic(5) == 6, "logic before the first frame");
    for (i = 0; i < 3; i++)
        CHECK(call_frame() == 41, "frame %u", i);
    t = CreateThread(NULL, 0, other_thread, NULL, 0, NULL);
    WaitForSingleObject(t, INFINITE);
    GetExitCodeThread(t, &code);
    CloseHandle(t);
    CHECK(code == 0x5EED0005, "anim on another thread returned %08lX", (unsigned long)code);

    pf_timing_get_stats(&s);
    CHECK(s.frames == 3 && s.logic_calls == 3 && s.render_calls == 3, "frames %u logic %u render %u",
          s.frames, s.logic_calls, s.render_calls);
    CHECK(s.skel_render_calls == 3 && s.skel_other_calls == 3,
          "skeleton during render %u, elsewhere %u", s.skel_render_calls, s.skel_other_calls);
    CHECK(s.passed_through == 5, "passed through %u (1 early, 3 nested, 1 other thread)",
          s.passed_through);
    CHECK(s.stale == 0, "stale %u", s.stale);

    printf("registers\n");
    CHECK(call_check_regs() == 0, "a register or the return value changed");
    pf_timing_get_stats(&s);
    CHECK(s.skel_other_calls == 4, "check_regs's anim not timed: %u", s.skel_other_calls);

    printf("row\n");
    pf_timing_flush();
    CHECK(read_file(csv, text, sizeof text), "no CSV");
    printf("%s", text);
    CHECK(strncmp(text, "time_s,frames,", 14) == 0, "no header");
    row = strstr(text, "\r\n");
    row = row ? row + 2 : "";
    field(row, 1, f, sizeof f);
    CHECK(!strcmp(f, "3"), "frames field %s", f);
    field(row, 4, f, sizeof f);
    CHECK(f[0] != 0, "logic column empty");
    field(row, 5, f, sizeof f);
    CHECK(f[0] != 0, "render column empty");
    field(row, 8, f, sizeof f);
    CHECK(!strcmp(f, "1"), "anim_on_screen %s", f);

    pf_timing_discard(csv);
    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: hook tests passed\n");
    return 0;
}
```

- [x] **Step 2: Add to `perf/Makefile`**

```make
$(BUILD)/test_hooks.exe: tests/test_hooks.c src/scan.c src/patch.c src/stubs.c src/timing.c \
                         src/csvrow.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)
```

and run `./$(BUILD)/test_hooks.exe` after `test_sites` in `test`.

- [x] **Step 3: Run it to see it fail** — `patch.h: No such file or directory`.

- [x] **Step 4: Write `perf/src/patch.h`**

```c
/* Entry hooks: a function's first `steal` bytes become a jmp to a stub; the
 * stolen bytes run from a trampoline that jumps back. */
#ifndef PF_PATCH_H
#define PF_PATCH_H

#include "scan.h"
#include "sites.h"

typedef enum {
    PF_ST_OFF,
    PF_ST_BAD_DEF,
    PF_ST_NOT_FOUND,
    PF_ST_AMBIGUOUS,
    PF_ST_GROUP_REFUSED,
    PF_ST_NO_MEMORY,
    PF_ST_PROTECT_FAILED,
    PF_ST_LOCATED,
    PF_ST_APPLIED
} pf_status;

typedef struct {
    pf_status status;
    unsigned matches;
    unsigned rva;
    void *trampoline;
} pf_result;

/* Locates every enabled site before writing anything. A group is written
 * only if all its members were located, and nothing at all unless every
 * member of required_group was. set_next(i, trampoline) runs before site i
 * is written, so stubs[i] can continue there. */
void pf_install(const pf_image *img, const pf_site *sites, unsigned count, const int *enabled,
                unsigned required_group, void *const *stubs, void (*set_next)(unsigned, void *),
                pf_result *results);

const char *pf_status_name(pf_status st);

#endif
```

- [x] **Step 5: Write `perf/src/patch.c`**

```c
#include "patch.h"

#define PF_MAX_STEAL 16

static unsigned char *trampoline(const unsigned char *bytes, unsigned steal, const void *resume)
{
    static unsigned char *pool;
    static unsigned used, size;
    unsigned char *t;
    unsigned i, need = (steal + 5 + 15) & ~15u;
    int rel;

    if (!pool || used + need > size) {
        pool = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!pool)
            return NULL;
        used = 0;
        size = 4096;
    }
    t = pool + used;
    used += need;
    for (i = 0; i < steal; i++)
        t[i] = bytes[i];
    rel = (int)((UINT_PTR)resume - (UINT_PTR)(t + steal + 5));
    t[steal] = 0xE9;
    for (i = 0; i < 4; i++)
        t[steal + 1 + i] = (unsigned char)((unsigned)rel >> (8 * i));
    FlushInstructionCache(GetCurrentProcess(), t, steal + 5);
    return t;
}

static void locate(const pf_image *img, const pf_site *s, pf_result *r)
{
    pf_pattern sig;
    if (!pf_parse_pattern(s->signature, &sig) || s->steal < 5 || s->steal > PF_MAX_STEAL ||
        s->steal > sig.len || !sig.fixed[0]) {
        r->status = PF_ST_BAD_DEF;
        return;
    }
    r->matches = pf_scan(img, &sig, &r->rva);
    r->status = r->matches == 0 ? PF_ST_NOT_FOUND
                : r->matches > 1 ? PF_ST_AMBIGUOUS
                                 : PF_ST_LOCATED;
}

static void apply(const pf_image *img, const pf_site *s, unsigned i, void *stub,
                  void (*set_next)(unsigned, void *), pf_result *r)
{
    unsigned char *site = img->base + r->rva;
    unsigned char code[PF_MAX_STEAL];
    DWORD old, unused;
    unsigned k;
    int rel;

    r->trampoline = trampoline(site, s->steal, site + s->steal);
    if (!r->trampoline) {
        r->status = PF_ST_NO_MEMORY;
        return;
    }
    set_next(i, r->trampoline);
    rel = (int)((UINT_PTR)stub - (UINT_PTR)(site + 5));
    code[0] = 0xE9;
    for (k = 0; k < 4; k++)
        code[1 + k] = (unsigned char)((unsigned)rel >> (8 * k));
    for (k = 5; k < s->steal; k++)
        code[k] = 0xCC;
    if (!VirtualProtect(site, s->steal, PAGE_EXECUTE_READWRITE, &old)) {
        r->status = PF_ST_PROTECT_FAILED;
        return;
    }
    for (k = 0; k < s->steal; k++)
        site[k] = code[k];
    VirtualProtect(site, s->steal, old, &unused);
    FlushInstructionCache(GetCurrentProcess(), site, s->steal);
    r->status = PF_ST_APPLIED;
}

void pf_install(const pf_image *img, const pf_site *sites, unsigned count, const int *enabled,
                unsigned required_group, void *const *stubs, void (*set_next)(unsigned, void *),
                pf_result *results)
{
    unsigned i, k;
    int required_ok = 1;

    for (i = 0; i < count; i++) {
        results[i].status = PF_ST_OFF;
        results[i].matches = results[i].rva = 0;
        results[i].trampoline = NULL;
        if (enabled[i])
            locate(img, &sites[i], &results[i]);
    }
    for (i = 0; i < count; i++)
        if (sites[i].group == required_group && results[i].status != PF_ST_LOCATED)
            required_ok = 0;
    for (i = 0; i < count; i++) {
        if (results[i].status != PF_ST_LOCATED)
            continue;
        if (!required_ok) {
            results[i].status = PF_ST_GROUP_REFUSED;
            continue;
        }
        for (k = 0; k < count; k++)
            if (sites[k].group == sites[i].group && results[k].status != PF_ST_LOCATED &&
                results[k].status != PF_ST_GROUP_REFUSED)
                results[i].status = PF_ST_GROUP_REFUSED;
    }
    for (i = 0; i < count; i++)
        if (results[i].status == PF_ST_LOCATED)
            apply(img, &sites[i], i, stubs[i], set_next, &results[i]);
}

const char *pf_status_name(pf_status st)
{
    switch (st) {
    case PF_ST_OFF: return "off";
    case PF_ST_BAD_DEF: return "REFUSED (bad table entry)";
    case PF_ST_NOT_FOUND: return "REFUSED (signature not found)";
    case PF_ST_AMBIGUOUS: return "REFUSED (signature ambiguous)";
    case PF_ST_GROUP_REFUSED: return "not installed (another hook of its group is missing)";
    case PF_ST_NO_MEMORY: return "REFUSED (no trampoline memory)";
    case PF_ST_PROTECT_FAILED: return "REFUSED (VirtualProtect failed)";
    case PF_ST_LOCATED: return "located";
    case PF_ST_APPLIED: return "applied";
    }
    return "?";
}
```

- [x] **Step 6: Write `perf/src/stubs.h`**

```c
/* Entry stub k calls pf_enter(k, &return_address) and continues at the
 * trampoline it returns. Exit stub k is where a wrapped call returns: it
 * calls pf_exit(k, esp) for the real return address and returns there with
 * every register, the flags and the return value as the function left them. */
#ifndef PF_STUBS_H
#define PF_STUBS_H

#include "sites.h"

extern void *const pf_enter_stubs[PF_SITE_COUNT];
extern void *const pf_exit_stubs[PF_SITE_COUNT];

#endif
```

- [x] **Step 7: Write `perf/src/stubs.c`**

```c
#include "stubs.h"

/* Both commons: [esp] = k, [esp+4] = the hooked call's return address
 * (enter) or the caller's stack after the wrapped call returned (exit).
 * pushfl + pushal put k at 36(%esp); the callee's result replaces it, so the
 * final ret lands there with everything else restored. */
__asm__(".text\n"
        ".p2align 4\n"
        "_pf_enter_common:\n"
        "    pushfl\n"
        "    pushal\n"
        "    leal 40(%esp), %eax\n"
        "    pushl %eax\n"
        "    pushl 40(%esp)\n"
        "    call _pf_enter\n"
        "    addl $8, %esp\n"
        "    movl %eax, 36(%esp)\n"
        "    popal\n"
        "    popfl\n"
        "    ret\n"
        ".p2align 4\n"
        "_pf_exit_common:\n"
        "    pushfl\n"
        "    pushal\n"
        "    leal 40(%esp), %eax\n"
        "    pushl %eax\n"
        "    pushl 40(%esp)\n"
        "    call _pf_exit\n"
        "    addl $8, %esp\n"
        "    movl %eax, 36(%esp)\n"
        "    popal\n"
        "    popfl\n"
        "    ret\n");

#define PF_STUB(kind, n)                \
    ".p2align 3\n"                      \
    "_pf_" #kind "_stub" #n ":\n"       \
    "    pushl $" #n "\n"               \
    "    jmp _pf_" #kind "_common\n"

__asm__(".text\n" PF_STUB(enter, 0) PF_STUB(enter, 1) PF_STUB(enter, 2) PF_STUB(enter, 3)
            PF_STUB(enter, 4) PF_STUB(exit, 0) PF_STUB(exit, 1) PF_STUB(exit, 2)
                PF_STUB(exit, 3) PF_STUB(exit, 4));

extern char pf_enter_stub0[], pf_enter_stub1[], pf_enter_stub2[], pf_enter_stub3[],
    pf_enter_stub4[], pf_exit_stub0[], pf_exit_stub1[], pf_exit_stub2[], pf_exit_stub3[],
    pf_exit_stub4[];

void *const pf_enter_stubs[PF_SITE_COUNT] = { pf_enter_stub0, pf_enter_stub1, pf_enter_stub2,
                                              pf_enter_stub3, pf_enter_stub4 };
void *const pf_exit_stubs[PF_SITE_COUNT] = { pf_exit_stub0, pf_exit_stub1, pf_exit_stub2,
                                             pf_exit_stub3, pf_exit_stub4 };
```

- [x] **Step 8: Write `perf/src/timing.h`**

```c
/* Timing hooks: wrap each outermost call of the hooked functions on the main
 * thread (the first thread to run the frame hook), attribute its time and
 * write one CSV row per second from the frame hook's exit. */
#ifndef PF_TIMING_H
#define PF_TIMING_H

#include <windows.h>

#include "sites.h"

typedef struct {
    unsigned frames, logic_calls, render_calls, skel_render_calls, skel_other_calls;
    unsigned passed_through, stale, rows;
} pf_timing_stats;

/* Before installing: creates the CSV (overwriting it) with its header.
 * Returns 0 if the file can't be created. */
int pf_timing_open(const pf_site *sites, const WCHAR *csv_path);
void pf_timing_set_next(unsigned k, void *trampoline);
/* Bit k set: site k is hooked. Decides which columns are filled. */
void pf_timing_set_installed(unsigned mask);
/* No hooks after all: closes and deletes the CSV. */
void pf_timing_discard(const WCHAR *csv_path);
/* Writes the current row now. */
void pf_timing_flush(void);
void pf_timing_get_stats(pf_timing_stats *s);

void *pf_enter(unsigned k, void **slot);
void *pf_exit(unsigned k, unsigned char *esp);

#endif
```

- [x] **Step 9: Write `perf/src/timing.c`**

```c
#include "timing.h"

#include "csvrow.h"
#include "stubs.h"

#define PF_STACK 64

typedef struct {
    unsigned k;
    void **slot; /* where the caller's return address was */
    void *ret;
    LONGLONG t0;
} wrapped_call;

static const pf_site *g_sites;
static void *g_next[PF_SITE_COUNT];
static unsigned g_have;
static HANDLE g_csv = INVALID_HANDLE_VALUE;
static DWORD g_main;
static LONGLONG g_freq, g_first_t0, g_row_t0;
static wrapped_call g_stack[PF_STACK];
static unsigned g_sp;
static unsigned g_depth[PF_GROUP_COUNT];
static unsigned g_time_s;
static volatile LONG g_passed;
static pf_timing_stats g_stats;

/* The current row, in QPC ticks. */
static LONGLONG g_frame, g_frame_max, g_logic, g_render, g_skel_render, g_skel_other;
static unsigned g_frames, g_skel_render_calls;

static LONGLONG now(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

static unsigned long long to_us(LONGLONG ticks)
{
    return (unsigned long long)ticks * 1000000ull / (unsigned long long)g_freq;
}

static void write_row(void)
{
    DWORD err = GetLastError(), put;
    char buf[256];
    unsigned n;
    pf_row r;

    r.time_s = g_time_s;
    r.frames = g_frames;
    r.frame_us = to_us(g_frame);
    r.frame_max_us = to_us(g_frame_max);
    r.logic_us = to_us(g_logic);
    r.render_us = to_us(g_render);
    r.skel_render_us = to_us(g_skel_render);
    r.skel_other_us = to_us(g_skel_other);
    r.skel_render_calls = g_skel_render_calls;
    r.to_workers = r.main_thread = 0;
    r.have = g_have;
    n = pf_format_row(&r, buf, sizeof buf);
    if (n && g_csv != INVALID_HANDLE_VALUE)
        WriteFile(g_csv, buf, n, &put, NULL);
    g_stats.rows++;
    g_frame = g_frame_max = g_logic = g_render = g_skel_render = g_skel_other = 0;
    g_frames = g_skel_render_calls = 0;
    SetLastError(err);
}

int pf_timing_open(const pf_site *sites, const WCHAR *csv_path)
{
    LARGE_INTEGER f;
    DWORD put;
    unsigned n = 0;

    g_sites = sites;
    QueryPerformanceFrequency(&f);
    g_freq = f.QuadPart;
    g_csv = CreateFileW(csv_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_csv == INVALID_HANDLE_VALUE)
        return 0;
    while (pf_csv_header[n])
        n++;
    WriteFile(g_csv, pf_csv_header, n, &put, NULL);
    return 1;
}

void pf_timing_set_next(unsigned k, void *trampoline)
{
    if (k < PF_SITE_COUNT)
        g_next[k] = trampoline;
}

void pf_timing_set_installed(unsigned mask)
{
    unsigned skel = 1u << PF_SKEL_HLOD | 1u << PF_SKEL_ANIM;
    g_have = (mask & 1u << PF_LOGIC ? PF_HAVE_LOGIC : 0) |
             (mask & 1u << PF_RENDER ? PF_HAVE_RENDER : 0) |
             ((mask & skel) == skel ? PF_HAVE_SKEL : 0);
}

void pf_timing_discard(const WCHAR *csv_path)
{
    if (g_csv != INVALID_HANDLE_VALUE)
        CloseHandle(g_csv);
    g_csv = INVALID_HANDLE_VALUE;
    DeleteFileW(csv_path);
}

void pf_timing_flush(void)
{
    write_row();
}

void pf_timing_get_stats(pf_timing_stats *s)
{
    *s = g_stats;
    s->passed_through = (unsigned)g_passed;
}

void *__attribute__((used)) pf_enter(unsigned k, void **slot)
{
    DWORD tid = GetCurrentThreadId();
    unsigned g = g_sites[k].group;
    wrapped_call *c;

    if (!g_main && k == PF_FRAME)
        g_main = tid;
    if (tid != g_main || g_depth[g] || g_sp == PF_STACK) {
        InterlockedIncrement(&g_passed);
        return g_next[k];
    }
    c = &g_stack[g_sp++];
    c->k = k;
    c->slot = slot;
    c->ret = *slot;
    *slot = pf_exit_stubs[k];
    g_depth[g]++;
    c->t0 = now();
    if (!g_first_t0)
        g_first_t0 = g_row_t0 = c->t0;
    return g_next[k];
}

void *__attribute__((used)) pf_exit(unsigned k, unsigned char *esp)
{
    void **want = (void **)(esp - 4 - g_sites[k].arg_bytes);
    LONGLONG t = now(), dt;
    wrapped_call *c;

    while (g_sp > 1 && g_stack[g_sp - 1].slot < want) {
        g_depth[g_sites[g_stack[g_sp - 1].k].group]--;
        g_sp--;
        g_stats.stale++;
    }
    c = &g_stack[--g_sp];
    dt = t - c->t0;
    g_depth[g_sites[k].group]--;
    switch (k) {
    case PF_FRAME:
        g_frame += dt;
        if (dt > g_frame_max)
            g_frame_max = dt;
        g_frames++;
        g_stats.frames++;
        if (t - g_row_t0 >= g_freq) {
            write_row();
            g_row_t0 = t;
            g_time_s = (unsigned)((t - g_first_t0) / g_freq);
        }
        break;
    case PF_LOGIC:
        g_logic += dt;
        g_stats.logic_calls++;
        break;
    case PF_RENDER:
        g_render += dt;
        g_stats.render_calls++;
        break;
    default:
        if (g_depth[PF_GROUP_RENDER]) {
            g_skel_render += dt;
            g_skel_render_calls++;
            g_stats.skel_render_calls++;
        } else {
            g_skel_other += dt;
            g_stats.skel_other_calls++;
        }
    }
    return c->ret;
}
```

The `g_sp > 1` guard keeps the last entry: the exit stub only runs for a
call that `pf_enter` wrapped, so the stack is never empty there, and when
stale entries were dropped the remaining top is the call returning now.

- [x] **Step 10: Run the tests to see them pass** — `OK: hook tests passed`.

- [x] **Step 11: Commit** `feat(perf): entry hooks and the timing accumulator`

### Task 5: the 0.2.0 plugin

**Files:**
- Modify: `perf/src/main.c`, `perf/src/version.rc`, `perf/tests/test_load.c`, `perf/Makefile`
- Create: `perf/bfme2perf.ini`
- Modify: `docs/specs/2026-10-13-perf-dll-design.md` (Milestone 1: the render site and the CSV columns)

**Interfaces:**
- Consumes: everything above.
- Produces: `build/bfme2perf.asi` 0.2.0; log lines `bfme2perf 0.2.0`, `cmd : …`, `game: …`, `ini : … (found|missing, using defaults)`, `settings: …`, `exe : .text crc32 …`, one `hook <id> (<function>): …` per site, then `timing: writing …` or `timing: no hooks installed (the frame hook is required)`.

- [x] **Step 1: Update `perf/tests/test_load.c`**

Replace the version check and add, after the `mss32` line:

```c
    CHECK(strstr(log, "bfme2perf 0.2.0\r\n") == log, "first line is not the version");
    /* the self, host, pinned and module checks stay as they are */
    CHECK(strstr(log, "game: unknown") != NULL, "no game line");
    CHECK(strstr(log, "(missing, using defaults)") != NULL, "missing INI not reported");
    CHECK(strstr(log, "settings: [Perf] Enabled=1 Log=1, [Timing] Enabled=1, "
                      "[Skeleton] Threads=0 Verify=0") != NULL, "no settings line");
    CHECK(strstr(log, "exe : .text crc32 ") != NULL, "no exe hash");
    CHECK(strstr(log, "hook frame (GameEngine::update): REFUSED (signature not found)") != NULL,
          "frame hook not refused in a non-game host");
    CHECK(strstr(log, "timing: no hooks installed") != NULL, "no 'no hooks' line");
    CHECK(GetFileAttributesW(csv_path) == INVALID_FILE_ATTRIBUTES, "CSV left behind");
```

with `csv_path` = `%TEMP%\bfme2perf-test\bfme2perf_timing.csv`, deleted
before the load like the log.

- [x] **Step 2: Run it to see it fail** — `first line is not the version`.

- [x] **Step 3: Rewrite `perf/src/main.c`** (the 0.1.0 helpers `ms_since_process_start`, `sibling_path`, `log_module` and `RIB_Main` unchanged; `PERF_VERSION` "0.2.0"; `DllMain` reads the INI, logs the lines above, and calls `install_hooks`):

```c
static void install_hooks(const pf_config *cfg, const WCHAR *csv_path)
{
    pf_image img;
    pf_result res[PF_SITE_COUNT];
    int enabled[PF_SITE_COUNT];
    unsigned i, installed = 0;

    if (!pf_image_open(&img, GetModuleHandleW(NULL))) {
        fu_logf("exe : no usable .text section, no hooks");
        return;
    }
    fu_logf("exe : .text crc32 %08X, %u bytes, timestamp %08X",
            pf_crc32(img.base + img.text_rva, img.text_size), img.text_size, img.timestamp);
    if (!cfg->timing) {
        fu_logf("timing: off ([Timing] Enabled=0), no hooks");
        return;
    }
    if (!pf_timing_open(pf_sites, csv_path)) {
        fu_logf("timing: can't create %ls, no hooks", csv_path);
        return;
    }
    for (i = 0; i < PF_SITE_COUNT; i++)
        enabled[i] = 1;
    pf_install(&img, pf_sites, PF_SITE_COUNT, enabled, PF_GROUP_FRAME, pf_enter_stubs,
               pf_timing_set_next, res);
    for (i = 0; i < PF_SITE_COUNT; i++) {
        if (res[i].status == PF_ST_APPLIED) {
            installed |= 1u << i;
            fu_logf("hook %s (%s): applied at %08X", pf_sites[i].id, pf_sites[i].function,
                    res[i].rva);
        } else {
            fu_logf("hook %s (%s): %s, %u match(es)", pf_sites[i].id, pf_sites[i].function,
                    pf_status_name(res[i].status), res[i].matches);
        }
    }
    if (!(installed & 1u << PF_FRAME)) {
        pf_timing_discard(csv_path);
        fu_logf("timing: no hooks installed (the frame hook is required)");
        return;
    }
    pf_timing_set_installed(installed);
    fu_logf("timing: writing %ls, one row per second", csv_path);
}
```

- [x] **Step 4: Version resource 0.2.0; `perf/bfme2perf.ini` with the spec's settings and comments; `make dist` copies it; CFLAGS gain `-mgeneral-regs-only` (the stubs run between a function's return and its caller: no x87 or SSE register may change).**

- [x] **Step 5: Run every test** — `mingw32-make clean test`: row, config, sites, hooks and load all `OK`; `objdump -p build/bfme2perf.asi | grep "DLL Name"` shows only `KERNEL32.dll`.

- [x] **Step 6: Update the spec's Milestone 1** with the render site (`RTS3DScene::Render`, 1.06 0x00071E40, AotR 0x0007177E, its signature) and this plan's CSV column table, and commit `feat(perf): timing build 0.2.0` with the spec change.

### Task 6: install and hand over

- [x] **Step 1:** `mingw32-make dist`, then copy `dist/bfme2perf.asi` and `dist/bfme2perf.ini` to `C:\Jogos\AgeoftheRing\rotwk\mss\` (overwriting 0.1.0). Nothing else in the game folder is written.
- [ ] **Step 2:** The user plays `Test Replay` from the Replays menu and quits; then read `mss\bfme2perf.log` and `mss\bfme2perf_timing.csv`.
