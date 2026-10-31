/* Run each loader scenario in its own process: the ASI deliberately pins itself. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (__stdcall *rib_fn)(unsigned, unsigned);
int main(int argc, char **argv)
{
    HMODULE module;
    rib_fn rib, decorated;
    if (argc != 3) return 1;
    /* Map a proxy's resources without executing third-party code. */
    if (strcmp(argv[1], "none") &&
        !LoadLibraryExA(argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES)) return 2;
    module = LoadLibraryA("bfme2unlocker.asi");
    if (!module) return 3;
    rib = (rib_fn)(void *)GetProcAddress(module, "RIB_Main");
    decorated = (rib_fn)(void *)GetProcAddress(module, "_RIB_Main@8");
    if (!rib || !decorated || !rib(0, 1) || !decorated(0, 0)) return 4;
    if (GetProcAddress(module, "DirectInput8Create")) return 5;
    if (!strcmp(argv[2], "duplicate") && !LoadLibraryA("duplicate.asi")) return 6;
    FreeLibrary(module);
    if (strcmp(argv[2], "legacy") && !GetModuleHandleA("bfme2unlocker.asi")) return 7;
    puts("ASI loader passed");
    return 0;
}
