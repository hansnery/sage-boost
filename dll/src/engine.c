#include "engine.h"

#include "hooks.h"

#ifdef FU_TESTING
extern BOOL fu_test_protect(void *, SIZE_T, DWORD, DWORD *);
#define FU_PROTECT fu_test_protect
#else
#define FU_PROTECT VirtualProtect
#endif

typedef struct {
    unsigned len;
    unsigned char bytes[FU_MAX_PATTERN];
    unsigned char fixed[FU_MAX_PATTERN]; /* 1 = must match, 0 = wildcard */
} fu_pattern;

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* "8B ?? 0F" -> pattern. Returns 0 on malformed input. */
static int parse_pattern(const char *s, fu_pattern *p)
{
    p->len = 0;
    for (;;) {
        while (*s == ' ')
            s++;
        if (!*s)
            return p->len > 0;
        if (p->len == FU_MAX_PATTERN)
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

static int pattern_at(const unsigned char *at, const fu_pattern *p)
{
    unsigned i;
    for (i = 0; i < p->len; i++)
        if (p->fixed[i] && at[i] != p->bytes[i])
            return 0;
    return 1;
}

/* Counts matches in [start, start+size); *first gets the first one. */
static unsigned pattern_scan(const unsigned char *start, unsigned size,
                             const fu_pattern *p, unsigned *first)
{
    unsigned anchor = 0, count = 0, i;
    while (anchor < p->len && !p->fixed[anchor])
        anchor++;
    if (anchor == p->len || size < p->len)
        return 0;
    for (i = 0; i + p->len <= size; i++) {
        if (start[i + anchor] != p->bytes[anchor] || !pattern_at(start + i, p))
            continue;
        if (count++ == 0)
            *first = i;
    }
    return count;
}

/* The fixed bytes of `expect` must be what the def's kind knows how to rewrite. */
static int expect_fits_kind(const fu_patch_def *def, const fu_pattern *e)
{
    switch (def->kind) {
    case FU_KIND_JCC8_TO_JMP:
    case FU_KIND_JCC8_NOP:
        return e->len == 2 && e->fixed[0] && (e->bytes[0] & 0xF0) == 0x70;
    case FU_KIND_JCC32_NOP:
    case FU_KIND_JCC32_TO_JMP:
        return e->len == 6 && e->fixed[0] && e->bytes[0] == 0x0F &&
               e->fixed[1] && (e->bytes[1] & 0xF0) == 0x80;
    case FU_KIND_IMM8_CLEAR:
        return e->len == 1;
    case FU_KIND_ENTRY_HOOK:
        return def->hook != FU_HOOK_NONE && def->steal >= 5 && def->steal <= FU_MAX_STEAL &&
               e->len == def->steal;
    case FU_KIND_CALL_HOOK:
        return def->hook != FU_HOOK_NONE && e->len == 5 && e->fixed[0] && e->bytes[0] == 0xE8;
    }
    return 0;
}

static unsigned read_le32(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static void write_le32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

/* E9/E8 rel32 from `site` to `dest`, CC up to `len`. */
static void build_hook(const unsigned char *site, unsigned char opcode, const void *dest,
                       unsigned len, unsigned char *out)
{
    unsigned i;
    out[0] = opcode;
    write_le32(out + 1, (unsigned)((UINT_PTR)dest - (UINT_PTR)(site + 5)));
    for (i = 5; i < len; i++)
        out[i] = 0xCC;
}

static void build_patch(fu_kind kind, const unsigned char *orig, unsigned char *out)
{
    unsigned i;
    switch (kind) {
    case FU_KIND_ENTRY_HOOK:
    case FU_KIND_CALL_HOOK:
        break; /* build_hook */
    case FU_KIND_JCC8_TO_JMP:
        out[0] = 0xEB;
        out[1] = orig[1];
        break;
    case FU_KIND_JCC8_NOP:
        out[0] = out[1] = 0x90;
        break;
    case FU_KIND_JCC32_NOP:
        for (i = 0; i < 6; i++)
            out[i] = 0x90;
        break;
    case FU_KIND_JCC32_TO_JMP: {
        /* jmp rel32 is one byte shorter than jcc rel32: rel+1 keeps the target. */
        unsigned rel = (unsigned)orig[2] | (unsigned)orig[3] << 8 |
                       (unsigned)orig[4] << 16 | (unsigned)orig[5] << 24;
        rel += 1;
        out[0] = 0xE9;
        out[1] = (unsigned char)rel;
        out[2] = (unsigned char)(rel >> 8);
        out[3] = (unsigned char)(rel >> 16);
        out[4] = (unsigned char)(rel >> 24);
        out[5] = 0x90;
        break;
    }
    case FU_KIND_IMM8_CLEAR:
        out[0] = 0x00;
        break;
    }
}

static int branch_target_ok(const fu_image *img, fu_kind kind, unsigned site_rva,
                            const unsigned char *orig)
{
    long long target;
    if (kind == FU_KIND_JCC8_TO_JMP || kind == FU_KIND_JCC8_NOP)
        target = (long long)site_rva + 2 + (signed char)orig[1];
    else if (kind == FU_KIND_JCC32_NOP || kind == FU_KIND_JCC32_TO_JMP)
        target = (long long)site_rva + 6 + (int)read_le32(orig + 2);
    else if (kind == FU_KIND_CALL_HOOK)
        target = (long long)site_rva + 5 + (int)read_le32(orig + 1);
    else
        return 1;
    return target >= img->text_rva && target < (long long)img->text_rva + img->text_size;
}

int fu_image_open(fu_image *img, void *module_base)
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
        img->preferred_base = nt->OptionalHeader.ImageBase;
        img->image_size = nt->OptionalHeader.SizeOfImage;
        return img->text_size > 0;
    }
    return 0;
}

const unsigned char *fu_image_va(const fu_image *img, unsigned va)
{
    unsigned rva = va - img->preferred_base;
    return va >= img->preferred_base && rva < img->image_size ? img->base + rva : NULL;
}

unsigned fu_engine_find(const fu_image *img, unsigned k, unsigned *sig_rva)
{
    fu_pattern sig;
    unsigned first = 0, n;
    if (k >= fu_patch_count || !parse_pattern(fu_patch_defs[k].signature, &sig))
        return 0;
    n = pattern_scan(img->base + img->text_rva, img->text_size, &sig, &first);
    *sig_rva = img->text_rva + first;
    return n;
}

void fu_select_game(const fu_game *game)
{
    fu_patch_defs = game->defs;
    fu_patch_count = game->count;
}

static void locate(const fu_image *img, unsigned k, fu_patch_result *r)
{
    const fu_patch_def *def = &fu_patch_defs[k];
    fu_pattern sig, expect;
    unsigned first = 0, i;
    const unsigned char *site;

    if (!parse_pattern(def->signature, &sig) || !parse_pattern(def->expect, &expect) ||
        def->site_offset + expect.len > sig.len || !expect_fits_kind(def, &expect) ||
        expect.len > sizeof r->original) {
        r->status = FU_ST_BAD_DEF;
        return;
    }
    r->matches = pattern_scan(img->base + img->text_rva, img->text_size, &sig, &first);
    if (r->matches == 0) {
        r->status = FU_ST_NOT_FOUND;
        return;
    }
    if (r->matches > 1) {
        r->status = FU_ST_AMBIGUOUS;
        return;
    }
    r->site_rva = img->text_rva + first + def->site_offset;
    r->len = expect.len;
    site = img->base + r->site_rva;
    for (i = 0; i < expect.len; i++)
        r->original[i] = site[i];
    if (!pattern_at(site, &expect)) {
        r->status = FU_ST_VERIFY_FAILED;
        return;
    }
    if (!branch_target_ok(img, def->kind, r->site_rva, r->original)) {
        r->status = FU_ST_BAD_TARGET;
        return;
    }
    if (def->kind == FU_KIND_ENTRY_HOOK || def->kind == FU_KIND_CALL_HOOK) {
        r->hook_stub = fu_hook_stub(k);
        build_hook(site, def->kind == FU_KIND_ENTRY_HOOK ? 0xE9 : 0xE8, r->hook_stub, r->len,
                   r->patched);
    } else {
        build_patch(def->kind, r->original, r->patched);
    }
    r->status = FU_ST_LOCATED;
}

/* Reserve trampolines and writable pages for the whole group before changing
 * any code. Restore protections in reverse order: multiple sites can share a
 * page, in which case later VirtualProtect calls see the temporary RWX flag. */
static void apply_group(const fu_image *img, unsigned first, fu_patch_result *results)
{
    unsigned members[FU_MAX_PATCHES], count = 0, protected = 0, n, j;
    DWORD old[FU_MAX_PATCHES], unused;
    unsigned group = fu_patch_defs[first].group;
    for (n = first; n < fu_patch_count; n++) {
        if (n != first && (!group || fu_patch_defs[n].group != group))
            continue;
        if (results[n].status == FU_ST_LOCATED)
            members[count++] = n;
    }
    for (n = 0; n < count; n++) {
        unsigned k = members[n];
        const fu_patch_def *def = &fu_patch_defs[k];
        fu_patch_result *r = &results[k];
        unsigned char *site = img->base + r->site_rva;
        for (j = 0; j < r->len; j++) {
            if (site[j] != r->original[j]) {
                r->status = FU_ST_VERIFY_FAILED;
                goto refused;
            }
        }
        if (def->kind == FU_KIND_ENTRY_HOOK) {
            r->hook_next = fu_hook_trampoline(r->original, def->steal, site + def->steal);
            if (!r->hook_next) {
                r->status = FU_ST_NO_MEMORY;
                goto refused;
            }
        } else if (def->kind == FU_KIND_CALL_HOOK) {
            r->hook_next = site + 5 + (int)read_le32(r->original + 1);
        }
        if (!FU_PROTECT(site, r->len, PAGE_EXECUTE_READWRITE, &old[n])) {
            r->status = FU_ST_PROTECT_FAILED;
            goto refused;
        }
        protected++;
    }
    for (n = 0; n < count; n++) {
        unsigned k = members[n];
        fu_patch_result *r = &results[k];
        unsigned char *site = img->base + r->site_rva;
        if (r->hook_next)
            fu_hook_set_next(k, r->hook_next);
        for (j = 0; j < r->len; j++)
            site[j] = r->patched[j];
        FlushInstructionCache(GetCurrentProcess(), site, r->len);
        r->status = FU_ST_APPLIED;
    }
    goto restore;
refused:
    for (n = 0; n < count; n++)
        if (results[members[n]].status == FU_ST_LOCATED)
            results[members[n]].status = FU_ST_GROUP_REFUSED;
restore:
    while (protected) {
        fu_patch_result *r = &results[members[--protected]];
        VirtualProtect(img->base + r->site_rva, r->len, old[protected], &unused);
    }
}

void fu_engine_run(const fu_image *img, const int *enabled, int dry_run,
                   fu_patch_result *results)
{
    unsigned i;

    for (i = 0; i < fu_patch_count; i++) {
        fu_patch_result *r = &results[i];
        unsigned k;
        r->status = FU_ST_OFF;
        r->matches = r->site_rva = r->len = 0;
        r->hook_stub = r->hook_next = NULL;
        for (k = 0; k < sizeof r->original; k++)
            r->original[k] = r->patched[k] = 0;
        if (enabled[i])
            locate(img, i, r);
    }
    for (i = 0; i < fu_patch_count; i++) {
        unsigned g = fu_patch_defs[i].group, k;
        if (!g || results[i].status != FU_ST_LOCATED)
            continue;
        for (k = 0; k < fu_patch_count; k++)
            if (fu_patch_defs[k].group == g && results[k].status != FU_ST_LOCATED &&
                results[k].status != FU_ST_GROUP_REFUSED)
                results[i].status = FU_ST_GROUP_REFUSED;
    }
    if (dry_run)
        return;
    for (i = 0; i < fu_patch_count; i++)
        if (results[i].status == FU_ST_LOCATED)
            apply_group(img, i, results);
}

const char *fu_status_name(fu_status st)
{
    switch (st) {
    case FU_ST_OFF: return "off";
    case FU_ST_BAD_DEF: return "REFUSED (bad table entry)";
    case FU_ST_NOT_FOUND: return "REFUSED (signature not found)";
    case FU_ST_AMBIGUOUS: return "REFUSED (signature ambiguous)";
    case FU_ST_VERIFY_FAILED: return "REFUSED (original bytes differ)";
    case FU_ST_BAD_TARGET: return "REFUSED (branch target outside .text)";
    case FU_ST_PROTECT_FAILED: return "REFUSED (VirtualProtect failed)";
    case FU_ST_NO_MEMORY: return "REFUSED (no trampoline memory)";
    case FU_ST_GROUP_REFUSED: return "not installed (its partner patch is off or refused)";
    case FU_ST_LOCATED: return "located (dry run, not written)";
    case FU_ST_APPLIED: return "applied";
    }
    return "?";
}
