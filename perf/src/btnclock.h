/* The progress clock on command buttons (units and heroes in production,
 * upgrades, recharging powers) in extra frames. GameClient::update draws the
 * frame first and then runs the in-game UI's update, whose control bar
 * update asks for the clock on the button's data; the push button's draw
 * draws it and clears the request. So a request is made for the next draw:
 * without extra frames that is the next game frame's. Push buttons are
 * drawn by W3DGadgetPushButtonDraw, which picks one of three draws
 * (check-like, which Zero Hour's control bar uses for command buttons;
 * color; image); each draws the clock and clears the request.
 *
 * An extra frame's draw would consume the request: the extra frames after
 * it and the next game frame's draw would show the button without its
 * clock. So an extra frame notes, at that draw's entry, each request it
 * finds and sets it back after its draw: every extra frame and the game's
 * next draw see the requests as the control bar left them, and the game's
 * draw clears them as it would have without extra frames. */
#ifndef PF_BTNCLOCK_H
#define PF_BTNCLOCK_H

#include "scan.h"
#include "sites.h"

/* The push button draw at site (found by its signature): checks its three
 * draws each read the button data through the window's user data getter,
 * test the clock request byte at the data's start and clear it after
 * drawing. Returns NULL and the user data offset in the window, or what
 * didn't match. */
const char *pf_btn_find(const pf_image *img, const pf_site *site, unsigned *data_off);

/* data_off 0 = off (nothing is noted or set back). */
void pf_btn_init(unsigned data_off);
/* At the push button draw's entry: in the game's frame, counts a clock it
 * draws; in an extra frame, notes the request it will draw and clear. */
void pf_btn_draw(const void *window, int extra);
/* The control bar asked for a clock on window (its two request setters), in
 * the game's frame seq. Only counted: what is drawn comes from the draw. */
void pf_btn_request(const void *window, unsigned seq);
/* After an extra frame's draw in game frame seq: the requests its draw
 * cleared are set back. Returns the clocks it drew. */
unsigned pf_btn_extra_end(unsigned seq);

typedef struct {
    unsigned noted;    /* clocks drawn by the game's own draws */
    unsigned asked;    /* clocks drawn in extra frames (and their requests set back) */
    unsigned requests; /* the control bar's requests */
    /* game frames with an extra frame that drew no clock on a window the
     * control bar asked one for in that frame's update (it wasn't drawn, or
     * not through the hooked draw) */
    unsigned unseen;
} pf_btn_stats;

/* Totals since pf_btn_init. */
void pf_btn_get_stats(pf_btn_stats *s);

#endif
