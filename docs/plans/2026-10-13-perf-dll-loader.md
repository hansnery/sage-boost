# Performance DLL, plan 1: the Miles plugin and the load check (R0)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** a `bfme2perf.asi` that Miles loads from the game's `mss\` folder,
pins itself, logs when and where it was loaded, and does nothing else; then
confirm in game how Miles treats it (spec Phase R, R0).

**Architecture:** a freestanding 32-bit DLL with the `.asi` extension in a
new `perf/` folder. `DllMain` pins the module and writes a startup log next
to it; the exported `RIB_Main` logs each call and returns success. A test
program loads it the way Miles does. No game-code hooks yet.

**Tech Stack:** C (freestanding, 32-bit MinGW-w64 from WinLibs at
`C:\tools\winlibs-i686\mingw32\bin`, kernel32 only), GNU make run from Git
Bash, the test program with the static C runtime.

Spec: `docs/specs/2026-10-13-perf-dll-design.md`. Later plans, written once
their inputs exist: plan 2, the Phase R research (R1–R6); plan 3, Milestone
1 (timing); plan 4, Milestone 2 (skeleton pre-pass), only if the decision
gate passes.

## Global Constraints

- Nothing under `C:\repositories\Open-BFME-2\` is written.
- No code from SAGE-Unlocked (GPL-3) or bfme2-accelerator (MIT).
- `game.dat` and other EA assets are never redistributed; the DLL ships alone and changes nothing on disk.
- No hardcoded absolute addresses: every hook is found by signature in the running executable.
- The faction unlocker is a separate project: nothing in `dll/` changes, and its `dinput8.dll` and `dinput8.ini` are never replaced or renamed.
- Conventional commits; branch `feat/perf-dll`.

---

## File structure

| File | Responsibility |
|---|---|
| `perf/Makefile` | Builds `build/bfme2perf.asi` and the test; `make test`, `make dist`. |
| `perf/src/main.c` | `DllMain` (pin, startup log) and the `RIB_Main` export. |
| `perf/src/bfme2perf.def` | Exports `_RIB_Main@8` (the name Miles looks up) and `RIB_Main`. |
| `perf/src/version.rc` | Version resource, 0.1.0. |
| `perf/src/log.c`, `perf/src/log.h`, `perf/src/rt.c` | Copies of `dll/src/log.c`, `log.h`, `rt.c` (CRT-free logger, `memcpy`/`memset`). |
| `perf/tests/test_load.c` | Loads the plugin from a folder of its own, checks the log, the exports, `RIB_Main` and the pin. |
| `.gitignore` | Adds `perf/build/` and `perf/dist/`. |

### Task 1: the plugin and its load test

**Files:**
- Create: `perf/Makefile`, `perf/src/main.c`, `perf/src/bfme2perf.def`, `perf/src/version.rc`, `perf/tests/test_load.c`
- Create (copies): `perf/src/log.c`, `perf/src/log.h`, `perf/src/rt.c`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: from `log.h`: `fu_log_open(const WCHAR *)`, `fu_log_close(void)`, `fu_logf(const char *, ...)`, `fu_log_event_path(const WCHAR *)`, `fu_log_event(const char *, ...)`.
- Produces: `build/bfme2perf.asi` exporting `_RIB_Main@8` and `RIB_Main`, both `int __stdcall RIB_Main(unsigned provider, unsigned up_down)`, returning 1. A log `bfme2perf.log` next to the DLL whose first lines are `bfme2perf 0.1.0`, `self: <path>`, `host: <path>`, `loaded <n> ms after process start, pinned=<0|1>`, then one `module <name>: <path | not loaded>` line each for `dinput8.dll`, `d3d9.dll` and `mss32.dll`; then one `RIB_Main provider=<hex> up_down=<n> t=<n> ms` line per call.

- [x] **Step 1: Create the branch and copy the shared sources**

```bash
cd /c/repositories/sage-boost
git switch -c feat/perf-dll
mkdir -p perf/src perf/tests
cp dll/src/log.c dll/src/log.h dll/src/rt.c perf/src/
```

- [x] **Step 2: Ignore the build output**

Append to `.gitignore`:

```
perf/build/
perf/dist/
```

- [x] **Step 3: Write the failing test** `perf/tests/test_load.c`

```c
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
    WCHAR arg[MAX_PATH], src[MAX_PATH], dir[MAX_PATH], dll[MAX_PATH], log_path[MAX_PATH];
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
    DeleteFileW(log_path);
    CHECK(CopyFileW(src, dll, FALSE), "copy to %ls failed: %lu", dll, GetLastError());

    printf("load\n");
    h = LoadLibraryW(dll);
    CHECK(h != NULL, "LoadLibrary(%ls) failed: %lu", dll, GetLastError());
    if (!h)
        goto done;
    CHECK(read_file(log_path, log, sizeof log), "no log at %ls", log_path);
    printf("%s", log);
    CHECK(strstr(log, "bfme2perf 0.1.0\r\n") == log, "first line is not the version");
    CHECK(strstr(log, "self: ") && strstr(log, "bfme2perf-test\\bfme2perf.asi"), "no self path");
    CHECK(strstr(log, "host: ") && strstr(log, "test_load.exe"), "no host path");
    CHECK(strstr(log, "pinned=1") != NULL, "not pinned");
    CHECK(strstr(log, "module dinput8.dll: ") != NULL, "no dinput8 line");
    CHECK(strstr(log, "module d3d9.dll: ") != NULL, "no d3d9 line");
    CHECK(strstr(log, "module mss32.dll: not loaded") != NULL, "no mss32 line");

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
```

- [x] **Step 4: Write `perf/Makefile`**

```make
# bfme2perf: performance plugin for BFME2 / ROTWK, loaded by Miles from the
# game's mss\ folder.
# Run from a POSIX shell (Git Bash, MSYS2, Linux) with a 32-bit MinGW-w64 GCC:
#   make                  build/bfme2perf.asi
#   make test             plugin load test
#   make dist             dist/ folder: copy its contents into the game's mss\
#   make CROSS=i686-w64-mingw32-   when the compiler is prefixed (Linux cross)

