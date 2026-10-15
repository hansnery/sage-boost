#include "config.h"

int pf_parse_int(const WCHAR *s, int def)
{
    int neg = 0, any = 0;
    unsigned v = 0;

    while (*s == L' ' || *s == L'\t')
        s++;
    if (*s == L'-') {
        neg = 1;
        s++;
    }
    while (*s >= L'0' && *s <= L'9') {
        if (v < 100000000u)
            v = v * 10 + (unsigned)(*s - L'0');
        s++;
        any = 1;
    }
    while (*s == L' ' || *s == L'\t')
        s++;
    if (!any || *s)
        return def;
    return neg ? -(int)v : (int)v;
}

static int read_int(const WCHAR *section, const WCHAR *key, int def, const WCHAR *path)
{
    WCHAR buf[32];
    GetPrivateProfileStringW(section, key, L"", buf, 32, path);
    return pf_parse_int(buf, def);
}

/* Printable ASCII only (anything else becomes '?'), spaces at both ends
 * dropped; def when missing or empty. */
static void read_text(const WCHAR *section, const WCHAR *key, const char *def, char *out,
                      unsigned cap, const WCHAR *path)
{
    WCHAR buf[64];
    unsigned i = 0, n = 0, end = 0;

    GetPrivateProfileStringW(section, key, L"", buf, 64, path);
    while (buf[i] == L' ' || buf[i] == L'\t')
        i++;
    for (; buf[i] && n + 1 < cap; i++) {
        out[n++] = buf[i] >= 32 && buf[i] <= 126 ? (char)buf[i] : '?';
        if (buf[i] != L' ' && buf[i] != L'\t')
            end = n;
    }
    n = end;
    if (!n) {
        while (def[n] && n + 1 < cap) {
            out[n] = def[n];
            n++;
        }
    }
    out[n] = 0;
}

