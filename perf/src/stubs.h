/* Entry stub k calls pf_enter(k, &return_address) and continues at the
 * trampoline it returns. Exit stub k is where a wrapped call returns: it
 * calls pf_exit(k, esp) for the real return address and returns there with
 * every register, the flags and the return value as the function left them. */
#ifndef PF_STUBS_H
#define PF_STUBS_H

#include "sites.h"

extern void *const pf_enter_stubs[PF_SITE_COUNT];
extern void *const pf_exit_stubs[PF_SITE_COUNT];

/* A `ret n` for a callee that pops 0, 4, 8 or 12 bytes; NULL otherwise. */
void *pf_skip_thunk(unsigned arg_bytes);

#endif
