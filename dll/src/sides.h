/* [Sides] aliases: a new Side string is treated as its donor's Side wherever a
 * side-alias hook sits (faction id, AI skirmish side, rank points). */
#ifndef FU_SIDES_H
#define FU_SIDES_H

#include "engine.h"

#define FU_MAX_SIDES 32
#define FU_SIDE_NAME_MAX 31
#define FU_MAX_NATIVE 32

/* Drops every alias. */
void fu_sides_clear(void);

/* Adds new_side -> donor. Returns NULL on success, else why it was rejected. */
const char *fu_sides_add(const char *new_side, const char *donor);

/* Reads [Sides] from the INI (logging each entry); returns the alias count. */
unsigned fu_sides_load(const WCHAR *ini_path);

unsigned fu_sides_count(void);
const char *fu_sides_new_name(unsigned i);
const char *fu_sides_donor_name(unsigned i);

/* `str` is a game AsciiString object (one pointer to its buffer). Returns the
 * donor's AsciiString object if `str` names an aliased Side, else NULL. */
const void *fu_sides_resolve(const void *str);

/* Reads the game's Side table (as the faction-id lookup sees it, so a mod's
 * enlarged table counts) plus its special-cased Side. Returns the number of
 * names, 0 if the table could not be located or read. */
unsigned fu_sides_read_native(const fu_image *img, const fu_game *game);

/* Sets the native list directly (tests). */
void fu_sides_set_native(const char *const *names, unsigned n);
unsigned fu_sides_native_count(void);
const char *fu_sides_native_name(unsigned i);

/* The game AsciiString object for native Side i (permanent), or NULL. */
const void *fu_sides_native_string(unsigned i);

/* 1 if `name` is one of the game's Sides or reserved (Neutral, Civilian,
 * Observer); such names cannot be aliased. */
int fu_sides_is_native(const char *name);

/* 1 for Neutral, Civilian and Observer. */
int fu_sides_is_reserved(const char *name);

/* The characters of a game AsciiString object; "" for NULL or empty. */
const char *fu_ascii_cstr(const void *str);

#endif