void pf_config_load(pf_config *cfg, const WCHAR *ini_path)
{
    cfg->ini_found = GetFileAttributesW(ini_path) != INVALID_FILE_ATTRIBUTES;
    cfg->enabled = read_int(L"Perf", L"Enabled", 1, ini_path) != 0;
    cfg->log = read_int(L"Perf", L"Log", 1, ini_path) != 0;
    cfg->timing = read_int(L"Timing", L"Enabled", 1, ini_path) != 0;
    cfg->threads = read_int(L"Skeleton", L"Threads", 0, ini_path);
    if (cfg->threads < -1)
        cfg->threads = 0;
    if (cfg->threads > 4)
        cfg->threads = 4;
    cfg->verify = read_int(L"Skeleton", L"Verify", 0, ini_path) != 0;
    cfg->detail = read_int(L"Timing", L"Detail", 1, ini_path) != 0;
    cfg->reflect_requested = read_int(L"Render", L"ReflectionInterval", 1, ini_path);
    cfg->reflect_interval = cfg->reflect_requested < 1   ? 1
                            : cfg->reflect_requested > 4 ? 4
                                                         : cfg->reflect_requested;
    cfg->shadow_requested = read_int(L"Render", L"ShadowInterval", 1, ini_path);
    cfg->shadow_interval = cfg->shadow_requested < 1   ? 1
                           : cfg->shadow_requested > 4 ? 4
                                                       : cfg->shadow_requested;
    cfg->shadow_scrolling = read_int(L"Render", L"ShadowFullWhileScrolling", 0, ini_path) != 0;
    cfg->batching = read_int(L"Render", L"Batching", 0, ini_path) != 0;
    cfg->instancing = read_int(L"Render", L"Instancing", 0, ini_path) != 0;
    cfg->instancing_verify = read_int(L"Render", L"InstancingVerify", 1, ini_path) != 0;
    cfg->shadow_instancing = read_int(L"Render", L"ShadowInstancing", 1, ini_path) != 0;
    cfg->max_fps_requested = read_int(L"Render", L"MaxFPS", 30, ini_path);
    cfg->max_fps = cfg->max_fps_requested < 60    ? 30
                   : cfg->max_fps_requested >= 120 ? 120
                                                   : cfg->max_fps_requested / 30 * 30;
    cfg->frame_smoothing = read_int(L"Render", L"FrameSmoothing", PF_DEFAULT_SMOOTHING, ini_path);
    if (cfg->frame_smoothing < 0)
        cfg->frame_smoothing = 0;
    if (cfg->frame_smoothing > 3)
        cfg->frame_smoothing = 3;
    cfg->extra_shadows = read_int(L"Render", L"ExtraFrameShadows", 0, ini_path) != 0;
    cfg->extra_reflections = read_int(L"Render", L"ExtraFrameReflections", 0, ini_path) != 0;
    cfg->shadow_batch = read_int(L"Render", L"ShadowBatchIgnoreLight", 1, ini_path) != 0;
    cfg->stencil_flushes = read_int(L"Render", L"NoStencilFlushes", 1, ini_path);
    if (cfg->stencil_flushes < 0 || cfg->stencil_flushes > 2)
        cfg->stencil_flushes = 1;
    cfg->shadow_particles = read_int(L"Render", L"ShadowSkipParticles", 1, ini_path);
    if (cfg->shadow_particles < 0 || cfg->shadow_particles > 2)
        cfg->shadow_particles = 1;
    cfg->shadow_verify = read_int(L"Render", L"ShadowPassVerify", 0, ini_path) != 0;
    cfg->light_skip = read_int(L"Render", L"LightSkipShadow", 0, ini_path) != 0;
    cfg->light_cull = read_int(L"Render", L"LightCull", 1, ini_path) != 0;
    cfg->light_reuse = read_int(L"Render", L"LightReuse", 1, ini_path) != 0;
    cfg->light_verify = read_int(L"Render", L"LightVerify", 1, ini_path);
    if (cfg->light_verify < 0 || cfg->light_verify > 2)
        cfg->light_verify = 1;
    cfg->catchup = read_int(L"CatchUp", L"Enabled", 1, ini_path) != 0;
    cfg->catchup_max_skips = read_int(L"CatchUp", L"MaxSkips", 1, ini_path);
    if (cfg->catchup_max_skips < 1)
        cfg->catchup_max_skips = 1;
    if (cfg->catchup_max_skips > 3)
        cfg->catchup_max_skips = 3;
    cfg->catchup_notice = read_int(L"CatchUp", L"Notice", 1, ini_path) != 0;
    read_text(L"CatchUp", L"NoticeText", "Catching up", cfg->catchup_text,
              sizeof cfg->catchup_text, ini_path);
    cfg->catchup_notice_min =
        read_int(L"CatchUp", L"NoticeMinSkips", PF_DEFAULT_NOTICE_SKIPS, ini_path);
    if (cfg->catchup_notice_min < 1)
        cfg->catchup_notice_min = 1;
    if (cfg->catchup_notice_min > 30)
        cfg->catchup_notice_min = 30;
}

static WCHAR lower(WCHAR c)
{
    return c >= L'A' && c <= L'Z' ? (WCHAR)(c - L'A' + L'a') : c;
}

static int contains(const WCHAR *hay, const WCHAR *needle)
{
    unsigned i, k;
    if (!hay)
        return 0;
    for (i = 0; hay[i]; i++) {
        for (k = 0; needle[k] && lower(hay[i + k]) == needle[k]; k++)
            ;
        if (!needle[k])
            return 1;
    }
    return 0;
}

const char *pf_detect_game(const WCHAR *command_line, const WCHAR *exe_path)
{
    if (contains(command_line, L"realms"))
        return "Realms in Exile";
    if (contains(command_line, L"aotr") || contains(exe_path, L"ageofthering") ||
        contains(exe_path, L"age of the ring"))
        return "Age of the Ring";
    return "unknown";
}
