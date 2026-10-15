#include "patch.h"

#define PF_MAX_STEAL 16

static unsigned read_le32(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static unsigned char *trampoline(const unsigned char *site, unsigned steal, const void *resume)
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
    if (steal == 5 && site[0] == 0xE9) {
        /* A jmp rel32 can't run from elsewhere as is: go straight to its target. */
        resume = site + 5 + (int)read_le32(site + 1);
        steal = 0;
    }
    for (i = 0; i < steal; i++)
        t[i] = site[i];
    rel = (int)((UINT_PTR)resume - (UINT_PTR)(t + steal + 5));
    t[steal] = 0xE9;
    for (i = 0; i < 4; i++)
        t[steal + 1 + i] = (unsigned char)((unsigned)rel >> (8 * i));
    FlushInstructionCache(GetCurrentProcess(), t, steal + 5);
    return t;
}

int pf_call_reg(const pf_site *s)
{
    pf_pattern sig;
    unsigned char modrm;
    if (s->call_hook != 2 || s->steal != 6 || !pf_parse_pattern(s->signature, &sig) ||
        s->site_offset + 6 > sig.len || !sig.fixed[s->site_offset] ||
        !sig.fixed[s->site_offset + 1] || sig.bytes[s->site_offset] != 0xFF)
        return -1;
    modrm = sig.bytes[s->site_offset + 1];
    /* mod 10, reg /2 (call), r/m a register other than esp (that needs a SIB). */
    if ((modrm & 0xF8) != 0x90 || (modrm & 7) == 4)
        return -1;
    return modrm & 7;
}

static void locate(const pf_image *img, const pf_site *s, pf_result *r)
{
    pf_pattern sig;
    if (!pf_parse_pattern(s->signature, &sig) || s->steal < 5 || s->steal > PF_MAX_STEAL ||
        s->site_offset + s->steal > sig.len || !sig.fixed[s->site_offset] ||
        (s->call_hook == 1 && (s->steal != 5 || sig.bytes[s->site_offset] != 0xE8)) ||
        (s->call_hook == 2 && pf_call_reg(s) < 0) ||
        (s->call_hook == PF_CALL_INDIRECT &&
         (s->steal != 6 || !sig.fixed[s->site_offset + 1] || sig.bytes[s->site_offset] != 0xFF ||
          sig.bytes[s->site_offset + 1] != 0x15)) ||
        s->call_hook > PF_CALL_INDIRECT) {
        r->status = PF_ST_BAD_DEF;
        return;
    }
    r->matches = pf_scan(img, &sig, &r->rva);
    if (r->matches == 1)
        r->rva += s->site_offset;
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

    if (s->call_hook == PF_CALL_INDIRECT) {
        /* call [import]: continue at what the import slot holds now. */
        r->trampoline = *(void *const *)(UINT_PTR)read_le32(site + 2);
    } else if (s->call_hook == 1) {
        unsigned callee = r->rva + 5 + read_le32(site + 1);
        if (callee < img->text_rva || callee >= img->text_rva + img->text_size) {
            r->status = PF_ST_BAD_TARGET;
            return;
        }
        r->trampoline = img->base + callee;
    } else if (s->call_hook == 2) {
        /* The target is read from [reg+disp32] at each call; the slot keeps disp32. */
        r->trampoline = (void *)(UINT_PTR)read_le32(site + 2);
    } else {
        r->trampoline = trampoline(site, s->steal, site + s->steal);
    }
    if (!r->trampoline) {
        r->status = PF_ST_NO_MEMORY;
        return;
    }
    set_next(i, r->trampoline);
    rel = (int)((UINT_PTR)stub - (UINT_PTR)(site + 5));
    code[0] = s->call_hook ? 0xE8 : 0xE9;
    for (k = 0; k < 4; k++)
        code[1 + k] = (unsigned char)((unsigned)rel >> (8 * k));
    /* After a call the rest is executed (nop); after a jmp it never is. */
    for (k = 5; k < s->steal; k++)
        code[k] = s->call_hook ? 0x90 : 0xCC;
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
    case PF_ST_BAD_TARGET: return "REFUSED (call target outside .text)";
    case PF_ST_LOCATED: return "located";
    case PF_ST_APPLIED: return "applied";
    }
    return "?";
}
