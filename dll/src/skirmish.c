#include "skirmish.h"

#include "hooks.h"
#include "sides.h"

typedef unsigned char(__attribute__((thiscall)) * find_skirmish_side_fn)(void *self, int *index);

static fu_skirmish_query g_query;
static unsigned g_s2 = FU_MAX_PATCHES;

#define MAX_REPORTED 16
static char g_reported[MAX_REPORTED][FU_SIDE_NAME_MAX + 1];
static unsigned g_reported_count;

void fu_skirmish_set_query(fu_skirmish_query q)
{
    g_query = q;
}

void fu_skirmish_set_s2(unsigned k)
{
    g_s2 = k;
}

int fu_skirmish_engine_query(const void *side)
{
    find_skirmish_side_fn find;
    int index = -1;
    if (g_s2 >= FU_MAX_PATCHES || !(find = (find_skirmish_side_fn)fu_hook_next(g_s2)))
        return 0;
    return find((char *)side - FU_PLAYER_SIDE_OFF, &index) != 0;
}

const void *fu_skirmish_resolve(const void *side, fu_skirmish_step *step)
{
    const void *donor;
    unsigned i;

    *step = FU_SKIRMISH_NONE;
    if (!g_query || !side || !*fu_ascii_cstr(side) || fu_sides_is_reserved(fu_ascii_cstr(side)))
        return side;
    if (g_query(side)) {
        *step = FU_SKIRMISH_OWN;
        return side;
    }
    if ((donor = fu_sides_resolve(side)) && g_query(donor)) {
        *step = FU_SKIRMISH_DONOR;
        return donor;
    }
    for (i = 0; i < fu_sides_native_count(); i++) {
        const void *native = fu_sides_native_string(i);
        if (fu_sides_is_reserved(fu_sides_native_name(i)))
            continue;
        if (g_query(native)) {
            *step = FU_SKIRMISH_FALLBACK;
            return native;
        }
    }
    return side;
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

int fu_skirmish_first_unresolved(const void *side)
{
    const char *name = fu_ascii_cstr(side);
    unsigned i, k;
    for (i = 0; i < g_reported_count; i++) {
        for (k = 0; name[k] && lower(name[k]) == lower(g_reported[i][k]); k++)
            ;
        if (!name[k] && !g_reported[i][k])
            return 0;
    }
    if (g_reported_count < MAX_REPORTED) {
        for (k = 0; k < FU_SIDE_NAME_MAX && name[k]; k++)
            g_reported[g_reported_count][k] = name[k];
        g_reported[g_reported_count++][k] = 0;
    }
    return 1;
}

void fu_skirmish_reset(void)
{
    g_reported_count = 0;
}
