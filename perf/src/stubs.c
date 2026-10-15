#include "stubs.h"

/* Both commons: [esp] = k, [esp+4] = the hooked call's return address
 * (enter) or the caller's stack after the wrapped call returned (exit).
 * pushfl + pushal put k at 36(%esp); the callee's result replaces it, so the
 * final ret lands there with everything else restored. */
__asm__(".text\n"
        ".p2align 4\n"
        "_pf_enter_common:\n"
        "    pushfl\n"
        "    pushal\n"
        "    leal 40(%esp), %eax\n"
        "    pushl %eax\n"
        "    pushl 40(%esp)\n"
        "    call _pf_enter\n"
        "    addl $8, %esp\n"
        "    movl %eax, 36(%esp)\n"
        "    popal\n"
        "    popfl\n"
        "    ret\n"
        ".p2align 4\n"
        "_pf_exit_common:\n"
        "    pushfl\n"
        "    pushal\n"
        "    leal 40(%esp), %eax\n"
        "    pushl %eax\n"
        "    pushl 40(%esp)\n"
        "    call _pf_exit\n"
        "    addl $8, %esp\n"
        "    movl %eax, 36(%esp)\n"
        "    popal\n"
        "    popfl\n"
        "    ret\n");

#define PF_STUB(kind, n)                \
    ".p2align 3\n"                      \
    "_pf_" #kind "_stub" #n ":\n"       \
    "    pushl $" #n "\n"               \
    "    jmp _pf_" #kind "_common\n"

#define PF_STUB_PAIR(n) PF_STUB(enter, n) PF_STUB(exit, n)

__asm__(".text\n" PF_STUB_PAIR(0) PF_STUB_PAIR(1) PF_STUB_PAIR(2) PF_STUB_PAIR(3)
            PF_STUB_PAIR(4) PF_STUB_PAIR(5) PF_STUB_PAIR(6) PF_STUB_PAIR(7) PF_STUB_PAIR(8)
                PF_STUB_PAIR(9) PF_STUB_PAIR(10) PF_STUB_PAIR(11) PF_STUB_PAIR(12)
                    PF_STUB_PAIR(13) PF_STUB_PAIR(14) PF_STUB_PAIR(15) PF_STUB_PAIR(16)
                        PF_STUB_PAIR(17) PF_STUB_PAIR(18) PF_STUB_PAIR(19) PF_STUB_PAIR(20)
                            PF_STUB_PAIR(21) PF_STUB_PAIR(22) PF_STUB_PAIR(23)
                                PF_STUB_PAIR(24) PF_STUB_PAIR(25) PF_STUB_PAIR(26)
                                    PF_STUB_PAIR(27) PF_STUB_PAIR(28) PF_STUB_PAIR(29)
                                        PF_STUB_PAIR(30) PF_STUB_PAIR(31) PF_STUB_PAIR(32)
                                            PF_STUB_PAIR(33) PF_STUB_PAIR(34) PF_STUB_PAIR(35)
                                                PF_STUB_PAIR(36) PF_STUB_PAIR(37) PF_STUB_PAIR(38)
                                                    PF_STUB_PAIR(39) PF_STUB_PAIR(40)
                                                        PF_STUB_PAIR(41) PF_STUB_PAIR(42)
                                                            PF_STUB_PAIR(43) PF_STUB_PAIR(44)
                                                                PF_STUB_PAIR(45)
                                                                    PF_STUB_PAIR(46));

extern char pf_enter_stub0[], pf_enter_stub1[], pf_enter_stub2[], pf_enter_stub3[],
    pf_enter_stub4[], pf_enter_stub5[], pf_enter_stub6[], pf_enter_stub7[], pf_enter_stub8[],
    pf_enter_stub9[], pf_enter_stub10[], pf_enter_stub11[], pf_enter_stub12[],
    pf_enter_stub13[], pf_enter_stub14[], pf_enter_stub15[], pf_enter_stub16[],
    pf_enter_stub17[], pf_enter_stub18[], pf_enter_stub19[], pf_enter_stub20[],
    pf_enter_stub21[], pf_enter_stub22[], pf_enter_stub23[], pf_enter_stub24[],
    pf_enter_stub25[], pf_enter_stub26[], pf_enter_stub27[], pf_enter_stub28[],
    pf_enter_stub29[], pf_enter_stub30[], pf_enter_stub31[], pf_enter_stub32[], pf_enter_stub33[],
    pf_enter_stub34[], pf_enter_stub35[], pf_enter_stub36[], pf_enter_stub37[],
    pf_enter_stub38[], pf_enter_stub39[], pf_enter_stub40[], pf_enter_stub41[],
    pf_enter_stub42[], pf_enter_stub43[], pf_enter_stub44[], pf_enter_stub45[],
    pf_enter_stub46[];
