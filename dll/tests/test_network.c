/* Executes copied, patched caller instructions from the user's executables.
 * No game is launched and no game file is modified. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
#include "hooks.h"
#include "network.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

/* cdecl probe(code, frame, slot, cursor, limit): scratch EBP frame supplies
 * the native locals. Code snippets finish with their original argument
 * cleanup and ret. Preserve the caller's nonvolatile registers. */
unsigned char *probe(void *, void *, void *, void *, void *);
__asm__(".text\n_probe:\n"
        "pushl %ebp\n pushl %ebx\n pushl %esi\n pushl %edi\n"
        "movl 20(%esp), %edx\n movl 24(%esp), %ebp\n"
        "movl 28(%esp), %esi\n movl 32(%esp), %eax\n movl 36(%esp), %edi\n"
        "movl %edi, %ebx\n call *%edx\n"
        "popl %edi\n popl %esi\n popl %ebx\n popl %ebp\n ret\n");

static unsigned char *map_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char *file, *mem;
    long size;
    IMAGE_NT_HEADERS32 *nt;
    IMAGE_SECTION_HEADER *sec;
    if (!f) { CHECK(f != NULL); return NULL; }
    fseek(f, 0, SEEK_END); size = ftell(f); rewind(f);
    file = malloc(size);
    CHECK(fread(file, 1, size, f) == (size_t)size);
    fclose(f);
    nt = (void *)(file + ((IMAGE_DOS_HEADER *)file)->e_lfanew);
    mem = VirtualAlloc(NULL, nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(mem != NULL);
    if (mem) {
        memcpy(mem, file, nt->OptionalHeader.SizeOfHeaders);
        sec = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
            memcpy(mem + sec->VirtualAddress, file + sec->PointerToRawData, sec->SizeOfRawData);
    }
    free(file);
    return mem;
}

static void call_at(unsigned char *site, void *target)
{
    unsigned rel = (unsigned char *)target - (site + 5);
    site[0] = 0xE8;
    memcpy(site + 1, &rel, 4);
}

static unsigned char __cdecl legacy_pack(unsigned char color, unsigned char faction, char c, char f)
{
    return (unsigned char)(((color - c) << 4) + faction - f);
}

/* Use the verified signature to copy the real argument setup. Replace calls
 * only, append cleanup/ret, then execute the original instruction sequence. */
static unsigned char *snippet(const fu_image *img, unsigned k)
{
    unsigned start = 0, off = fu_patch_defs[k].site_offset, n = off + 5;
    unsigned char *code;
    CHECK(fu_engine_find(img, k, &start) == 1);
    code = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    CHECK(code != NULL);
    if (!code) return NULL;
    if (fu_patch_defs[k].hook == FU_HOOK_NETWORK_WRITE_START) {
        /* Begin after the native accumulated-stack cleanup, which belongs
         * to preceding fields outside this isolated snippet. */
        start += off - 3;
        off = 3;
        n = off + 5;
    }
    memcpy(code, img->base + start, n);
    call_at(code + off, fu_hook_stub(k));
    if (fu_patch_defs[k].hook == FU_HOOK_NETWORK_WRITE) {
        CHECK(code[15] == 0xE8);
        call_at(code + 15, legacy_pack);
    }
    code[n++] = 0x83; code[n++] = 0xC4;
    code[n++] = fu_patch_defs[k].hook == FU_HOOK_NETWORK_WRITE ? 28 :
                fu_patch_defs[k].hook == FU_HOOK_NETWORK_SPLIT ? 20 : 12;
    code[n++] = 0xC3;
    FlushInstructionCache(GetCurrentProcess(), code, n);
    return code;
}

