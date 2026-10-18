#include "queueinsert.h"
#include "queueinsert_table.h"
#include "patch.h"
#include <string.h>

/* This path changes only the rendering-task queue. The by-value mesh holder
 * is consumed exactly once, including when the original entry is called. */
typedef struct { unsigned word[8]; } qi_record;
typedef struct { qi_record *first, *last, *limit; } qi_queue;
typedef struct { void **first, **last, **limit; } qi_methods;
typedef struct { void *vtable; unsigned refs; } qi_object;
static pf_queue_add_fn qi_original;

void pf_queue_insert_original(pf_queue_add_fn original) { qi_original = original; }

static int intersects(unsigned begin, unsigned end, unsigned slot)
{ return begin < slot + 32 && slot < end; }

void __attribute__((thiscall)) pf_queue_insert_add(void *queues, void *mesh, const void *input)
{
    const qi_methods *methods = input;
    unsigned begin = (unsigned)methods->first, end = (unsigned)methods->last;
    if (end < begin || ((end - begin) & 3) || end - begin > 24 || !mesh)
        goto fallback;
    unsigned char *model = *(unsigned char **)((char *)mesh + 0xc4);
    if (!model) goto fallback;
    unsigned which = 0;
    if (model[0x19] & 4) {
        const unsigned char *geometry = *(unsigned char **)(model + 0xbc);
        if (!geometry) goto fallback;
        which = *geometry ? 1 : 2;
    }
    qi_queue *q = (qi_queue *)queues + which;
    unsigned first = (unsigned)q->first, last = (unsigned)q->last, limit = (unsigned)q->limit;
    if (!first || last < first || limit < last || ((last - first) & 31) ||
        ((limit - first) & 31) || limit - last < 32) goto fallback;
    /* Native construction zeros the holders before reading the input stack.
     * Preserve that order by delegating any destination/input alias. */
    if (intersects(begin, end, last) ||
        intersects((unsigned)methods, (unsigned)(methods + 1), last)) goto fallback;
    qi_record *record = q->last;
    unsigned count = (end - begin) / 4;
    record->word[0] = (unsigned)mesh;
    record->word[1] = count;
    for (unsigned i = 0; i < 6; i++) record->word[2 + i] = 0;
    q->last++;
    ((qi_object *)mesh)->refs++;
    for (unsigned i = 0; i < count; i++) {
        qi_object *method = methods->first[i];
        if (method) method->refs++;
        record->word[2 + i] = (unsigned)method;
    }
    ((qi_object *)mesh)->refs--;
    return;
fallback:
    qi_original(queues, mesh, input);
}

static unsigned read_word(const unsigned char *p)
{ unsigned value; memcpy(&value, p, 4); return value; }

/* Require a complete range in one section of the expected role. Bounds are
 * checked before reading a body or following one of its encoded targets. */
static int section_role(const IMAGE_NT_HEADERS32 *nt, unsigned rva, unsigned size, int code)
{
    unsigned image_size = nt->OptionalHeader.SizeOfImage, matches = 0;
    if (!size || rva >= image_size || size > image_size - rva) return 0;
    const IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        const IMAGE_SECTION_HEADER *s = sections + i;
        if (rva >= s->VirtualAddress && rva - s->VirtualAddress < s->Misc.VirtualSize &&
            size <= s->Misc.VirtualSize - (rva - s->VirtualAddress) &&
            (s->Characteristics & IMAGE_SCN_MEM_READ) &&
            !!(s->Characteristics & IMAGE_SCN_MEM_EXECUTE) == code) matches++;
    }
    return matches == 1;
}

