#include "config.h"

static int read_bool(const WCHAR *section, const WCHAR *key, int def, const WCHAR *path)
{
    return GetPrivateProfileIntW(section, key, def, path) != 0;
}

void fu_config_load(fu_config *cfg, const WCHAR *ini_path)
{
    cfg->ini_found = GetFileAttributesW(ini_path) != INVALID_FILE_ATTRIBUTES;
    cfg->master = read_bool(L"FactionUnlocker", L"Enabled", 1, ini_path);
    cfg->dry_run = read_bool(L"FactionUnlocker", L"DryRun", 0, ini_path);
    cfg->log = read_bool(L"FactionUnlocker", L"Log", 1, ini_path);
    fu_config_load_patches(cfg, ini_path);
}

void fu_config_load_patches(fu_config *cfg, const WCHAR *ini_path)
{
    unsigned i;

    for (i = 0; i < fu_patch_count; i++) {
        const char *id = fu_patch_defs[i].id;
        WCHAR key[8];
        unsigned k;
        for (k = 0; k + 1 < 8 && id[k]; k++)
            key[k] = (WCHAR)(unsigned char)id[k];
        key[k] = 0;
        cfg->enabled[i] = read_bool(L"Patches", key, fu_patch_defs[i].default_on, ini_path);
    }
}
