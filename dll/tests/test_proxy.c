/* Loads the built proxy into a non-game process: it must refuse to patch, keep
 * forwarding dinput8, honour Enabled=0, and export the dinput8 ordinals.
 * Usage: test_proxy.exe <path to build/dinput8.dll> */
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int g_failures;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            g_failures++;                                 \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

static const GUID IID_IDirectInput8A_ = {
    0xBF798030, 0x483A, 0x4DA2, { 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00 }
};

typedef HRESULT(WINAPI *pfn_create)(HINSTANCE, DWORD, REFIID, LPVOID *, LPVOID);
typedef ULONG(WINAPI *pfn_release)(void *);
typedef LPCVOID(WINAPI *pfn_format)(void);

static int read_file(const WCHAR *path, char *buf, unsigned cap)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, 0, NULL);
    DWORD got = 0;
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    ReadFile(h, buf, cap - 1, &got, NULL);
    CloseHandle(h);
    buf[got] = 0;
    return 1;
}

static void write_file(const WCHAR *path, const char *text)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD put;
    WriteFile(h, text, (DWORD)strlen(text), &put, NULL);
    CloseHandle(h);
}

static WCHAR g_dirs[2][MAX_PATH];
static unsigned g_ndirs;

static void cleanup(void)
{
    static const WCHAR *files[] = { L"dinput8.dll", L"dinput8.ini", L"dinput8.log" };
    unsigned i, k;
    for (i = 0; i < g_ndirs; i++) {
        for (k = 0; k < 3; k++) {
            WCHAR path[MAX_PATH];
            swprintf(path, MAX_PATH, L"%ls\\%ls", g_dirs[i], files[k]);
            DeleteFileW(path);
        }
        RemoveDirectoryW(g_dirs[i]);
    }
}

/* Copies the DLL into a fresh temp folder (optionally with an INI), loads it
 * and returns the module; *log receives dinput8.log. */
static HMODULE stage(const WCHAR *dll, const char *tag, const char *ini, char *log, unsigned cap)
{
    WCHAR *dir = g_dirs[g_ndirs++], path[MAX_PATH];
    HMODULE h;
    GetTempPathW(MAX_PATH, dir);
    swprintf(dir + wcslen(dir), MAX_PATH - wcslen(dir), L"fu-proxy-test-%lu-%hs",
             GetCurrentProcessId(), tag);
    CreateDirectoryW(dir, NULL);
    swprintf(path, MAX_PATH, L"%ls\\dinput8.ini", dir);
    DeleteFileW(path);
    if (ini)
        write_file(path, ini);
    swprintf(path, MAX_PATH, L"%ls\\dinput8.dll", dir);
    CHECK(CopyFileW(dll, path, FALSE), "copy to %ls failed", path);
    h = LoadLibraryW(path);
    CHECK(h != NULL, "LoadLibrary(%ls) failed: %lu", path, GetLastError());
    swprintf(path, MAX_PATH, L"%ls\\dinput8.log", dir);
    log[0] = 0;
    CHECK(read_file(path, log, cap), "no log written at %ls", path);
    return h;
}

static void check_forwarding(HMODULE h)
{
    pfn_create create = (pfn_create)(void (*)(void))GetProcAddress(h, "DirectInput8Create");
    pfn_format joy = (pfn_format)(void (*)(void))GetProcAddress(h, "GetdfDIJoystick");
    void *di = NULL;
    HRESULT hr;
    const char *names[] = { "DirectInput8Create", "DllCanUnloadNow", "DllGetClassObject",
                            "DllRegisterServer", "DllUnregisterServer", "GetdfDIJoystick" };
    unsigned i;

    for (i = 0; i < 6; i++) {
        FARPROC by_name = GetProcAddress(h, names[i]);
        FARPROC by_ord = GetProcAddress(h, MAKEINTRESOURCEA(i + 1));
        CHECK(by_name && by_name == by_ord, "export %s / ordinal %u", names[i], i + 1);
    }
    CHECK(create != NULL, "no DirectInput8Create");
    if (!create)
        return;
    hr = create(GetModuleHandleW(NULL), 0x0800, &IID_IDirectInput8A_, &di, NULL);
    CHECK(hr == S_OK && di != NULL, "DirectInput8Create hr=0x%08lX", (unsigned long)hr);
    if (di) {
        pfn_release release = (pfn_release)(*(void ***)di)[2];
        release(di);
    }
    CHECK(joy && joy() != NULL, "GetdfDIJoystick returned NULL");
}

int main(int argc, char **argv)
{
    WCHAR arg[MAX_PATH], dll[MAX_PATH];
    char log[8192];
    HMODULE a, b;

    if (argc < 2) {
        printf("usage: %s <dinput8.dll>\n", argv[0]);
        return 2;
    }
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, arg, MAX_PATH);
    GetFullPathNameW(arg, MAX_PATH, dll, NULL);

    printf("master switch off\n");
    a = stage(dll, "off", "[FactionUnlocker]\r\nEnabled=0\r\n", log, sizeof log);
    printf("%s", log);
    CHECK(strstr(log, "ini : ") && strstr(log, "(found)"), "INI not reported as found");
    CHECK(strstr(log, "master switch off") != NULL, "master=0 not honoured");
    CHECK(strstr(log, "game:") == NULL, "version gate ran although master=0");
    if (a)
        check_forwarding(a);

    printf("no INI, non-game host\n");
    b = stage(dll, "nogame", NULL, log, sizeof log);
    printf("%s", log);
    CHECK(b != a, "second copy resolved to the first module");
    CHECK(strstr(log, "missing, using defaults") != NULL, "missing INI not reported");
    CHECK(strstr(log, "not BFME2 1.06 or ROTWK 2.01 -> refused") != NULL, "non-game host not refused");
    if (b)
        check_forwarding(b);

    if (a)
        FreeLibrary(a);
    if (b)
        FreeLibrary(b);
    cleanup();
    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: proxy tests passed\n");
    return 0;
}
