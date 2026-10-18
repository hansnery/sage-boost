/* Offline fixture access to the real extra-frame dispatch path. Never
 * linked into the ASI; does not execute the scheduler or launch a game. */
#include "inst.h"
#include "../src/fps.c"

void bench_extra_mode(int on)
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_freq = f.QuadPart;
    g_main = GetCurrentThreadId();
    set_extra(on);
    pf_inst_set_extra(on);
}
