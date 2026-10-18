#include "bulk.h"
#include "instbulk.h"
#include "timing.h"
#include "log.h"

/* Assembly reads these symbols directly. No executable addresses are
 * inferred here: the installer supplies the verified native continuations. */
void *pf_bulk_original, *pf_bulk_resume, *pf_bulk_fallback;
typedef struct {
    void *previous, *handler;
    unsigned owned;
} bulk_guard;
static void *volatile g_owner;
static DWORD g_owner_thread;
static unsigned g_enabled, g_consumed;

int pf_bulk_bind(void *original, void *resume, void *fallback)
{
    if (g_owner || g_enabled || !original || !resume || !fallback)
        return 0;
    pf_bulk_original = original;
    pf_bulk_resume = resume;
    pf_bulk_fallback = fallback;
    return 1;
}

int pf_bulk_enable(int on)
{
    if (g_owner || (on && (!pf_bulk_original || !pf_bulk_resume || !pf_bulk_fallback)))
        return 0;
    g_enabled = on != 0;
    g_consumed = 0;
    return 1;
}

unsigned pf_bulk_consumed(void) { return g_consumed; }

unsigned __attribute__((used)) pf_bulk_enter_group(bulk_guard *guard)
{
    if (!pf_timing_render_thread())
        return 0;
    DWORD thread = GetCurrentThreadId();
    if (InterlockedCompareExchangePointer(&g_owner, guard, NULL)) {
        if (g_owner_thread == thread) {
            /* The old group's material is still current at this boundary.
             * Replay pending units before the nested native group changes it. */
            pf_submit_result result = pf_inst_bulk_cancel();
            g_enabled = 0;
            pf_timing_bulk_abandon();
            pf_inst_bulk_finish(1);
            fu_log_event("bulk: nested native rendering; instancing disabled, "
                         "pending replay coherent=%u complete=%u", result.coherent, result.complete);
        }
        return 0;
    }
    g_owner_thread = thread;
    /* Keep the lifetime boundary even after batching switches off. */
    pf_inst_bulk_context(g_enabled);
    return 1;
}

void __attribute__((used)) pf_bulk_leave_group(bulk_guard *guard, unsigned unwinding)
{
    if (!guard->owned || g_owner != guard)
        return;
    /* Do not call the renderer or draw again while native frames unwind. */
    pf_timing_bulk_abandon();
    pf_inst_bulk_finish(unwinding != 0);
    guard->owned = 0;
    g_owner_thread = 0;
    InterlockedExchangePointer(&g_owner, NULL);
}

int __attribute__((used)) pf_bulk_exception(EXCEPTION_RECORD *record, bulk_guard *guard,
                                           CONTEXT *context, void *dispatch)
{
    (void)context; (void)dispatch;
    if (record->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
        pf_bulk_leave_group(guard, 1);
    return 1; /* ExceptionContinueSearch: never catch or resume a native exception. */
}

static void submission_failed(pf_submit_result result)
{
    g_enabled = 0;
    fu_log_event("bulk: submission stopped: error=%08X operation=%u committed=%u replayed=%u "
                 "coherent=%u complete=%u; current frame may be incomplete",
                 (unsigned)result.error, result.failed_operation, result.committed,
                 result.replayed, result.coherent, result.complete);
    pf_inst_bulk_finish(1);
}

int pf_bulk_dispatch(unsigned *regs)
{
    if (!g_enabled || !g_owner || GetCurrentThreadId() != g_owner_thread)
        return 0;
    unsigned *frame = (unsigned *)regs[2];
    const unsigned char *current = (const void *)regs[0], *first = (const void *)regs[4];
    const unsigned char *end = (const void *)frame[2];
    void *method = (void *)regs[1];
    if (current <= first || current >= end || frame[-1] != 1 || frame[-9] != 1 ||
        frame[-11] != 1 || regs[6] != 1 || frame[-7] != (unsigned)(current + PF_RECORD_SIZE) ||
        (*(const unsigned *)(first + 4) - 1u) > 5u || !pf_inst_bulk_matches(current, end, method))
        return 0;
    if (!pf_timing_bulk_begin()) {
        pf_submit_result result = pf_inst_bulk_cancel();
        if (!result.coherent || !result.complete)
            submission_failed(result);
        return 0;
    }
    pf_submit_result result;
    unsigned n = pf_inst_bulk_advance(current, end, method, &result);
    if (!n) {
        if (!pf_timing_bulk_cancel()) {
            pf_timing_bulk_abandon();
            submission_failed((pf_submit_result){E_UNEXPECTED, 0, 0, 0, 1, 0});
            return 0;
        }
        result = pf_inst_bulk_cancel();
        if (!result.coherent || !result.complete)
            submission_failed(result);
        return 0;
    }
    /* Even a failed submission has consumed these staged records. Advancing
     * them avoids duplicating a batch that committed before restore failed.
     * Completion is separately reported, never inferred from this cursor. */
    unsigned remaining = (unsigned)(end - current) / PF_RECORD_SIZE;
    if (n > remaining) {
        n = remaining;
        result = (pf_submit_result){E_UNEXPECTED, 0, 0, 0, 1, 0};
    }
    if (!pf_timing_bulk_end(n)) {
        pf_timing_bulk_abandon();
        result = (pf_submit_result){E_UNEXPECTED, 0, 0, 0, 1, 0};
    }
    if (!result.coherent || !result.complete)
        submission_failed(result);
    g_consumed += n;
    frame[-7] = (unsigned)(current + n * PF_RECORD_SIZE);
    return 1;
}

/* The group is a thiscall with two stack arguments. The guard stays below
 * the wrapper's saved registers, enclosing the native SEH frame. Its
 * unwind handler releases only plugin-owned state and continues unwinding.
 * No floating-point register/control state is read or written. */
__asm__(
".text\n.globl _pf_bulk_entry\n_pf_bulk_entry:\n"
"pushl %ebp\nmovl %esp,%ebp\npushl %ebx\npushl %esi\npushl %edi\nsubl $16,%esp\n"
"movl %ecx,%ebx\nleal -28(%ebp),%eax\npushl %eax\ncall _pf_bulk_enter_group\naddl $4,%esp\n"
"movl %eax,-20(%ebp)\nmovl %fs:0,%eax\nmovl %eax,-28(%ebp)\n"
"movl $_pf_bulk_exception,-24(%ebp)\nleal -28(%ebp),%eax\nmovl %eax,%fs:0\n"
"pushl 12(%ebp)\npushl 8(%ebp)\nmovl %ebx,%ecx\ncall *_pf_bulk_original\nmovl %eax,-16(%ebp)\n"
"pushl $0\nleal -28(%ebp),%eax\npushl %eax\ncall _pf_bulk_leave_group\naddl $8,%esp\n"
"movl -28(%ebp),%eax\nmovl %eax,%fs:0\nmovl -16(%ebp),%eax\n"
"leal -12(%ebp),%esp\npopl %edi\npopl %esi\npopl %ebx\npopl %ebp\nret $8\n"
".globl _pf_bulk_bridge\n_pf_bulk_bridge:\n"
"pushfl\npushal\npushl %esp\ncall _pf_bulk_dispatch\naddl $4,%esp\ntestl %eax,%eax\nje 1f\n"
"popal\npopfl\njmp *_pf_bulk_resume\n1:\npopal\npopfl\nleal -0x134(%ebp),%eax\n"
"jmp *_pf_bulk_fallback\n");