CROSS    ?=
CC       := $(CROSS)gcc
WINDRES  := $(CROSS)windres

BUILD := build
SRCS  := src/main.c src/log.c src/rt.c
OBJS  := $(SRCS:src/%.c=$(BUILD)/%.o) $(BUILD)/version.o

COMMON_FLAGS := -std=gnu11 -Wall -Wextra -Werror -DWIN32_LEAN_AND_MEAN -DUNICODE -D_UNICODE
# No C runtime in the DLL: nothing runs before or around DllMain but our code.
CFLAGS  := -O2 $(COMMON_FLAGS) -ffreestanding -fno-stack-protector \
           -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns
LDFLAGS := -shared -nostdlib -s -Wl,-e,_DllMain@12 -Wl,--enable-stdcall-fixup \
           -Wl,--dynamicbase -Wl,--nxcompat
LIBS    := -lkernel32 -lgcc

TEST_CFLAGS := -O1 $(COMMON_FLAGS) -Isrc -Itests

.PHONY: all test dist clean check-arch

all: check-arch $(BUILD)/bfme2perf.asi

check-arch:
	@case "$$($(CC) -dumpmachine)" in \
	  i686-*|i586-*|i386-*) ;; \
	  *) echo "error: need a 32-bit (i686) MinGW-w64 gcc, '$(CC)' targets $$($(CC) -dumpmachine)"; exit 1;; \
	esac

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: src/%.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/version.o: src/version.rc | $(BUILD)
	$(WINDRES) -O coff -o $@ $<

$(BUILD)/bfme2perf.asi: $(OBJS) src/bfme2perf.def
	$(CC) $(LDFLAGS) -o $@ $(OBJS) src/bfme2perf.def $(LIBS)

$(BUILD)/test_load.exe: tests/test_load.c | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $<

test: all $(BUILD)/test_load.exe
	./$(BUILD)/test_load.exe "$(BUILD)/bfme2perf.asi"

dist: all
	mkdir -p dist
	cp $(BUILD)/bfme2perf.asi dist/

clean:
	rm -rf $(BUILD) dist
