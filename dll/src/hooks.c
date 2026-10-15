#include "hooks.h"

#include "log.h"
#include "sides.h"
#include "skirmish.h"

static void *volatile g_next[FU_MAX_PATCHES];
static volatile LONG g_calls[FU_MAX_PATCHES];
static volatile LONG g_hits[FU_MAX_PATCHES];

/* Entered with [esp] = k (pushed by stub k), [esp+4] = return address of the
 * hooked call, [esp+8] = its first argument. The dispatcher overwrites the k
 * slot with the continuation address, so the final `ret` pops it and resumes
 * there with esp, all registers and EFLAGS as they were at the hooked site. */
__asm__(
    ".text\n"
    ".p2align 4\n"
    "_fu_hook_common:\n"
    "    pushfl\n"
    "    pushal\n"
    "    leal 36(%esp), %eax\n" /* &k slot (pushal 32 + pushfl 4) */
    "    leal 40(%esp), %ecx\n" /* esp of the hooked code: its return address */
    "    pushl %ecx\n"
    "    pushl %eax\n"
    "    call _fu_hook_dispatch\n"
    "    addl $8, %esp\n"
    "    popal\n"
    "    popfl\n"
    "    ret\n");

#define FU_STUB(n)                    \
    ".p2align 3\n"                    \
    "_fu_hook_stub" #n ":\n"          \
    "    pushl $" #n "\n"             \
    "    jmp _fu_hook_common\n"

__asm__(".text\n" FU_STUB(0) FU_STUB(1) FU_STUB(2) FU_STUB(3) FU_STUB(4) FU_STUB(5) FU_STUB(6)
            FU_STUB(7) FU_STUB(8) FU_STUB(9) FU_STUB(10) FU_STUB(11) FU_STUB(12) FU_STUB(13)
                FU_STUB(14) FU_STUB(15));

extern char fu_hook_stub0[], fu_hook_stub1[], fu_hook_stub2[], fu_hook_stub3[], fu_hook_stub4[],
    fu_hook_stub5[], fu_hook_stub6[], fu_hook_stub7[], fu_hook_stub8[], fu_hook_stub9[],
    fu_hook_stub10[], fu_hook_stub11[], fu_hook_stub12[], fu_hook_stub13[], fu_hook_stub14[],
    fu_hook_stub15[];

static char *const g_stubs[FU_MAX_PATCHES] = {
    fu_hook_stub0,  fu_hook_stub1,  fu_hook_stub2,  fu_hook_stub3,
    fu_hook_stub4,  fu_hook_stub5,  fu_hook_stub6,  fu_hook_stub7,
    fu_hook_stub8,  fu_hook_stub9,  fu_hook_stub10, fu_hook_stub11,
    fu_hook_stub12, fu_hook_stub13, fu_hook_stub14, fu_hook_stub15,
};

void __attribute__((used, cdecl)) fu_hook_dispatch(void **kslot, void **entry_sp);

void __attribute__((used, cdecl)) fu_hook_dispatch(void **kslot, void **entry_sp)
{
    unsigned k = (unsigned)(UINT_PTR)*kslot;
    const fu_patch_def *def = &fu_patch_defs[k];

    InterlockedIncrement(&g_calls[k]);
    if (def->hook == FU_HOOK_SIDE_ALIAS) {
        void **arg = &entry_sp[def->arg_slot];
        const void *donor = fu_sides_resolve(*arg);
        if (donor) {
            *arg = (void *)donor;
            InterlockedIncrement(&g_hits[k]);
        }
    } else if (def->hook == FU_HOOK_SKIRMISH_THIS) {
        /* kslot[-1] is the saved EFLAGS, kslot[-2] EAX, kslot[-3] ECX (`this`).
         * The stand-in `this` is only read at +FU_PLAYER_SIDE_OFF. */
        void **self = &kslot[-3];
        const void *side = (const char *)*self + FU_PLAYER_SIDE_OFF;
        fu_skirmish_step step;
        const void *r = fu_skirmish_resolve(side, &step);
        if (r != side) {
            *self = (char *)r - FU_PLAYER_SIDE_OFF;
            InterlockedIncrement(&g_hits[k]);
            fu_log_event("skirmish: %s has no skirmish player on this map, using %s's",
                         fu_ascii_cstr(side), fu_ascii_cstr(r));
        }
    } else if (def->hook == FU_HOOK_SKIRMISH_ARG) {
        void **arg = &entry_sp[def->arg_slot];
        fu_skirmish_step step;
        const void *r = fu_skirmish_resolve(*arg, &step);
        if (r != *arg) {
            *arg = (void *)r;
            InterlockedIncrement(&g_hits[k]);
        } else if (step == FU_SKIRMISH_NONE && *fu_ascii_cstr(r) &&
                   !fu_sides_is_reserved(fu_ascii_cstr(r)) && fu_skirmish_first_unresolved(r)) {
            fu_log_event("skirmish: this map has no skirmish player for %s, its [Sides] donor "
                         "or any game Side; a computer %s player gets no AI",
                         fu_ascii_cstr(r), fu_ascii_cstr(r));
        }
    }
    *kslot = g_next[k];
}

void *fu_hook_stub(unsigned k)
{
    return k < FU_MAX_PATCHES ? g_stubs[k] : NULL;
}

void fu_hook_set_next(unsigned k, void *next)
{
    g_next[k] = next;
}

void *fu_hook_next(unsigned k)
{
    return g_next[k];
}

unsigned char *fu_hook_trampoline(const unsigned char *bytes, unsigned steal, const void *resume)
{
    static unsigned char *pool;
    static unsigned used, size;
    unsigned char *t;
    unsigned i, need = (steal + 5 + 15) & ~15u;
    int rel;

    if (steal > FU_MAX_STEAL)
        return NULL;
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

void fu_hook_counts(unsigned k, unsigned *calls, unsigned *hits)
{
    *calls = (unsigned)g_calls[k];
    *hits = (unsigned)g_hits[k];
}

void fu_hook_reset_counts(void)
{
    unsigned k;
    for (k = 0; k < FU_MAX_PATCHES; k++)
        g_calls[k] = g_hits[k] = 0;
}
