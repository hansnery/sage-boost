/* Which Side an AI player is matched to among the map's skirmish players
 * (patches S2 and S4): its own Side if the map has a player for it, else its
 * [Sides] donor's, else the first game Side the map has. */
#ifndef FU_SKIRMISH_H
#define FU_SKIRMISH_H

/* Player::m_side, as the hooked code reads it (`this+0x58`). */
#define FU_PLAYER_SIDE_OFF 0x58

typedef enum {
    FU_SKIRMISH_OWN = 1,   /* the map has a player for the Side itself */
    FU_SKIRMISH_DONOR,     /* ... for its [Sides] donor */
    FU_SKIRMISH_FALLBACK,  /* ... for another game Side */
    FU_SKIRMISH_NONE       /* for none of them: left as is */
} fu_skirmish_step;

/* Does the current map have a skirmish player for this Side (a game
 * AsciiString object)? */
typedef int (*fu_skirmish_query)(const void *side);

void fu_skirmish_set_query(fu_skirmish_query q);

/* The real query: calls the unhooked findSkirmishSide (patch k's trampoline)
 * with a stand-in `this` whose Side is `side`. Set k with fu_skirmish_set_s2. */
int fu_skirmish_engine_query(const void *side);
void fu_skirmish_set_s2(unsigned k);

/* The Side object to match with. Returns `side` itself for OWN and NONE. */
const void *fu_skirmish_resolve(const void *side, fu_skirmish_step *step);

/* 1 the first time a Side is reported unresolved (to log it once). */
int fu_skirmish_first_unresolved(const void *side);
void fu_skirmish_reset(void);

#endif
