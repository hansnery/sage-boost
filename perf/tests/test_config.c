/* bfme2perf.ini parsing and game detection.
 * Usage: test_config.exe (writes INIs in %TEMP%\bfme2perf-config-test) */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#include "config.h"

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

static WCHAR g_ini[MAX_PATH];

static void write_ini(const char *text)
{
    HANDLE h = CreateFileW(g_ini, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD put;
    WriteFile(h, text, (DWORD)strlen(text), &put, NULL);
    CloseHandle(h);
}

int main(void)
{
    WCHAR dir[MAX_PATH];
    pf_config c;

    GetTempPathW(MAX_PATH, dir);
    wcscat(dir, L"bfme2perf-config-test");
    CreateDirectoryW(dir, NULL);
    swprintf(g_ini, MAX_PATH, L"%ls\\bfme2perf.ini", dir);

    printf("pf_parse_int\n");
    CHECK(pf_parse_int(L"0", 7) == 0, "0");
    CHECK(pf_parse_int(L" 42 ", 7) == 42, "spaces");
    CHECK(pf_parse_int(L"-1", 7) == -1, "-1");
    CHECK(pf_parse_int(L"", 7) == 7, "empty");
    CHECK(pf_parse_int(L"abc", 7) == 7, "abc");
    CHECK(pf_parse_int(L"3x", 7) == 7, "3x");
    CHECK(pf_parse_int(L"-", 7) == 7, "-");

    printf("missing file\n");
    DeleteFileW(g_ini);
    pf_config_load(&c, g_ini);
    CHECK(!c.ini_found && c.enabled == 1 && c.log == 1 && c.timing == 1 && c.threads == 0 &&
              c.verify == 0,
          "defaults: found=%d enabled=%d log=%d timing=%d threads=%d verify=%d", c.ini_found,
          c.enabled, c.log, c.timing, c.threads, c.verify);

    printf("every key set\n");
    write_ini("[Perf]\r\nEnabled=0\r\nLog=0\r\n[Timing]\r\nEnabled=0\r\n"
              "[Skeleton]\r\nThreads=-1\r\nVerify=1\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.ini_found && c.enabled == 0 && c.log == 0 && c.timing == 0 && c.threads == -1 &&
              c.verify == 1,
          "set: found=%d enabled=%d log=%d timing=%d threads=%d verify=%d", c.ini_found,
          c.enabled, c.log, c.timing, c.threads, c.verify);

    printf("out of range and junk\n");
    write_ini("[Skeleton]\r\nThreads=9\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.threads == 4, "Threads=9 -> %d", c.threads);
    write_ini("[Skeleton]\r\nThreads=-5\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.threads == 0, "Threads=-5 -> %d", c.threads);
    write_ini("[Skeleton]\r\nThreads=two\r\n[Perf]\r\nEnabled=yes\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.threads == 0 && c.enabled == 1, "junk -> threads=%d enabled=%d", c.threads, c.enabled);

    printf("detail and reflection interval\n");
    DeleteFileW(g_ini);
    pf_config_load(&c, g_ini);
    CHECK(c.detail == 1 && c.reflect_interval == 1 && c.reflect_requested == 1,
          "defaults: detail=%d interval=%d requested=%d", c.detail, c.reflect_interval,
          c.reflect_requested);
    write_ini("[Render]\r\nReflectionInterval=2\r\n[Timing]\r\nDetail=0\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.reflect_interval == 2 && c.detail == 0, "2/0 -> interval=%d detail=%d",
          c.reflect_interval, c.detail);
    write_ini("[Render]\r\nReflectionInterval=9\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.reflect_interval == 4 && c.reflect_requested == 9, "9 -> %d (requested %d)",
          c.reflect_interval, c.reflect_requested);
    write_ini("[Render]\r\nReflectionInterval=0\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.reflect_interval == 1 && c.reflect_requested == 0, "0 -> %d (requested %d)",
          c.reflect_interval, c.reflect_requested);
    write_ini("[Render]\r\nReflectionInterval=-3\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.reflect_interval == 1, "-3 -> %d", c.reflect_interval);
    write_ini("[Render]\r\nReflectionInterval=two\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.reflect_interval == 1 && c.reflect_requested == 1, "two -> %d (requested %d)",
          c.reflect_interval, c.reflect_requested);

    printf("shadow interval\n");
    DeleteFileW(g_ini);
    pf_config_load(&c, g_ini);
    CHECK(c.shadow_interval == 1 && c.shadow_requested == 1, "default %d (requested %d)",
          c.shadow_interval, c.shadow_requested);
    write_ini("[Render]\r\nShadowInterval=2\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.shadow_interval == 2, "2 -> %d", c.shadow_interval);
    write_ini("[Render]\r\nShadowInterval=7\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.shadow_interval == 4 && c.shadow_requested == 7, "7 -> %d (requested %d)",
          c.shadow_interval, c.shadow_requested);
    write_ini("[Render]\r\nShadowInterval=0\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.shadow_interval == 1, "0 -> %d", c.shadow_interval);

    printf("shadow while scrolling\n");
    DeleteFileW(g_ini);
    pf_config_load(&c, g_ini);
    CHECK(c.shadow_scrolling == 0, "default %d", c.shadow_scrolling);
    write_ini("[Render]\r\nShadowFullWhileScrolling=1\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.shadow_scrolling == 1, "1 -> %d", c.shadow_scrolling);

    printf("batching\n");
    DeleteFileW(g_ini);
    pf_config_load(&c, g_ini);
    CHECK(c.batching == 0, "default %d", c.batching);
    write_ini("[Render]\r\nBatching=1\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.batching == 1, "1 -> %d", c.batching);

    printf("instancing\n");
    DeleteFileW(g_ini);
    pf_config_load(&c, g_ini);
    CHECK(c.instancing == 0 && c.instancing_verify == 1, "defaults %d %d", c.instancing,
          c.instancing_verify);
    write_ini("[Render]\r\nInstancing=1\r\nInstancingVerify=0\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.instancing == 1 && c.instancing_verify == 0, "1/0 -> %d %d", c.instancing,
          c.instancing_verify);
    printf("shadow instancing\n");
    DeleteFileW(g_ini);
    pf_config_load(&c, g_ini);
    CHECK(c.shadow_instancing == 1, "default %d", c.shadow_instancing);
    write_ini("[Render]\r\nInstancing=1\r\nShadowInstancing=0\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.instancing == 1 && c.shadow_instancing == 0, "0 -> %d (instancing %d)",
          c.shadow_instancing, c.instancing);
    write_ini("[Render]\r\nShadowInstancing=off\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.shadow_instancing == 1, "junk -> %d", c.shadow_instancing);
    printf("shadow-pass fixes\n");
    DeleteFileW(g_ini);
    pf_config_load(&c, g_ini);
    CHECK(c.shadow_batch == 1 && c.stencil_flushes == 1 && c.shadow_particles == 1 &&
              c.shadow_verify == 0,
          "defaults %d %d %d %d", c.shadow_batch, c.stencil_flushes, c.shadow_particles,
          c.shadow_verify);
    write_ini("[Render]\r\nShadowBatchIgnoreLight=0\r\nNoStencilFlushes=2\r\n"
              "ShadowSkipParticles=0\r\nShadowPassVerify=1\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.shadow_batch == 0 && c.stencil_flushes == 2 && c.shadow_particles == 0 &&
              c.shadow_verify == 1,
          "set %d %d %d %d", c.shadow_batch, c.stencil_flushes, c.shadow_particles,
          c.shadow_verify);
    write_ini("[Render]\r\nNoStencilFlushes=3\r\nShadowSkipParticles=-1\r\n"
              "ShadowBatchIgnoreLight=yes\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.stencil_flushes == 1 && c.shadow_particles == 1 && c.shadow_batch == 1,
          "out of range -> %d %d %d", c.stencil_flushes, c.shadow_particles, c.shadow_batch);
    write_ini("[Render]\r\nShadowSkipParticles=2\r\nNoStencilFlushes=0\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.shadow_particles == 2 && c.stencil_flushes == 0, "2/0 -> %d %d", c.shadow_particles,
          c.stencil_flushes);
    printf("light cuts\n");
    DeleteFileW(g_ini);
    pf_config_load(&c, g_ini);
    CHECK(c.light_skip == 0 && c.light_cull == 1 && c.light_reuse == 1 && c.light_verify == 1,
          "defaults %d %d %d %d", c.light_skip, c.light_cull, c.light_reuse, c.light_verify);
    write_ini("[Render]\r\nLightSkipShadow=1\r\nLightCull=0\r\nLightReuse=0\r\nLightVerify=2\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.light_skip == 1 && c.light_cull == 0 && c.light_reuse == 0 && c.light_verify == 2,
          "set %d %d %d %d", c.light_skip, c.light_cull, c.light_reuse, c.light_verify);
    write_ini("[Render]\r\nLightVerify=3\r\nLightCull=x\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.light_verify == 1 && c.light_cull == 1, "out of range -> %d, junk -> %d",
          c.light_verify, c.light_cull);
    printf("max fps\n");
    DeleteFileW(g_ini);
    pf_config_load(&c, g_ini);
    CHECK(c.max_fps == 30, "default %d", c.max_fps);
    write_ini("[Render]\r\nMaxFPS=60\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.max_fps == 60, "60 -> %d", c.max_fps);
    write_ini("[Render]\r\nMaxFPS=75\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.max_fps == 60, "75 -> %d", c.max_fps);
    write_ini("[Render]\r\nMaxFPS=144\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.max_fps == 120 && c.max_fps_requested == 144, "144 -> %d", c.max_fps);
    write_ini("[Render]\r\nMaxFPS=45\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.max_fps == 30, "45 -> %d", c.max_fps);
    CHECK(c.frame_smoothing == PF_DEFAULT_SMOOTHING, "smoothing default %d", c.frame_smoothing);
    write_ini("[Render]\r\nFrameSmoothing=9\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.frame_smoothing == 3, "smoothing 9 -> %d", c.frame_smoothing);
    write_ini("[Render]\r\nFrameSmoothing=3\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.frame_smoothing == 3, "smoothing 3 -> %d", c.frame_smoothing);

    printf("passes redrawn in extra frames\n");
    DeleteFileW(g_ini);
    pf_config_load(&c, g_ini);
    CHECK(c.extra_shadows == 0 && c.extra_reflections == 0, "defaults %d %d", c.extra_shadows,
          c.extra_reflections);
    write_ini("[Render]\r\nExtraFrameShadows=1\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.extra_shadows == 1 && c.extra_reflections == 0, "shadows on: %d %d",
          c.extra_shadows, c.extra_reflections);
    write_ini("[Render]\r\nExtraFrameShadows=0\r\nExtraFrameReflections=1\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.extra_shadows == 0 && c.extra_reflections == 1, "reflections on: %d %d",
          c.extra_shadows, c.extra_reflections);
    write_ini("[Render]\r\nExtraFrameShadows=yes\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.extra_shadows == 0, "junk -> %d", c.extra_shadows);

    printf("catch-up\n");
    DeleteFileW(g_ini);
    pf_config_load(&c, g_ini);
    CHECK(c.catchup == 1 && c.catchup_max_skips == 1 && c.catchup_notice == 1 &&
              !strcmp(c.catchup_text, "Catching up") && c.catchup_notice_min == 6,
          "defaults %d %d %d '%s' %d", c.catchup, c.catchup_max_skips, c.catchup_notice,
          c.catchup_text, c.catchup_notice_min);
    write_ini("[CatchUp]\r\nNoticeMinSkips=10\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.catchup_notice_min == 10, "NoticeMinSkips=10 -> %d", c.catchup_notice_min);
    write_ini("[CatchUp]\r\nNoticeMinSkips=0\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.catchup_notice_min == 1, "NoticeMinSkips=0 -> %d", c.catchup_notice_min);
    write_ini("[CatchUp]\r\nNoticeMinSkips=99\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.catchup_notice_min == 30, "NoticeMinSkips=99 -> %d", c.catchup_notice_min);
    write_ini("[CatchUp]\r\nEnabled=0\r\nMaxSkips=2\r\nNotice=0\r\nNoticeText=  Recuperando "
              "atraso  \r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.catchup == 0 && c.catchup_max_skips == 2 && c.catchup_notice == 0 &&
              !strcmp(c.catchup_text, "Recuperando atraso"),
          "set %d %d %d '%s'", c.catchup, c.catchup_max_skips, c.catchup_notice, c.catchup_text);
    write_ini("[CatchUp]\r\nMaxSkips=9\r\nNoticeText=Pulando quadros \xC3\xA7\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.catchup_max_skips == 3 && !strncmp(c.catchup_text, "Pulando quadros ", 16) &&
              strlen(c.catchup_text) > 16 && !strchr(c.catchup_text + 16, 0xC3),
          "9 -> %d, non-ASCII -> '%s'", c.catchup_max_skips, c.catchup_text);
    write_ini("[CatchUp]\r\nMaxSkips=0\r\nNoticeText=\r\n");
    pf_config_load(&c, g_ini);
    CHECK(c.catchup_max_skips == 1 && !strcmp(c.catchup_text, "Catching up"),
          "0 -> %d, empty -> '%s'", c.catchup_max_skips, c.catchup_text);
    write_ini("[CatchUp]\r\nNoticeText=0123456789012345678901234567890123456789012345678901234\r\n");
    pf_config_load(&c, g_ini);
    CHECK(strlen(c.catchup_text) == sizeof c.catchup_text - 1, "long text -> %u chars",
          (unsigned)strlen(c.catchup_text));

    printf("game detection\n");
    CHECK(!strcmp(pf_detect_game(L"\"C:\\Jogos\\AgeoftheRing\\rotwk\\game.dat\" -mod "
                                 L"\"C:\\Program Files (x86)\\Realms in Exile Nightly\\x\"",
                                 L"C:\\Jogos\\AgeoftheRing\\rotwk\\game.dat"),
                  "Realms in Exile"),
          "Realms");
    CHECK(!strcmp(pf_detect_game(L"game.dat", L"C:\\Jogos\\AgeoftheRing\\rotwk\\game.dat"),
                  "Age of the Ring"),
          "AotR");
    CHECK(!strcmp(pf_detect_game(L"x.exe", L"C:\\tmp\\x.exe"), "unknown"), "unknown");

    DeleteFileW(g_ini);
    RemoveDirectoryW(dir);
    if (g_failures) {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("OK: config tests passed\n");
    return 0;
}
