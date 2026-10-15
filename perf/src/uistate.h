/* Read-only view of two InGameUI fields: the building the player is placing
 * and whether the camera is scrolling. Found by signature, read from the
 * shadow gate on the main thread; nothing in the game is called. */
#ifndef PF_UISTATE_H
#define PF_UISTATE_H

#include "scan.h"

typedef struct {
    void *const *ui;      /* TheInGameUI, the global pointer; NULL if not found */
    unsigned pending_off; /* InGameUI: the template being placed, NULL if none */
    unsigned scroll_off;  /* InGameUI: nonzero byte while the camera scrolls */
} pf_ui_state;

/* NULL on success, else what wasn't found. *out is zeroed on failure. */
const char *pf_ui_find(const pf_image *img, pf_ui_state *out);
int pf_ui_placing(const pf_ui_state *s);
int pf_ui_scrolling(const pf_ui_state *s);

#endif
