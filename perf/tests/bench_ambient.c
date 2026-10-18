/* Offline test only. Reads the user's image into private memory, relocates
 * only a fully inspected sampler, and calls it with synthetic data. Never
 * enters the executable's startup, scene renderer, or game loop. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "refused %s:%d: %s\n", \
    __FILE__, __LINE__, #c); exit(1); } } while (0)
#define BODY_SIZE 507
#define CALLS 1290
#define REPEATS 3000
#define SAMPLES 15

typedef unsigned char (__attribute__((thiscall)) *sample_fn)(void *, const float *, float *);
typedef struct {
    void **vt;
    unsigned pad[6];
    int extent_x, extent_y;
    unsigned char *pixels;
    unsigned width, height;
} terrain;
_Static_assert(sizeof(terrain) == 0x30, "sampler input ABI");
static float __attribute__((thiscall)) border(void *self) { (void)self; return 0.0f; }
static long long ticks(void) { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }
static unsigned hash(const unsigned char *p, unsigned n)
{
    unsigned h = 2166136261u;
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}
static int compare(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
    FILE *f;
    long size;
    unsigned char *raw, *memory, *code;
    IMAGE_NT_HEADERS32 *nt;
    IMAGE_SECTION_HEADER *section;
    unsigned rva, i, offset, delta, floor_iat;
    DWORD old;
    HMODULE crt;
    sample_fn sample;
    void *vt[12] = {0};
    terrain t = {0};
    float positions[CALLS][3], colours[CALLS][3];
    LARGE_INTEGER frequency;
    REQUIRE(argc == 15); /* three identities, import field, nine relocations */
    rva = strtoul(argv[3], NULL, 10);
    f = fopen(argv[1], "rb"); REQUIRE(f);
    REQUIRE(!fseek(f, 0, SEEK_END)); size = ftell(f); rewind(f); REQUIRE(size > 4096);
    raw = malloc(size); REQUIRE(raw); REQUIRE(fread(raw, 1, size, f) == (size_t)size); fclose(f);
    REQUIRE(((IMAGE_DOS_HEADER *)raw)->e_magic == IMAGE_DOS_SIGNATURE);
    offset = ((IMAGE_DOS_HEADER *)raw)->e_lfanew;
    REQUIRE(offset <= (unsigned)size - sizeof(*nt)); nt = (void *)(raw + offset);
    REQUIRE(nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC);
    REQUIRE(nt->OptionalHeader.SizeOfImage < 128 * 1024 * 1024 &&
            nt->OptionalHeader.SizeOfHeaders <= (unsigned)size);
    REQUIRE(rva < nt->OptionalHeader.SizeOfImage - BODY_SIZE);
    memory = VirtualAlloc(NULL, nt->OptionalHeader.SizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    REQUIRE(memory); memcpy(memory, raw, nt->OptionalHeader.SizeOfHeaders);
    section = IMAGE_FIRST_SECTION(nt);
    REQUIRE((unsigned char *)(section + nt->FileHeader.NumberOfSections) <= raw + size);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        REQUIRE(section[i].PointerToRawData <= (unsigned)size &&
                section[i].SizeOfRawData <= (unsigned)size - section[i].PointerToRawData);
        REQUIRE(section[i].VirtualAddress <= nt->OptionalHeader.SizeOfImage &&
                section[i].SizeOfRawData <= nt->OptionalHeader.SizeOfImage - section[i].VirtualAddress);
        memcpy(memory + section[i].VirtualAddress, raw + section[i].PointerToRawData, section[i].SizeOfRawData);
    }
    code = memory + rva;
    REQUIRE(hash(code, BODY_SIZE) == strtoul(argv[4], NULL, 10));
    offset = strtoul(argv[5], NULL, 10); REQUIRE(offset <= BODY_SIZE - 4);
    floor_iat = *(unsigned *)(code + offset) - nt->OptionalHeader.ImageBase;
    REQUIRE(floor_iat <= nt->OptionalHeader.SizeOfImage - 4);
    delta = (unsigned)memory - nt->OptionalHeader.ImageBase;
    for (i = 6; i < (unsigned)argc; i++) {
        offset = strtoul(argv[i], NULL, 10); REQUIRE(offset <= BODY_SIZE - 4);
        REQUIRE(*(unsigned *)(code + offset) >= nt->OptionalHeader.ImageBase &&
                *(unsigned *)(code + offset) - nt->OptionalHeader.ImageBase < nt->OptionalHeader.SizeOfImage);
        *(unsigned *)(code + offset) += delta;
    }
    crt = LoadLibraryA(argv[2]); REQUIRE(crt);
    *(FARPROC *)(memory + floor_iat) = GetProcAddress(crt, "floor");
    REQUIRE(*(FARPROC *)(memory + floor_iat));
    REQUIRE(VirtualProtect(code, BODY_SIZE, PAGE_EXECUTE_READ, &old));
    REQUIRE(FlushInstructionCache(GetCurrentProcess(), code, BODY_SIZE));
    sample = (sample_fn)code;
    vt[11] = border; t.vt = vt; t.extent_x = t.extent_y = 512;
    t.width = t.height = 2048;
    t.pixels = malloc(t.width * t.height * 3); REQUIRE(t.pixels);
    memset(t.pixels, 255, t.width * t.height * 3);
    for (i = 0; i < CALLS; i++) {
        /* Wide synthetic positions repeat across modeled passes. */
        positions[i][0] = (float)((i % 530) * 73 % 5120) + 0.25f;
        positions[i][1] = (float)((i % 530) * 191 % 5120) + 0.75f;
        positions[i][2] = 0;
        REQUIRE(sample(&t, positions[i], colours[i]));
        for (unsigned j = 0; j < 3; j++) REQUIRE(colours[i][j] == 1.0f);
    }
    /* Check failed samples leave output alone, including no-map/empty map. */
    {
        unsigned char *pixels = t.pixels;
        float pos[3] = {-1, 10000, 0}, colour[3] = {2, 3, 4};
        t.pixels = NULL; REQUIRE(!sample(&t, pos, colour));
        REQUIRE(colour[0] == 2 && colour[1] == 3 && colour[2] == 4);
        t.pixels = pixels; t.extent_x = 0; REQUIRE(!sample(&t, pos, colour));
        REQUIRE(colour[0] == 2 && colour[1] == 3 && colour[2] == 4);
        t.extent_x = 512;
        REQUIRE(sample(&t, pos, colour));
        REQUIRE(colour[0] == 1 && colour[1] == 1 && colour[2] == 1);
    }
    QueryPerformanceFrequency(&frequency);
    puts("kind,sample,map_size,calls,ms\nValidation: uniform map, clamped coordinates, absent map, empty extent passed");
    for (unsigned mode = 0; mode < 3; mode++) {
        double times[SAMPLES];
        unsigned char *pixels = t.pixels;
        t.width = t.height = mode == 1 ? 256 : 2048;
        if (mode == 2) t.pixels = NULL;
        for (unsigned round = 0; round < SAMPLES + 2; round++) {
            long long begin = ticks();
            for (unsigned repeat = 0; repeat < REPEATS; repeat++)
                for (i = 0; i < CALLS; i++) sample(&t, positions[i], colours[i]);
            double ms = (ticks() - begin) * 1000.0 / frequency.QuadPart / REPEATS;
            if (round < 2) continue;
            times[round - 2] = ms;
            printf("sample,%u,%u,%u,%.6f\n", round - 2, mode == 2 ? 0 : t.width, CALLS, ms);
        }
        qsort(times, SAMPLES, sizeof(double), compare);
        printf("SUMMARY map=%u calls=%u: median %.6f ms, p10 %.6f p90 %.6f\n",
               mode == 2 ? 0 : t.width, CALLS, times[SAMPLES/2], times[1], times[13]);
        t.pixels = pixels;
    }
    free(t.pixels); VirtualFree(memory, 0, MEM_RELEASE); free(raw); FreeLibrary(crt);
    return 0;
}
