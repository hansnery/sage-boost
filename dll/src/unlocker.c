#include "unlocker.h"

#include "config.h"
#include "engine.h"
#include "gameversion.h"
#include "hooks.h"
#include "log.h"
#include "sides.h"
#include "skirmish.h"

static WCHAR g_log_path[MAX_PATH];
static int g_hooked[FU_MAX_PATCHES];

static int is_hook(const fu_patch_def *def)
{
    return def->kind == FU_KIND_ENTRY_HOOK || def->kind == FU_KIND_CALL_HOOK;
}

static int find_patch(const char *id)
{
    unsigned i;
    for (i = 0; i < fu_patch_count; i++) {
        const char *a = fu_patch_defs[i].id;
        unsigned k;
        for (k = 0; a[k] && a[k] == id[k]; k++)
            ;
        if (!a[k] && !id[k])
            return (int)i;
    }
    return -1;
}

/* Copies `src` to `dst` with its extension replaced by `ext` (".ini"). */
static int sibling_path(WCHAR *dst, unsigned cap, const WCHAR *src, const WCHAR *ext)
{
    unsigned n = 0, dot = 0, i;
    while (src[n]) {
        if (src[n] == L'.')
            dot = n;
        else if (src[n] == L'\\' || src[n] == L'/')
            dot = 0;
        n++;
    }
    if (!dot)
        dot = n;
    for (i = 0; ext[i]; i++)
        ;
    if (dot + i + 1 > cap)
        return 0;
    for (n = 0; n < dot; n++)
        dst[n] = src[n];
    for (i = 0; ext[i]; i++)
        dst[n++] = ext[i];
    dst[n] = 0;
    return 1;
}

static void hex_bytes(char *out, const unsigned char *b, unsigned len)
{
    static const char digits[] = "0123456789ABCDEF";
    unsigned i, o = 0;
    for (i = 0; i < len; i++) {
        if (i)
            out[o++] = ' ';
        out[o++] = digits[b[i] >> 4];
        out[o++] = digits[b[i] & 15];
    }
    out[o] = 0;
}

static void log_result(const fu_patch_def *def, const fu_patch_result *r)
{
    char orig[3 * 16], patched[3 * 16];

    if (r->status == FU_ST_OFF) {
        fu_logf("[%s] %s: off", def->id, def->name);
        return;
    }
    fu_logf("[%s] %s: %s", def->id, def->name, fu_status_name(r->status));
    fu_logf("    %s", def->summary);
    fu_logf("    signature matches: %u", r->matches);
    if (r->matches != 1)
        return;
    hex_bytes(orig, r->original, r->len);
    fu_logf("    site RVA 0x%08X (baseline 0x%08X: %s)", r->site_rva, def->baseline_rva,
            r->site_rva == def->baseline_rva ? "same" : "moved");
    if (r->status == FU_ST_LOCATED || r->status == FU_ST_APPLIED) {
        hex_bytes(patched, r->patched, r->len);
        fu_logf("    bytes %s -> %s", orig, patched);
        if (def->kind == FU_KIND_ENTRY_HOOK && r->status == FU_ST_APPLIED)
            fu_logf("    stub 0x%08X, stolen bytes run from trampoline 0x%08X",
                    (unsigned)(UINT_PTR)r->hook_stub, (unsigned)(UINT_PTR)r->hook_next);
        else if (def->kind == FU_KIND_CALL_HOOK && r->status == FU_ST_APPLIED)
            fu_logf("    stub 0x%08X, continues at the original callee 0x%08X",
                    (unsigned)(UINT_PTR)r->hook_stub, (unsigned)(UINT_PTR)r->hook_next);
    } else {
        fu_logf("    bytes found: %s, expected: %s", orig, def->expect);
    }
}

static void log_native_sides(void)
{
    char line[FU_MAX_NATIVE * (FU_SIDE_NAME_MAX + 2) + 1];
    unsigned i, o = 0, k;
    for (i = 0; i < fu_sides_native_count(); i++) {
        const char *s = fu_sides_native_name(i);
        if (i) {
            line[o++] = ',';
            line[o++] = ' ';
        }
        for (k = 0; s[k]; k++)
            line[o++] = s[k];
    }
    line[o] = 0;
    fu_logf("sides: the game's own Sides (%u): %s", fu_sides_native_count(), line);
}

/* Reads the game's Side table, then [Sides]. Without usable entries the
 * side-alias hooks have nothing to do, so they are not installed at all. */
static void configure_sides(fu_config *cfg, const WCHAR *ini_path, const fu_image *img,
                            const fu_game *game)
{
    unsigned i, n, native = fu_sides_read_native(img, game);
    int d = find_patch("D"), e = find_patch("E");

    if (native)
        log_native_sides();
    n = fu_sides_load(ini_path);
    if (n && !native) {
        /* Without the table an alias could shadow a Side the game (or a mod's
         * patched executable) already has. */
        fu_logf("sides: [Sides] ignored: the game's Side table could not be read "
                "(the %s signature is not found exactly once)",
                fu_patch_defs[game->side_patch].id);
        fu_sides_clear();
        n = 0;
    }
    if (n == 0) {
        for (i = 0; i < fu_patch_count; i++) {
            if (fu_patch_defs[i].needs_sides && cfg->enabled[i]) {
                cfg->enabled[i] = 0;
                fu_logf("[%s] %s: not installed, [Sides] has no entries", fu_patch_defs[i].id,
                        fu_patch_defs[i].name);
            }
        }
        return;
    }
    fu_logf("sides: %u alias(es)", n);
    if ((d >= 0 && !cfg->enabled[d]) || (e >= 0 && !cfg->enabled[e]))
        fu_logf("sides: WARNING D or E is off; maps with AllowedFactions can hide new-Side factions");
}

