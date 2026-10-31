/* Miles-loaded alternative to the DirectInput proxy. Tacitus keeps dinput8.dll. */
#include <windows.h>
#include "gameversion.h"
#include "log.h"
#include "unlocker.h"

static HANDLE g_instance;
static int g_started;

static void refused(HMODULE self, const char *reason)
{
    WCHAR path[MAX_PATH];
    DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    if (n < 4 || n >= MAX_PATH)
        return;
    path[n-3] = L'l'; path[n-2] = L'o'; path[n-1] = L'g';
    fu_log_open(path);
    fu_logf("sage-boost unlocker ASI: refused: %s; nothing patched", reason);
    fu_log_close();
}

static int legacy_proxy_loaded(void)
{
    static const WCHAR name[] = L"bfme2-faction-unlocker";
    HMODULE proxy = GetModuleHandleW(L"dinput8.dll");
    fu_game_version version;
    unsigned i;
    if (!proxy)
        return 0;
    fu_game_version_read(proxy, &version);
    for (i = 0; name[i] && name[i] == version.internal_name[i]; i++)
        ;
    return !name[i] && !version.internal_name[i];
}

int __stdcall RIB_Main(unsigned provider, unsigned up_down)
{
    (void)provider;
    (void)up_down;
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        WCHAR lock[] = L"Local\\sage-boost-unlocker-00000000";
        static const WCHAR hex[] = L"0123456789ABCDEF";
        DWORD pid = GetCurrentProcessId(), error;
        HMODULE pinned;
        unsigned i, n = sizeof(lock) / sizeof(lock[0]) - 1;
        DisableThreadLibraryCalls(inst);
        if (legacy_proxy_loaded()) {
            refused(inst, "sage-boost dinput8 proxy already loaded; install only one unlocker");
            return TRUE;
        }
        for (i = 0; i < 8; i++) {
            lock[n-1-i] = hex[pid & 15];
            pid >>= 4;
        }
        g_instance = CreateMutexW(NULL, FALSE, lock);
        error = GetLastError();
        if (!g_instance || error == ERROR_ALREADY_EXISTS) {
            if (g_instance) CloseHandle(g_instance);
            g_instance = NULL;
            refused(inst, "another unlocker ASI owns this process, or instance guard failed");
            return TRUE;
        }
        /* Miles may release non-codec providers. Hooks must remain resident. */
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)inst, &pinned)) {
            CloseHandle(g_instance);
            g_instance = NULL;
            refused(inst, "could not pin hook module");
            return TRUE;
        }
        g_started = 1;
        fu_startup(inst, GetModuleHandleW(NULL));
    } else if (reason == DLL_PROCESS_DETACH && g_started) {
        fu_shutdown();
        if (g_instance) CloseHandle(g_instance);
    }
    return TRUE;
}
