#include "nativecontract.h"
#include <string.h>

#define MAX_BODIES 64u
#define MAX_SYMBOLS 128u
#define MAX_BODY_BYTES 4096u

static unsigned read_word(const unsigned char *p)
{ unsigned value; memcpy(&value, p, 4); return value; }

static int section_role(const IMAGE_NT_HEADERS32 *nt, unsigned rva, unsigned size, int code,
                        int writable)
{
    unsigned image_size = nt->OptionalHeader.SizeOfImage, matches = 0;
    if (!size || rva >= image_size || size > image_size - rva) return 0;
    const IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        const IMAGE_SECTION_HEADER *s = sections + i;
        if (rva >= s->VirtualAddress && rva - s->VirtualAddress < s->Misc.VirtualSize &&
            size <= s->Misc.VirtualSize - (rva - s->VirtualAddress) &&
            (s->Characteristics & IMAGE_SCN_MEM_READ) &&
            !!(s->Characteristics & IMAGE_SCN_MEM_EXECUTE) == code &&
            (!writable || (s->Characteristics & IMAGE_SCN_MEM_WRITE))) matches++;
    }
    return matches == 1;
}

static unsigned span(const pf_native_contract *contract, unsigned symbol)
{
    return contract->spans ? contract->spans[symbol] :
        symbol < contract->body_count ? contract->bodies[symbol].size : 1;
}

static int region(const IMAGE_NT_HEADERS32 *nt, const pf_native_contract *contract,
                  unsigned symbol, unsigned rva)
{
    return section_role(nt, rva, span(contract, symbol), contract->code[symbol],
                        contract->writable && contract->writable[symbol]);
}

const char *pf_contract_find(const pf_image *image, const pf_native_contract *contract,
                             unsigned *output)
{
    unsigned locations[MAX_SYMBOLS] = {0};
    unsigned char done[MAX_BODIES] = {0}, normalized[MAX_BODY_BYTES];
    if (!contract || !contract->body_count || contract->body_count > MAX_BODIES ||
        contract->symbol_count < contract->body_count || contract->symbol_count > MAX_SYMBOLS ||
        !contract->root_count || contract->root_count > contract->body_count)
        return "invalid contract dimensions";
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
    if (!section_role(nt, image->text_rva, image->text_size, 1, 0)) return "invalid text range";
    for (unsigned i = 0; i < contract->root_count; i++) {
        const pf_contract_root *root = contract->roots + i;
        pf_pattern pattern;
        if (root->symbol >= contract->body_count || !contract->code[root->symbol] ||
            !pf_parse_pattern(root->signature, &pattern)) return "invalid entry pattern";
        unsigned found = 0, matches = pf_scan(image, &pattern, &found);
        if (matches != 1) return matches ? "ambiguous entry signature" : "entry signature missing";
        if (locations[root->symbol] && locations[root->symbol] != found) return "root identity differs";
        locations[root->symbol] = found;
    }
    unsigned verified = 0;
    for (;;) {
        unsigned before = verified;
        for (unsigned i = 0; i < contract->body_count; i++) {
            if (!locations[i] || done[i]) continue;
            const pf_contract_body *body = contract->bodies + i;
            unsigned rva = locations[i];
            if (body->size < 1 || body->size > MAX_BODY_BYTES ||
                body->size > span(contract, i) ||
                body->first > contract->patch_count || body->count > contract->patch_count - body->first)
                return "invalid body metadata";
            if (!region(nt, contract, i, rva)) return "body outside executable section";
            memcpy(normalized, image->base + rva, body->size);
            for (unsigned j = 0; j < body->count; j++) {
                const pf_contract_patch *p = contract->patches + body->first + j;
                if (body->size < 4 || p->offset > body->size - 4 || p->absolute > 1 ||
                    p->symbol >= contract->symbol_count || p->addend >= span(contract, p->symbol))
                    return "invalid dependency metadata";
                unsigned value = read_word(normalized + p->offset);
                unsigned target = p->absolute ? value - (unsigned)image->base : rva + p->offset + 4 + value;
                if (target < p->addend) return "dependency section role";
                target -= p->addend;
                if (!region(nt, contract, p->symbol, target)) return "dependency section role";
                if (locations[p->symbol] && locations[p->symbol] != target) return "dependency identity differs";
                locations[p->symbol] = target;
                memset(normalized + p->offset, 0, 4);
            }
            unsigned hash = 2166136261u;
            for (unsigned j = 0; j < body->size; j++) hash = (hash ^ normalized[j]) * 16777619u;
            if (hash != body->fnv || pf_crc32(normalized, body->size) != body->crc) return "complete body differs";
            done[i] = 1; verified++;
        }
        if (verified == contract->body_count) break;
        if (before == verified) return "incomplete dependency closure";
    }
    for (unsigned i = 0; i < contract->symbol_count; i++) {
        if (!locations[i]) return "unresolved dependency";
        for (unsigned j = i + 1; j < contract->symbol_count; j++) {
            unsigned a = span(contract, i), b = span(contract, j);
            if (locations[i] < locations[j] + b && locations[j] < locations[i] + a)
                return "overlapping dependency identities";
        }
    }
    if (output) memcpy(output, locations, contract->symbol_count * sizeof *locations);
    return NULL;
}