extern char pf_exit_stub0[], pf_exit_stub1[], pf_exit_stub2[], pf_exit_stub3[], pf_exit_stub4[],
    pf_exit_stub5[], pf_exit_stub6[], pf_exit_stub7[], pf_exit_stub8[], pf_exit_stub9[],
    pf_exit_stub10[], pf_exit_stub11[], pf_exit_stub12[], pf_exit_stub13[], pf_exit_stub14[],
    pf_exit_stub15[], pf_exit_stub16[], pf_exit_stub17[], pf_exit_stub18[], pf_exit_stub19[],
    pf_exit_stub20[], pf_exit_stub21[], pf_exit_stub22[], pf_exit_stub23[], pf_exit_stub24[],
    pf_exit_stub25[], pf_exit_stub26[], pf_exit_stub27[], pf_exit_stub28[], pf_exit_stub29[],
    pf_exit_stub30[], pf_exit_stub31[], pf_exit_stub32[], pf_exit_stub33[], pf_exit_stub34[],
    pf_exit_stub35[], pf_exit_stub36[], pf_exit_stub37[], pf_exit_stub38[], pf_exit_stub39[],
    pf_exit_stub40[], pf_exit_stub41[], pf_exit_stub42[], pf_exit_stub43[], pf_exit_stub44[],
    pf_exit_stub45[], pf_exit_stub46[];

void *const pf_enter_stubs[PF_SITE_COUNT] = {
    pf_enter_stub0, pf_enter_stub1, pf_enter_stub2, pf_enter_stub3, pf_enter_stub4,
    pf_enter_stub5, pf_enter_stub6, pf_enter_stub7, pf_enter_stub8, pf_enter_stub9,
    pf_enter_stub10, pf_enter_stub11, pf_enter_stub12, pf_enter_stub13, pf_enter_stub14,
    pf_enter_stub15, pf_enter_stub16, pf_enter_stub17, pf_enter_stub18, pf_enter_stub19,
    pf_enter_stub20, pf_enter_stub21, pf_enter_stub22, pf_enter_stub23, pf_enter_stub24,
    pf_enter_stub25, pf_enter_stub26, pf_enter_stub27, pf_enter_stub28, pf_enter_stub29,
    pf_enter_stub30, pf_enter_stub31, pf_enter_stub32, pf_enter_stub33, pf_enter_stub34,
    pf_enter_stub35, pf_enter_stub36, pf_enter_stub37, pf_enter_stub38, pf_enter_stub39,
    pf_enter_stub40, pf_enter_stub41, pf_enter_stub42, pf_enter_stub43, pf_enter_stub44,
    pf_enter_stub45, pf_enter_stub46,
};
void *const pf_exit_stubs[PF_SITE_COUNT] = {
    pf_exit_stub0, pf_exit_stub1, pf_exit_stub2, pf_exit_stub3, pf_exit_stub4,
    pf_exit_stub5, pf_exit_stub6, pf_exit_stub7, pf_exit_stub8, pf_exit_stub9,
    pf_exit_stub10, pf_exit_stub11, pf_exit_stub12, pf_exit_stub13, pf_exit_stub14,
    pf_exit_stub15, pf_exit_stub16, pf_exit_stub17, pf_exit_stub18, pf_exit_stub19,
    pf_exit_stub20, pf_exit_stub21, pf_exit_stub22, pf_exit_stub23, pf_exit_stub24,
    pf_exit_stub25, pf_exit_stub26, pf_exit_stub27, pf_exit_stub28, pf_exit_stub29,
    pf_exit_stub30, pf_exit_stub31, pf_exit_stub32, pf_exit_stub33, pf_exit_stub34,
    pf_exit_stub35, pf_exit_stub36, pf_exit_stub37, pf_exit_stub38, pf_exit_stub39,
    pf_exit_stub40, pf_exit_stub41, pf_exit_stub42, pf_exit_stub43, pf_exit_stub44,
    pf_exit_stub45, pf_exit_stub46,
};

/* Where a skipped call continues: the stack is as at the callee's entry,
 * so popping its arguments returns to the caller as the callee would. */
__asm__(".text\n"
        ".p2align 2\n"
        "_pf_ret0:\n"
        "    ret\n"
        ".p2align 2\n"
        "_pf_ret4:\n"
        "    ret $4\n"
        ".p2align 2\n"
        "_pf_ret8:\n"
        "    ret $8\n"
        ".p2align 2\n"
        "_pf_ret12:\n"
        "    ret $12\n");

extern char pf_ret0[], pf_ret4[], pf_ret8[], pf_ret12[];

void *pf_skip_thunk(unsigned arg_bytes)
{
    switch (arg_bytes) {
    case 0: return pf_ret0;
    case 4: return pf_ret4;
    case 8: return pf_ret8;
    case 12: return pf_ret12;
    }
    return 0;
}
