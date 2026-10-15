/* dinput8.dll proxy: patches the game from DllMain, forwards the six dinput8
 * exports to the system DLL. The system DLL is loaded on the first forwarded
 * call, never inside DllMain (no LoadLibrary under the loader lock). */
#include <windows.h>

#include "unlocker.h"

#define DIERR_GENERIC_ E_FAIL

static HMODULE volatile g_real;

static FARPROC real_proc(const char *name)
{
    HMODULE h = g_real;
    if (!h) {
        WCHAR path[MAX_PATH];
        static const WCHAR file[] = L"\\dinput8.dll";
        UINT n = GetSystemDirectoryW(path, MAX_PATH), i;
        if (n == 0 || n + sizeof file / sizeof file[0] > MAX_PATH)
            return NULL;
        for (i = 0; i < sizeof file / sizeof file[0]; i++)
            path[n + i] = file[i];
        h = LoadLibraryW(path);
        if (!h)
            return NULL;
        if (InterlockedCompareExchangePointer((void *volatile *)&g_real, h, NULL) != NULL) {
            FreeLibrary(h);
            h = g_real;
        }
    }
    return GetProcAddress(h, name);
}

#define REAL(type, name) ((type)(void (*)(void))real_proc(name))

typedef HRESULT(WINAPI *pfn_create)(HINSTANCE, DWORD, REFIID, LPVOID *, LPVOID);
typedef HRESULT(WINAPI *pfn_void)(void);
typedef HRESULT(WINAPI *pfn_class)(REFCLSID, REFIID, LPVOID *);
typedef LPCVOID(WINAPI *pfn_format)(void);

HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID *out,
                                  LPVOID outer)
{
    pfn_create f = REAL(pfn_create, "DirectInput8Create");
    if (!f) {
        if (out)
            *out = NULL;
        return DIERR_GENERIC_;
    }
    return f(inst, version, riid, out, outer);
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    pfn_void f = REAL(pfn_void, "DllCanUnloadNow");
    return f ? f() : S_FALSE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID *out)
{
    pfn_class f = REAL(pfn_class, "DllGetClassObject");
    if (!f) {
        if (out)
            *out = NULL;
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    return f(clsid, riid, out);
}

HRESULT WINAPI DllRegisterServer(void)
{
    pfn_void f = REAL(pfn_void, "DllRegisterServer");
    return f ? f() : E_FAIL;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    pfn_void f = REAL(pfn_void, "DllUnregisterServer");
    return f ? f() : E_FAIL;
}

LPCVOID WINAPI GetdfDIJoystick(void)
{
    pfn_format f = REAL(pfn_format, "GetdfDIJoystick");
    return f ? f() : NULL;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        fu_startup(inst, GetModuleHandleW(NULL));
    } else if (reason == DLL_PROCESS_DETACH) {
        fu_shutdown();
    }
    /* Never fail the load: game.dat imports dinput8 statically and would not start. */
    return TRUE;
}