const char *pf_queue_insert_find(const pf_image *image, unsigned *entry)
{
    unsigned locations[QI_SYMBOL_COUNT] = {0};
    unsigned char done[QI_BODY_COUNT] = {0}, normalized[QI_MAX_BODY];
    if (entry) *entry = 0;
    if (!image || !image->base) return "missing image";
    const IMAGE_DOS_HEADER *dos = (const void *)image->base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < (LONG)sizeof *dos ||
        dos->e_lfanew > 4096 - (LONG)sizeof(IMAGE_NT_HEADERS32)) return "invalid DOS header";
    const IMAGE_NT_HEADERS32 *nt = (const void *)(image->base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->FileHeader.SizeOfOptionalHeader != sizeof nt->OptionalHeader ||
        !nt->FileHeader.NumberOfSections || nt->FileHeader.NumberOfSections > 32 ||
        nt->OptionalHeader.SizeOfHeaders > 4096 ||
        nt->OptionalHeader.SizeOfImage < nt->OptionalHeader.SizeOfHeaders ||
        (unsigned)((const unsigned char *)(IMAGE_FIRST_SECTION(nt) + nt->FileHeader.NumberOfSections) -
                   image->base) > nt->OptionalHeader.SizeOfHeaders) return "invalid PE header";
    if (!section_role(nt, image->text_rva, image->text_size, 1)) return "invalid text range";
    pf_pattern pattern;
    if (!pf_parse_pattern(QI_SIGNATURE, &pattern)) return "invalid entry pattern";
    unsigned matches = pf_scan(image, &pattern, &locations[0]);
    if (matches != 1) return matches ? "ambiguous entry signature" : "entry signature missing";
    unsigned verified = 0;
    for (;;) {
        unsigned before = verified;
        for (unsigned i = 0; i < QI_BODY_COUNT; i++) {
            if (!locations[i] || done[i]) continue;
            const qi_body *body = qi_bodies + i;
            unsigned rva = locations[i];
            if (!section_role(nt, rva, body->size, 1)) return "body outside executable section";
            memcpy(normalized, image->base + rva, body->size);
            for (unsigned j = 0; j < body->count; j++) {
                const qi_patch *p = qi_patches + body->first + j;
                unsigned value = read_word(normalized + p->offset);
                unsigned target = p->absolute ? value - (unsigned)image->base :
                                               rva + p->offset + 4 + value;
                if (!section_role(nt, target, 1, qi_code[p->symbol])) return "dependency section role";
                if (locations[p->symbol] && locations[p->symbol] != target) return "dependency identity differs";
                locations[p->symbol] = target;
                memset(normalized + p->offset, 0, 4);
            }
            unsigned hash = 2166136261u;
            for (unsigned j = 0; j < body->size; j++) hash = (hash ^ normalized[j]) * 16777619u;
            if (hash != body->fnv || pf_crc32(normalized, body->size) != body->crc)
                return "complete body differs";
            done[i] = 1; verified++;
        }
        if (verified == QI_BODY_COUNT) break;
        if (before == verified) return "incomplete dependency closure";
    }
    for (unsigned i = 0; i < QI_SYMBOL_COUNT; i++) {
        if (!locations[i]) return "unresolved dependency";
        for (unsigned j = i + 1; j < QI_SYMBOL_COUNT; j++) {
            unsigned a = i < QI_BODY_COUNT ? qi_bodies[i].size : 1;
            unsigned b = j < QI_BODY_COUNT ? qi_bodies[j].size : 1;
            if (locations[i] < locations[j] + b && locations[j] < locations[i] + a)
                return "overlapping dependency identities";
        }
    }
    if (entry) *entry = locations[0];
    return NULL;
}

static void set_original(unsigned index, void *original)
{ (void)index; pf_queue_insert_original((pf_queue_add_fn)original); }

int pf_queue_insert_install(const pf_image *image, const char **why)
{
    unsigned entry;
    const char *error = pf_queue_insert_find(image, &entry);
    if (error) { if (why) *why = error; return 0; }
    /* mov eax,imm32 is position independent. Its original SEH setup remains
     * in place after the trampoline returns to the following instruction. */
    const pf_site site = {"queue-insert", "AddTask", QI_SIGNATURE, 0, 5, 8, 1, 1, 0, 0};
    const int enabled = 1;
    void *stub = (void *)pf_queue_insert_add;
    pf_result result;
    pf_install(image, &site, 1, &enabled, 1, &stub, set_original, &result);
    if (why) *why = pf_status_name(result.status);
    return result.status == PF_ST_APPLIED;
}
