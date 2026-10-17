/* Startup sequence run from DllMain: config, version gate, patch, log. */
#ifndef FU_UNLOCKER_H
#define FU_UNLOCKER_H

#include <windows.h>

#define FU_VERSION_STRING "0.5.0"

/* `self` locates the INI and log; `game` is the module to patch (the host
 * executable in production, a mapped test image in the tests). */
void fu_startup(HMODULE self, HMODULE game);

/* Appends per-hook hit counts to the log (DLL_PROCESS_DETACH). */
void fu_shutdown(void);

#endif
