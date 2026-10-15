/* ShadowBatchIgnoreLight is proven for the effect files in shadowfx_table.h
 * only. At start-up this reads the directory of every .big archive under the
 * game's folder (the working directory) and the -mod folder, and the loose
 * shaders\compiled folders there, and hashes every shaders\compiled\*.fxo it
 * finds. Kernel32 only, no CRT; safe from DllMain. */
#ifndef PF_FXGUARD_H
#define PF_FXGUARD_H

#include <windows.h>

typedef struct {
    unsigned archives;  /* .big archives read */
    unsigned effects;   /* shaders\compiled\*.fxo entries and files found */
    unsigned proven;    /* of those, in the table with the same content */
    char why[160];      /* empty when every effect is proven */
} pf_fxguard_result;

/* 1 when at least the shadow-capable effects were found and every effect is
 * in the table. game_dir NULL: the working directory. */
int pf_fxguard_check(const WCHAR *game_dir, const WCHAR *command_line, pf_fxguard_result *r);

/* The -mod argument's folder (quotes removed) into out; 0 if none. */
int pf_fxguard_mod_dir(const WCHAR *command_line, WCHAR *out, unsigned cap);

#endif
