/* Actual production wrapper/SEH and instancer ownership; no GPU or game.
 * The COM double dies on its last reference, exposing delayed releases. */
#define main ownership_baseline_main
#include "test_inst_ownership.c"
#undef main
#include "bulk.h"
#include <setjmp.h>

static DWORD test_thread;
static unsigned mode, depth, catches, abandoned;
static jmp_buf landing;
static void *outer_chain;
int pf_timing_render_thread(void) { return GetCurrentThreadId() == test_thread; }
int pf_timing_bulk_begin(void) { return 1; }
int pf_timing_bulk_end(unsigned n) { return n != 0; }
int pf_timing_bulk_cancel(void) { return 1; }
void pf_timing_bulk_abandon(void) { abandoned++; }

static void __attribute__((thiscall)) native_group(void *self, void *first, void *end)
{
    REQUIRE(self == (void *)0x1234 && first == (void *)0x5678 && end == (void *)0x9abc);
    depth++;
    if (depth == 1) {
        REQUIRE(g_bulk_context == (mode != 4));
        capture_leader();
        REQUIRE(g_stream_owner == &native_vb && native_vb.refs == 2);
        if (mode == 1) {
            g.mode = M_INST; g.count = 2;
            RaiseException(0xe0424242, 0, 0, NULL);
            REQUIRE(0);
        } else if (mode == 2) {
            /* A native early return cannot replay after material cleanup. */
            g.mode = M_INST; g.count = 2;
        } else if (mode == 3) {
            pf_bulk_entry(self, first, end);
            REQUIRE(!g_stream_owner && !g_bulk_context);
        } else if (mode == 5 || mode == 6) {
            /* Preserve the failed transaction result while releasing its
             * owner; coherent state alone is not complete unit submission. */
            g_bulk_capture = 1;
            g_bulk_result = (pf_submit_result){E_FAIL, 11, 0, 1, mode == 6, mode == 5};
        }
    } else {
        REQUIRE(mode == 3 && depth == 2 && !g_bulk_context && !g_stream_owner);
    }
    depth--;
}

void wrapper_test_call(void)
{
    pf_bulk_entry((void *)0x1234, (void *)0x5678, (void *)0x9abc);
}

int __attribute__((used)) wrapper_test_handler(EXCEPTION_RECORD *record, void *frame,
                                              CONTEXT *context, void *dispatch)
{
    (void)context; (void)dispatch;
    if (!(record->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) &&
        record->ExceptionCode == 0xe0424242) {
        catches++;
        /* Ask Windows to unwind through the real production registration
         * before escaping the test's outer handler. */
        RtlUnwind(frame, NULL, record, NULL);
        longjmp(landing, 1);
    }
    return 1; /* ExceptionContinueSearch */
}

void wrapper_test_outer(void);
__asm__(
".text\n.globl _wrapper_test_outer\n_wrapper_test_outer:\n"
"pushl %ebp\nmovl %esp,%ebp\nsubl $8,%esp\nmovl %fs:0,%eax\nmovl %eax,-8(%ebp)\n"
"movl $_wrapper_test_handler,-4(%ebp)\nmovl %esp,%fs:0\ncall _wrapper_test_call\n"
"movl -8(%ebp),%eax\nmovl %eax,%fs:0\nleave\nret\n");

int main(void)
{
    test_thread = GetCurrentThreadId();
    REQUIRE(pf_bulk_bind(native_group, native_group, native_group));
    for (mode = 0; mode < 7; mode++) {
        reset_case(0); depth = abandoned = 0;
        REQUIRE(pf_bulk_enable(mode != 4));
        __asm__ volatile("movl %%fs:0,%0" : "=r"(outer_chain));
        if (!setjmp(landing)) {
            wrapper_test_outer();
            REQUIRE(mode != 1);
        } else {
            REQUIRE(mode == 1 && catches == 1);
            __asm__ volatile("movl %0,%%fs:0" : : "r"(outer_chain));
        }
        void *after;
        __asm__ volatile("movl %%fs:0,%0" : "=r"(after));
        REQUIRE(after == outer_chain && !g_stream_owner && !g_bulk_context);
        REQUIRE(!g.count && !g_submit.open && !draws && !dead_uses);
        REQUIRE(native_vb.refs == 1 && abandoned >= 1);
        REQUIRE(g_failed == (mode != 0 && mode != 4));
        if (mode == 5 || mode == 6)
            REQUIRE(g_bulk_result.coherent == (mode == 6) &&
                    g_bulk_result.complete == (mode == 5));
        REQUIRE(pf_bulk_enable(0)); /* owner is no longer live */
    }
    puts("BULK_LIFETIME,cases,7,unwind_caught,1,dead_uses,0,late_draws,0");
    return 0;
}
