/* <dll name>.ini next to the DLL. A missing file means "all defaults". */
#ifndef FU_CONFIG_H
#define FU_CONFIG_H

#include "engine.h"

typedef struct {
    int ini_found;
    int master;    /* [FactionUnlocker] Enabled, default 1 */
    int dry_run;   /* [FactionUnlocker] DryRun,  default 0 */
    int log;       /* [FactionUnlocker] Log,     default 1 */
    int enabled[FU_MAX_PATCHES]; /* [Patches] <id>, default per fu_patch_defs */
} fu_config;

void fu_config_load(fu_config *cfg, const WCHAR *ini_path);

/* Re-reads [Patches] for the selected game's table (fu_select_game). */
void fu_config_load_patches(fu_config *cfg, const WCHAR *ini_path);

#endif
