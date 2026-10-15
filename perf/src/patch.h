/* Entry hooks: a function's first `steal` bytes become a jmp to a stub; the
 * stolen bytes run from a trampoline that jumps back. Call hooks: a
 * call's target becomes the stub, which continues at the old callee.
 * Indirect call hooks (`call [reg+disp32]`): a direct call to the stub and a
 * nop; set_next gets disp32 as the "trampoline", and the stub continues at
 * [reg+disp32] with the register the call had. */
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
    PF_ST_BAD_TARGET,
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

/* For an indirect call hook: the base register's number (0 eax .. 7 edi, as
 * in pushal's reverse order), or -1 if the site isn't one. */
int pf_call_reg(const pf_site *s);

#endif
