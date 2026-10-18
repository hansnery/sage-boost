/* Loads the built plugin the way Miles does (LoadLibrary from a folder of
 * its own, then RIB_Main) and checks the log, the exports and the pin.
 * Usage: test_load.exe <path to build/bfme2perf.asi> */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
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

typedef int(__stdcall *pfn_rib_main)(unsigned, unsigned);

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

int main(int argc, char **argv)
{
    WCHAR arg[MAX_PATH], src[MAX_PATH], dir[MAX_PATH], dll[MAX_PATH], log_path[MAX_PATH],
        csv_path[MAX_PATH], ini_path[MAX_PATH], scopes_path[MAX_PATH];
    char log[8192];
    pfn_rib_main decorated, plain;
    HMODULE h;

    if (argc < 2) {
        printf("usage: %s <bfme2perf.asi>\n", argv[0]);
        return 2;
    }
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, arg, MAX_PATH);
    GetFullPathNameW(arg, MAX_PATH, src, NULL);

    /* A fixed folder: the pinned copy from the last run is free again once
     * that run's process has exited. */
    GetTempPathW(MAX_PATH, dir);
    wcscat(dir, L"bfme2perf-test");
    CreateDirectoryW(dir, NULL);
    swprintf(dll, MAX_PATH, L"%ls\\bfme2perf.asi", dir);
    swprintf(log_path, MAX_PATH, L"%ls\\bfme2perf.log", dir);
    swprintf(csv_path, MAX_PATH, L"%ls\\bfme2perf_timing.csv", dir);
    swprintf(ini_path, MAX_PATH, L"%ls\\bfme2perf.ini", dir);
    swprintf(scopes_path, MAX_PATH, L"%ls\\bfme2perf_scopes.csv", dir);
    DeleteFileW(log_path);
    DeleteFileW(csv_path);
    DeleteFileW(scopes_path);
    DeleteFileW(ini_path);
    CHECK(CopyFileW(src, dll, FALSE), "copy to %ls failed: %lu", dll, GetLastError());

    printf("load\n");
    h = LoadLibraryW(dll);
    CHECK(h != NULL, "LoadLibrary(%ls) failed: %lu", dll, GetLastError());
    if (!h)
        goto done;
    CHECK(read_file(log_path, log, sizeof log), "no log at %ls", log_path);
    printf("%s", log);
    CHECK(strstr(log, "bfme2perf 0.9.11-beta.4\r\n") == log, "first line is not the version");
    CHECK(strstr(log, "self: ") && strstr(log, "bfme2perf-test\\bfme2perf.asi"), "no self path");
    CHECK(strstr(log, "host: ") && strstr(log, "test_load.exe"), "no host path");
    CHECK(strstr(log, "pinned=1") != NULL, "not pinned");
    CHECK(strstr(log, "module dinput8.dll: ") != NULL, "no dinput8 line");
    CHECK(strstr(log, "module d3d9.dll: ") != NULL, "no d3d9 line");
    CHECK(strstr(log, "module mss32.dll: not loaded") != NULL, "no mss32 line");
    CHECK(strstr(log, "game: unknown") != NULL, "no game line");
    CHECK(strstr(log, "(missing, using defaults)") != NULL, "missing INI not reported");
    CHECK(strstr(log, "settings: [Perf] Enabled=1 Log=1, [Timing] Enabled=1 Detail=1, "
                      "[Render] ReflectionInterval=1 ShadowInterval=1 ShadowFullWhileScrolling=0 "
                      "Batching=0 QueueInsert=1 Instancing=0 InstancingVerify=1 ShadowInstancing=1 MaxFPS=30 "
                      "FrameSmoothing=3 "
                      "ExtraFrameShadows=0 ExtraFrameReflections=0 "
                      "ShadowBatchIgnoreLight=1 NoStencilFlushes=1 ShadowSkipParticles=1 "
                      "ShadowPassVerify=0 LightSkipShadow=0 LightCull=1 LightReuse=1 "
                      "LightVerify=1, "
                      "[CatchUp] Enabled=1 MaxSkips=1 Notice=1 NoticeText=Catching up "
                      "NoticeMinSkips=6, "
                      "[Skeleton] Threads=0 Verify=0") != NULL,
          "no settings line");
    /* No game code: catch-up finds nothing and stays off. */
    CHECK(strstr(log, "render: QueueInsert=0: entry signature missing") != NULL,
          "queue insertion not refused without the game");
    CHECK(strstr(log, "render: catch-up: ") != NULL &&
              strstr(log, "not found, renders are never skipped") != NULL,
          "catch-up not reported off without the game");
    CHECK(strstr(log, "hook catchup_render (W3DDisplay's frame, the start of its render): off") !=
              NULL,
          "catch-up site not left off without the game");
    /* Nor renderOneObject: the light cuts and timers stay off. */
    CHECK(strstr(log, "render: renderOneObject's light lists doesn't match: LightSkipShadow, "
                      "LightCull, LightReuse and the renderOneObject timers stay off") != NULL,
          "light cuts not reported off without the game");
    /* The test exe has no game code: the shadow-pass fixes find no flag. */
    CHECK(strstr(log, "ShadowBatchIgnoreLight, NoStencilFlushes, ShadowSkipParticles and "
                      "ShadowPassVerify stay off") != NULL,
          "shadow-pass fixes not reported off without the game");
    CHECK(strstr(log, "render: Instancing=0, the game draws every unit mesh (no hook)") != NULL,
          "no instancing line");
    CHECK(strstr(log, "hook inst_mesh (the FX shader batch, the per-mesh Render_With_FX_Material "
                      "call): off") != NULL,
          "instancing hook not left off at Instancing=0");
    CHECK(strstr(log, "hook inst_dip (BfmeDrawOps::Draw, the DrawIndexedPrimitive call): off") !=
              NULL,
          "draw hook not left off at Instancing=0");
    CHECK(strstr(log, "render: ReflectionInterval=1") != NULL, "no reflection line");
    CHECK(strstr(log, "render: ShadowInterval=1") != NULL, "no shadow line");
    CHECK(strstr(log, "hook reflect (W3DDisplay::draw, the water reflection update call): off") !=
              NULL,
          "reflection hook not left off at interval 1");
    CHECK(strstr(log, "hook shadow (W3DDisplay::draw, the UpdateShadowMap call): off") != NULL,
          "shadow hook not left off at interval 1");
    CHECK(strstr(log, "hook shadow_info (the SetShadowMapInfo call in the shadow matrix setter): "
                      "off") != NULL,
          "shadow matrix hook not left off at interval 1");
    CHECK(strstr(log, "exe : .text crc32 ") != NULL, "no exe hash");
    CHECK(strstr(log, "hook frame (GameEngine::update): REFUSED (signature not found)") != NULL,
          "frame hook not refused in a non-game host");
    CHECK(strstr(log, "hook event_end (BFME2ScopedRenderEvent::~BFME2ScopedRenderEvent): "
                      "REFUSED (signature not found)") != NULL,
          "marker hook not refused in a non-game host");
    CHECK(strstr(log, "hook fx_group (the FX shader batch, the per-group setup call): REFUSED "
                      "(signature not found)") != NULL,
          "FX group hook not refused in a non-game host");
    CHECK(strstr(log, "timing: no hooks installed") != NULL, "no 'no hooks' line");
    CHECK(GetFileAttributesW(csv_path) == INVALID_FILE_ATTRIBUTES, "CSV left behind");
    CHECK(GetFileAttributesW(scopes_path) == INVALID_FILE_ATTRIBUTES, "scopes CSV left behind");

    printf("RIB_Main\n");
    decorated = (pfn_rib_main)(void (*)(void))GetProcAddress(h, "_RIB_Main@8");
    plain = (pfn_rib_main)(void (*)(void))GetProcAddress(h, "RIB_Main");
    CHECK(decorated != NULL, "no export _RIB_Main@8");
    CHECK(plain != NULL && plain == decorated, "RIB_Main missing or a different function");
    if (decorated) {
        CHECK(decorated(0x1234, 1) == 1, "RIB_Main(up) did not return 1");
        CHECK(decorated(0x1234, 0) == 1, "RIB_Main(down) did not return 1");
        CHECK(read_file(log_path, log, sizeof log), "log gone");
        CHECK(strstr(log, "RIB_Main provider=1234 up_down=1 t=") != NULL, "up call not logged");
        CHECK(strstr(log, "RIB_Main provider=1234 up_down=0 t=") != NULL, "down call not logged");
    }

    printf("pin\n");
    FreeLibrary(h);
    CHECK(GetModuleHandleW(dll) != NULL, "FreeLibrary unloaded the plugin");

done:
    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: load tests passed\n");
    return 0;
}