```

- [x] **Step 5: Run the test to see it fail**

```bash
export PATH=/c/tools/winlibs-i686/mingw32/bin:$PATH
cd /c/repositories/sage-boost/perf && mingw32-make test
```

Expected: the build stops with `No rule to make target 'build/main.o'` or
`src/main.c: No such file`, because `main.c`, the `.def` and the `.rc` don't
exist yet.

- [x] **Step 6: Write `perf/src/bfme2perf.def`**

```
LIBRARY bfme2perf.asi
EXPORTS
    _RIB_Main@8 = RIB_Main@8
    RIB_Main = RIB_Main@8
```

- [x] **Step 7: Write `perf/src/version.rc`**

```rc
#include <winver.h>

VS_VERSION_INFO VERSIONINFO
FILEVERSION     0,1,0,0
PRODUCTVERSION  0,1,0,0
FILEOS          VOS_NT_WINDOWS32
FILETYPE        VFT_DLL
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904B0"
        BEGIN
            VALUE "FileDescription", "BFME2 performance plugin (Miles .asi)"
            VALUE "FileVersion", "0.1.0"
            VALUE "InternalName", "bfme2perf"
            VALUE "LegalCopyright", "MIT License"
            VALUE "OriginalFilename", "bfme2perf.asi"
            VALUE "ProductName", "sage-boost performance plugin"
            VALUE "ProductVersion", "0.1.0"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
```

- [x] **Step 8: Write `perf/src/main.c`**

```c
/* bfme2perf: a Miles plugin (.asi) that mss32.dll loads from the game's mss\
 * folder at AIL_startup. It pins itself so Miles can't unload it and
 * registers no audio provider. */
#include <windows.h>

#include "log.h"

#define PERF_VERSION "0.1.0"

static WCHAR g_log_path[MAX_PATH];

static unsigned ms_since_process_start(void)
{
    FILETIME create, exit_time, kernel, user, now;
    ULARGE_INTEGER a, b;
    if (!GetProcessTimes(GetCurrentProcess(), &create, &exit_time, &kernel, &user))
        return 0;
    GetSystemTimeAsFileTime(&now);
    a.LowPart = create.dwLowDateTime;
    a.HighPart = create.dwHighDateTime;
    b.LowPart = now.dwLowDateTime;
    b.HighPart = now.dwHighDateTime;
    return (unsigned)((b.QuadPart - a.QuadPart) / 10000);
}

/* out = this module's path with its extension replaced by ext. 0 on failure. */
static int sibling_path(HINSTANCE inst, const WCHAR *ext, WCHAR *out)
{
    DWORD n = GetModuleFileNameW(inst, out, MAX_PATH), dot = n, i;
    if (n == 0 || n >= MAX_PATH)
        return 0;
    for (i = n; i > 0 && out[i - 1] != L'\\'; i--) {
        if (out[i - 1] == L'.') {
            dot = i - 1;
            break;
        }
    }
    for (i = 0; ext[i]; i++) {
        if (dot + i >= MAX_PATH - 1)
            return 0;
        out[dot + i] = ext[i];
    }
    out[dot + i] = 0;
    return 1;
}

static void log_module(const WCHAR *name)
{
    WCHAR path[MAX_PATH];
    HMODULE h = GetModuleHandleW(name);
    if (h && GetModuleFileNameW(h, path, MAX_PATH))
        fu_logf("module %ls: %ls", name, path);
    else
        fu_logf("module %ls: not loaded", name);
}