/* S2 and S4 are installed together or not at all; the resolver asks the map
 * through S2's trampoline. */
static void configure_skirmish(void)
{
    int s2 = find_patch("S2");

    fu_skirmish_set_query(NULL);
    fu_skirmish_reset();
    if (s2 < 0 || !g_hooked[s2])
        return;
    fu_skirmish_set_s2((unsigned)s2);
    fu_skirmish_set_query(fu_skirmish_engine_query);
    fu_logf("skirmish: an AI whose Side has no skirmish player on the map uses its [Sides] "
            "donor's, else the first game Side the map has");
}

void fu_startup(HMODULE self, HMODULE game)
{
    WCHAR dll_path[MAX_PATH], ini_path[MAX_PATH], log_path[MAX_PATH], exe_path[MAX_PATH];
    fu_config cfg;
    fu_game_version ver;
    const fu_game *known;
    fu_image img;
    fu_patch_result results[FU_MAX_PATCHES];
    SYSTEMTIME now;
    DWORD n;
    unsigned i, applied = 0, refused = 0, off = 0;

    n = GetModuleFileNameW(self, dll_path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH || !sibling_path(ini_path, MAX_PATH, dll_path, L".ini") ||
        !sibling_path(log_path, MAX_PATH, dll_path, L".log"))
        return;
    n = GetModuleFileNameW(NULL, exe_path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        exe_path[0] = 0;

    fu_config_load(&cfg, ini_path);
    if (cfg.log)
        fu_log_open(log_path);

    GetLocalTime(&now);
    fu_logf("BFME2 faction unlocker %s  %04u-%02u-%02u %02u:%02u:%02u", FU_VERSION_STRING,
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    fu_logf("dll : %ls", dll_path);
    fu_logf("exe : %ls", exe_path);
    fu_logf("ini : %ls (%s)", ini_path, cfg.ini_found ? "found" : "missing, using defaults");

    if (!cfg.master) {
        fu_logf("master switch off ([FactionUnlocker] Enabled=0): nothing patched");
        fu_log_close();
        return;
    }

    fu_game_version_read(game, &ver);
    fu_logf("game: FileVersion %u.%u.%u.%u, InternalName \"%ls\"", ver.v[0], ver.v[1], ver.v[2],
            ver.v[3], ver.internal_name);
    if (!(known = fu_game_identify(&ver))) {
        fu_logf("game: not BFME2 1.06 or ROTWK 2.01 -> refused, nothing patched");
        fu_log_close();
        return;
    }
    if (!fu_image_open(&img, (void *)game)) {
        fu_logf("game: no usable .text section -> refused, nothing patched");
        fu_log_close();
        return;
    }
    fu_select_game(known);
    fu_config_load_patches(&cfg, ini_path);
    fu_logf("game: %s, TimeDateStamp 0x%08X (%s), base 0x%08X, .text RVA 0x%08X size 0x%X",
            known->title, img.timestamp,
            img.timestamp == known->baseline_timestamp ? "the verified baseline build"
                                                       : "a different build, relying on signatures",
            (unsigned)(UINT_PTR)img.base, img.text_rva, img.text_size);

    fu_logf("config: DryRun=%d", cfg.dry_run);
    configure_sides(&cfg, ini_path, &img, known);
    fu_engine_run(&img, cfg.enabled, cfg.dry_run, results);
    for (i = 0; i < fu_patch_count; i++) {
        g_hooked[i] = is_hook(&fu_patch_defs[i]) && results[i].status == FU_ST_APPLIED;
        if (!(fu_patch_defs[i].needs_sides && fu_sides_count() == 0))
            log_result(&fu_patch_defs[i], &results[i]);
        if (results[i].status == FU_ST_OFF)
            off++;
        else if (results[i].status == FU_ST_APPLIED || results[i].status == FU_ST_LOCATED)
            applied++;
        else
            refused++;
    }
    fu_logf("summary: %u %s, %u refused, %u off", applied, cfg.dry_run ? "located" : "applied",
            refused, off);
    configure_skirmish();
    fu_log_close();
    if (cfg.log) {
        for (i = 0; log_path[i]; i++)
            g_log_path[i] = log_path[i];
        g_log_path[i] = 0;
        fu_log_event_path(log_path);
    }
}

void fu_shutdown(void)
{
    unsigned i, any = 0;

    for (i = 0; i < fu_patch_count; i++)
        any |= g_hooked[i];
    if (!any || !g_log_path[0])
        return;
    fu_log_append(g_log_path);
    fu_logf("exit: hook activity this session");
    for (i = 0; i < fu_patch_count; i++) {
        unsigned calls, hits;
        if (!g_hooked[i])
            continue;
        fu_hook_counts(i, &calls, &hits);
        fu_logf("    [%s] %s: %u calls, %u with the Side replaced", fu_patch_defs[i].id,
                fu_patch_defs[i].name, calls, hits);
    }
    fu_log_close();
}