static void test_calls(const char *path, unsigned game)
{
    unsigned char *mem = map_file(path), *codes[10];
    fu_image img;
    int slot[8] = {0};
    unsigned frame_words[64];
    unsigned char *frame = (void *)(frame_words + 48);
    unsigned char wire[8], *next;
    if (!mem) return;
    printf("native network callers: %s\n", path);
    CHECK(fu_image_open(&img, mem));
    fu_select_game(&fu_games[game]);
    for (unsigned i = 0; i < 10; i++) codes[i] = snippet(&img, 10 + i);
    for (int kind = 0; kind < 2; kind++) {
        for (int faction = -2; faction <= 125; faction++) {
            for (int color = -1; color <= 14; color++) {
                for (int start = -1; start < 8; start++) {
                    memset(frame_words, 0xA5, sizeof frame_words);
                    memset(wire, 0xCC, sizeof wire);
                    slot[3] = color; slot[6] = faction; slot[4] = start;
                    next = probe(codes[kind], frame, slot, wire + 1, wire + 3);
                    CHECK(next == wire + 2);
                    next = probe(codes[6 + kind], frame, slot, next, wire + 3);
                    CHECK(next == wire + 3);
                    CHECK(wire[0] == 0xCC && wire[3] == 0xCC);
                    CHECK((signed char)wire[2] < -1); /* legacy human reader rejects */
                    *(void **)(frame - 16) = wire + 3;
                    next = probe(codes[2 + kind], frame, slot, NULL, wire + 1);
                    CHECK(next == wire + 2);
                    probe(codes[4 + kind], frame, slot, NULL, NULL);
                    CHECK((signed char)frame[-0x1C - kind] == color);
                    CHECK((signed char)frame[-0x1A - kind] == faction);
                    next = probe(codes[8 + kind], frame, slot, NULL, wire + 2);
                    CHECK(next == wire + 3);
                    CHECK((signed char)frame[kind ? -0x34 : -0x18] == start);
                    CHECK(frame[kind ? -0x33 : -0x17] == 0xA5); /* byte output */
                }
            }
        }
    }
    for (unsigned i = 0; i < 10; i++) VirtualFree(codes[i], 0, MEM_RELEASE);
    VirtualFree(mem, 0, MEM_RELEASE);
}

static void test_bounds(void)
{
    unsigned char wire[16], color, faction;
    int slot[8] = {0};
    unsigned out;
    static const int old_ids[] = {-2,-1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,-2};
    for (int i = 0; i < 17; i++) {
        slot[3] = 2; slot[6] = i + 14;
        CHECK((legacy_pack(2, i + 14, -1, -2) & 15) - 2 == old_ids[i]);
        CHECK(fu_network_write(wire, 0, wire + 2, slot) == wire + 1);
        CHECK(fu_network_read(wire, &out, wire + 2) == wire + 1);
        fu_network_split(out, -1, -2, &color, &faction);
        CHECK(color == 2 && faction == i + 14);
    }
    for (int n = 0; n < 2; n++) {
        memset(wire, 0xCC, sizeof wire);
        CHECK(fu_network_write(wire + 1, 0, wire + 1 + n, slot) == wire + 1 + n);
        CHECK(wire[0] == 0xCC && wire[1 + n] == 0xCC);
        out = 0xDEADBEEF;
        CHECK(fu_network_read(wire + 1, &out, wire + 1 + n) == wire + 1);
        CHECK(out == 0xDEADBEEF);
    }
    /* Exhaust every two-byte wire value. Legacy starts -1..7 cannot be
     * mistaken for our marker; reserved start codes 9..15 are rejected. */
    for (unsigned packed = 0; packed < 65536; packed++) {
        int valid = (packed & 0x8000) && ((packed >> 11) & 15) <= 8;
        wire[0] = packed; wire[1] = packed >> 8;
        out = 0xDEADBEEF;
        CHECK(fu_network_read(wire, &out, wire + 2) == wire + (valid ? 1 : 0));
        if (!valid) CHECK(out == 0xDEADBEEF);
        else CHECK(out == packed);
    }
    for (int which = 0; which < 3; which++) {
        static const int fields[] = {3, 4, 6}, lows[] = {-2, -2, -3}, highs[] = {15, 8, 126};
        for (int high = 0; high < 2; high++) {
            slot[3] = 2; slot[4] = 0; slot[6] = 30;
            slot[fields[which]] = high ? highs[which] : lows[which];
            memset(wire, 0xCC, sizeof wire);
            CHECK(fu_network_write(wire + 1, 0, wire + 3, slot) == wire + 3);
            CHECK(wire[0] == 0xCC && wire[3] == 0xCC && wire[1] == 0xFF);
            CHECK(fu_network_read(wire + 1, &out, wire + 3) == wire + 1);
        }
    }
    CHECK(fu_network_read(wire + 5, &out, wire + 4) == wire + 5);
    CHECK(fu_network_read(NULL, &out, wire) == NULL);
    CHECK(fu_network_read_start(wire, &color, wire) == wire);
    CHECK(fu_network_write_start(wire, 0, wire) == wire);
    /* Eight slots use exactly the original 16 bytes for these two fields. */
    slot[3] = 7; slot[4] = 7; slot[6] = 30;
    for (int i = 0; i < 8; i++) {
        unsigned char *next = fu_network_write(wire + i * 2, 0, wire + 16, slot);
        CHECK(next == wire + i * 2 + 1);
        CHECK(fu_network_write_start(next, 7, wire + 16) == wire + i * 2 + 2);
    }
}

int main(int argc, char **argv)
{
    test_bounds();
    for (int i = 1; i + 1 < argc; i += 2)
        if (strcmp(argv[i], "--wb") != 0)
            test_calls(argv[i + 1], strcmp(argv[i], "--bfme2") == 0 ? 0 : 1);
    printf("network: %s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures != 0;
}