int __stdcall RIB_Main(unsigned provider, unsigned up_down)
{
    fu_log_event("RIB_Main provider=%X up_down=%u t=%u ms", provider, up_down,
                 ms_since_process_start());
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    WCHAR path[MAX_PATH];
    HMODULE self = NULL;
    int pinned;

    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    DisableThreadLibraryCalls(inst);
    pinned = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_PIN,
                                (LPCWSTR)inst, &self) != 0;

    if (!sibling_path(inst, L".log", g_log_path))
        return TRUE;
    fu_log_open(g_log_path);
    fu_logf("bfme2perf " PERF_VERSION);
    if (GetModuleFileNameW(inst, path, MAX_PATH))
        fu_logf("self: %ls", path);
    if (GetModuleFileNameW(NULL, path, MAX_PATH))
        fu_logf("host: %ls", path);
    fu_logf("loaded %u ms after process start, pinned=%d", ms_since_process_start(), pinned);
    log_module(L"dinput8.dll");
    log_module(L"d3d9.dll");
    log_module(L"mss32.dll");
    /* fu_log_event opens the file per line; the startup handle doesn't share writes. */
    fu_log_close();
    fu_log_event_path(g_log_path);
    return TRUE;
}
```

- [x] **Step 9: Run the test to see it pass**

```bash
export PATH=/c/tools/winlibs-i686/mingw32/bin:$PATH
cd /c/repositories/sage-boost/perf && mingw32-make test
```

Expected: the log is printed, ending with `OK: load tests passed`, and
`make` exits 0. If the build warns or the `_RIB_Main@8` check fails, check
the export names with `objdump -p build/bfme2perf.asi | grep -A4 "Ordinal/Name"`;
both `_RIB_Main@8` and `RIB_Main` must be listed.

- [x] **Step 10: Check the imports**

```bash
objdump -p build/bfme2perf.asi | grep "DLL Name"
```

Expected: only `DLL Name: KERNEL32.dll`.

- [x] **Step 11: Commit**

```bash
cd /c/repositories/sage-boost
git add .gitignore perf/Makefile perf/src perf/tests
git commit -m "feat(perf): Miles plugin that pins itself and logs its load"
```

### Task 2: the in-game load check (R0)

**Files:**
- Install (not in the repo): `C:\Jogos\AgeoftheRing\rotwk\mss\bfme2perf.asi`
- Modify: `docs/specs/2026-10-13-perf-dll-design.md` (an "R0 findings" section after Phase R's table)

**Interfaces:**
- Consumes: `build/bfme2perf.asi` and the log format from Task 1.
- Produces: the R0 answers in the spec, which plan 3 relies on: where Miles
  loads from, whether the plugin stays loaded, `RIB_Main`'s calls, and
  whether `d3d9.dll` is already loaded when the plugin is.

- [x] **Step 1: Install the plugin**

```bash
cd /c/repositories/sage-boost/perf
mingw32-make dist
cp dist/bfme2perf.asi /c/Jogos/AgeoftheRing/rotwk/mss/
```

Nothing else in the game folder changes. Removing the file uninstalls it.

- [x] **Step 2: The user starts Realms in Exile, gets to the main menu, starts a skirmish against one AI, plays a minute, and quits to the desktop**

The game must behave exactly as without the plugin.

- [x] **Step 3: Read the log**

Read `C:\Jogos\AgeoftheRing\rotwk\mss\bfme2perf.log`. Expected:

- `self:` is the `mss\` path and `host:` is `game.dat`;
- `pinned=1`;
- `module dinput8.dll:` is the game folder's copy (the faction unlocker);
- one or more `RIB_Main` lines with `up_down=1`, and lines with `up_down=0`
  if Miles shuts plugins down at exit;
- `module d3d9.dll:` says whether graphics were set up before audio.

If the log is missing, Miles didn't load the plugin: check that the game
passes `mss\` to `AIL_set_redist_directory` (search the strings near the
`AIL_set_redist_directory` import's call site in `game.dat`), and stop here
to rethink the loading with the user.

- [x] **Step 4: Record the findings in the spec**

Add after Phase R's table in `docs/specs/2026-10-13-perf-dll-design.md`:

```markdown
### R0 findings (in game, <date>)

- Loaded from: <self path>, <n> ms after process start; `d3d9.dll` <was / was not> loaded yet.
- `RIB_Main` calls: <list of up_down values and times>.
- Pinned: <yes/no>; the game <did / did not> behave differently.
- Consequence for plan 3: <the hooks can / cannot be installed from DllMain before the first frame>.
```

Fill each `<…>` from the log, not from expectations.

- [x] **Step 5: Commit**

```bash
cd /c/repositories/sage-boost
git add docs/specs/2026-10-13-perf-dll-design.md
git commit -m "docs: record how Miles loads the performance plugin (R0)"
```
