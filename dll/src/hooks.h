/* Hook stubs. Patch k's site jumps/calls fu_hook_stub(k), which saves all
 * registers and flags, lets the patch's handler rewrite the arguments on the
 * stack, restores everything and continues at fu_hook_next(k) with the stack
 * exactly as the hooked code expects it. */
#ifndef FU_HOOKS_H
#define FU_HOOKS_H

#include "engine.h"

void *fu_hook_stub(unsigned k);
void fu_hook_set_next(unsigned k, void *next);
void *fu_hook_next(unsigned k);

/* Copies `steal` prologue bytes into executable memory followed by a jmp to
 * `resume`. Returns the trampoline, or NULL if out of memory. */
unsigned char *fu_hook_trampoline(const unsigned char *bytes, unsigned steal, const void *resume);

/* How often stub k ran, and how often its handler changed something. */
void fu_hook_counts(unsigned k, unsigned *calls, unsigned *hits);
void fu_hook_reset_counts(void);

#endif
