/* Patch engine: section scan, signature match, verify-then-patch. */
#ifndef FU_ENGINE_H
#define FU_ENGINE_H

#include <windows.h>

#define FU_MAX_PATCHES 16
#define FU_MAX_PATTERN 96
#define FU_MAX_STEAL 16

typedef enum {
    FU_KIND_JCC8_TO_JMP,  /* 7x rel8     -> EB rel8 */
    FU_KIND_JCC8_NOP,     /* 7x rel8     -> 90 90 */
    FU_KIND_JCC32_NOP,    /* 0F 8x rel32 -> 90 x6 */
    FU_KIND_JCC32_TO_JMP, /* 0F 8x rel32 -> E9 rel32+1 90 (same target) */
    FU_KIND_IMM8_CLEAR,   /* imm8        -> 00 */
    FU_KIND_ENTRY_HOOK,   /* prologue    -> E9 stub + CC pad; stolen bytes run from a trampoline */
    FU_KIND_CALL_HOOK     /* E8 rel32    -> E8 stub; the stub continues at the old callee */
} fu_kind;

typedef enum {
    FU_HOOK_NONE,
    FU_HOOK_SIDE_ALIAS,   /* replace a `const AsciiString &` Side argument with its donor */
    FU_HOOK_SKIRMISH_THIS, /* entry hook: this+0x58 is the Side; substitute `this` for the resolved one */
    FU_HOOK_SKIRMISH_ARG  /* replace a `const AsciiString &` Side argument with the resolved Side */
} fu_hook;

typedef struct {
    const char *id;          /* "A", "C2", ...; also the INI key */
    const char *name;
    int default_on;
    fu_kind kind;
    const char *signature;   /* "8B 1E ?? ..." */
    unsigned site_offset;    /* patch site, relative to the signature start */
    const char *expect;      /* pattern the site must match before writing */
    unsigned baseline_rva;   /* diagnostics only, never used to locate */
    const char *summary;
    fu_hook hook;            /* hooks only: what the stub does */
    unsigned arg_slot;       /* hooks only: argument dword above the return address */
    unsigned steal;          /* entry hooks only: prologue bytes moved to the trampoline */
    int needs_sides;         /* installed only when [Sides] has entries */
    unsigned group;          /* nonzero: written only if every member of the group is located */
} fu_patch_def;

/* One supported executable: its version, the build the signatures were
 * verified on, and its patch table. Patch ids are the same in every table. */
typedef struct {
    const char *title;            /* "BFME2 1.06", "ROTWK 2.01" */
    unsigned ver_major, ver_minor;
    unsigned baseline_timestamp;
    const fu_patch_def *defs;
    unsigned count;
    /* Where the game's Side name table is read: the signature of patch
     * defs[side_patch] holds the table address, its size and the
     * special-cased Side (Goblins) at these offsets from its start. */
    unsigned side_patch, side_table_off, side_count_off, side_special_off;
} fu_game;

extern const fu_game fu_games[];
extern const unsigned fu_game_count;

/* The selected game's table (fu_games[0] until fu_select_game). */
extern const fu_patch_def *fu_patch_defs;
extern unsigned fu_patch_count;

void fu_select_game(const fu_game *game);

typedef enum {
    FU_ST_OFF,            /* disabled in the config */
    FU_ST_BAD_DEF,        /* table entry inconsistent with its kind */
    FU_ST_NOT_FOUND,      /* signature matched 0 times */
    FU_ST_AMBIGUOUS,      /* signature matched more than once */
    FU_ST_VERIFY_FAILED,  /* bytes at the site do not match `expect` */
    FU_ST_BAD_TARGET,     /* branch at the site leaves .text */
    FU_ST_PROTECT_FAILED, /* VirtualProtect refused */
    FU_ST_NO_MEMORY,      /* no trampoline memory */
    FU_ST_GROUP_REFUSED,  /* located, but another member of its group is off or refused */
    FU_ST_LOCATED,        /* dry run: everything checked, nothing written */
    FU_ST_APPLIED
} fu_status;

typedef struct {
    fu_status status;
    unsigned matches;
    unsigned site_rva;
    unsigned len;
    unsigned char original[16];
    unsigned char patched[16];
    void *hook_stub;      /* hooks: where the site now jumps */
    void *hook_next;      /* hooks: where the stub continues (trampoline or old callee) */
} fu_patch_result;

typedef struct {
    unsigned char *base;
    unsigned text_rva;
    unsigned text_size;
    unsigned timestamp;
    unsigned preferred_base; /* OptionalHeader.ImageBase: what absolute operands assume */
    unsigned image_size;     /* OptionalHeader.SizeOfImage */
} fu_image;

/* Absolute address operand -> pointer into the loaded image, or NULL if it is
 * outside the image. (The games have no relocations; the tests map them
 * elsewhere.) */
const unsigned char *fu_image_va(const fu_image *img, unsigned va);

/* Matches of patch k's signature in .text; *sig_rva gets the first one's start. */
unsigned fu_engine_find(const fu_image *img, unsigned k, unsigned *sig_rva);

/* Fills `img` from an in-memory PE image; finds the code section holding the
 * entry point (".text" in BFME2). Returns 0 if the headers are not usable. */
int fu_image_open(fu_image *img, void *module_base);

/* Locates and (unless dry_run) applies every enabled patch. All sites are
 * located before any byte is written, so overlapping signatures cannot
 * interfere. A group is written only if all of its members were located.
 * `enabled` is indexed like fu_patch_defs. */
void fu_engine_run(const fu_image *img, const int *enabled, int dry_run,
                   fu_patch_result *results);

const char *fu_status_name(fu_status st);

#endif
