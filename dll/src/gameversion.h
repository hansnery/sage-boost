/* Identifies the host executable from its VS_VERSIONINFO resource. */
#ifndef FU_GAMEVERSION_H
#define FU_GAMEVERSION_H

#include "engine.h"

typedef struct {
    int has_fixed;            /* VS_FIXEDFILEINFO present */
    unsigned short v[4];      /* FileVersion a.b.c.d */
    WCHAR internal_name[32];  /* StringFileInfo InternalName, "" if absent */
} fu_game_version;

void fu_game_version_read(HMODULE module, fu_game_version *out);

/* The supported game with InternalName "BFME2" (both games use it) and
 * FileVersion major.minor of one of fu_games, or NULL. */
const fu_game *fu_game_identify(const fu_game_version *v);

#endif
