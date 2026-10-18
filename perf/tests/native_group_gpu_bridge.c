/* Offline bridge, compiled with production general-register flags. Native
 * exception unwinding and GPU failure recovery are not proved here. */
#include <windows.h>
#include "inst.h"
extern int bench_native_bulk_matches(const unsigned char *,const unsigned char *,void *);
extern unsigned bench_inst_bulk(const unsigned char *,const unsigned char *,void *);
extern int bench_scope_bulk_begin(void),bench_scope_bulk_end(unsigned);
void *ng_group_resume,*ng_group_fallback;
unsigned ng_group_abi_errors;
static unsigned enabled,consumed,visits;

void bench_native_group_mode(unsigned on) { enabled=on; consumed=visits=0; }
unsigned bench_native_group_consumed(void) { return consumed; }

int bench_native_group_bulk(unsigned *regs)
{
    unsigned *frame=(unsigned *)regs[2];
    const unsigned char *current=(const void *)regs[0],*first=(const void *)regs[4];
    const unsigned char *end=(const void *)frame[2];
    void *method=(void *)regs[1];
    visits++;
    if(!enabled || current<=first || current>=end ||
       frame[-1]!=1 || frame[-9]!=1 || frame[-11]!=1 || regs[6]!=1 ||
       frame[-7]!=(unsigned)(current+PF_RECORD_SIZE) || *(const unsigned *)(first+4)!=1 ||
       !bench_native_bulk_matches(current,end,method)) return 0;
    if(!bench_scope_bulk_begin()) ExitProcess(4);
    unsigned n=bench_inst_bulk(current,end,method);
    /* No production fallback claim: zero-consumption/failure after this
     * preflight must fail the fixture until recovery has been proved. */
    if(!n || n>(unsigned)(end-current)/PF_RECORD_SIZE || !bench_scope_bulk_end(n))
        ExitProcess(4);
    consumed+=n;
    frame[-7]=(unsigned)(current+n*PF_RECORD_SIZE);
    return 1;
}

void ng_group_bridge(void);
void ng_group_call(void *,void *,void *,void *);
__asm__(
".text\n.globl _ng_group_bridge\n_ng_group_bridge:\n"
"pushfl\npushal\npushl %esp\ncall _bench_native_group_bulk\naddl $4,%esp\ntestl %eax,%eax\nje 1f\n"
"popal\npopfl\njmp *_ng_group_resume\n1:\npopal\npopfl\nleal -0x134(%ebp),%eax\njmp *_ng_group_fallback\n"
".globl _ng_group_call\n_ng_group_call:\n"
"pushl %ebp\nmovl %esp,%ebp\npushl %ebx\npushl %esi\npushl %edi\nsubl $8,%esp\n"
"movl %esp,-16(%ebp)\nmovl %fs:0,%eax\nmovl %eax,-20(%ebp)\n"
"movl 8(%ebp),%eax\nmovl 12(%ebp),%ecx\npushl 20(%ebp)\npushl 16(%ebp)\n"
"movl $0x13579bdf,%ebx\nmovl $0x2468ace0,%esi\nmovl $0x76543210,%edi\ncall *%eax\n"
"cmpl -16(%ebp),%esp\nje 2f\nincl _ng_group_abi_errors\n2:\n"
"cmpl $0x13579bdf,%ebx\nje 3f\nincl _ng_group_abi_errors\n3:\n"
"cmpl $0x2468ace0,%esi\nje 4f\nincl _ng_group_abi_errors\n4:\n"
"cmpl $0x76543210,%edi\nje 5f\nincl _ng_group_abi_errors\n5:\n"
"movl %fs:0,%eax\ncmpl -20(%ebp),%eax\nje 6f\nincl _ng_group_abi_errors\n6:\n"
"leal -12(%ebp),%esp\npopl %edi\npopl %esi\npopl %ebx\npopl %ebp\nret\n");
