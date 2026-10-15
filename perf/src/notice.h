/* The catch-up notice: a line of text in the top right corner of the back buffer,
 * drawn with IDirect3DDevice9::Clear rectangles only (no shader, texture,
 * vertex buffer or state block; the viewport and the scissor test are put
 * back as they were). Nothing in the game's UI is touched. */
#ifndef PF_NOTICE_H
#define PF_NOTICE_H

typedef struct {
    long x1, y1, x2, y2; /* D3DRECT */
} pf_notice_rect;

#define PF_NOTICE_MAX_TEXT 47
#define PF_NOTICE_MAX_RECTS 1024

/* The rectangles for `text` on a width x height target: [0] the background,
 * then the text's pixel runs. Characters outside 32..126 are drawn as '?'.
 * Returns the count (0 if it doesn't fit). */
unsigned pf_notice_layout(const char *text, unsigned width, unsigned height, pf_notice_rect *out,
                          unsigned cap);
/* text with "..." at the end (dots and spaces it already ends with dropped
 * first), cut to fit cap and PF_NOTICE_MAX_TEXT. The font has no U+2026. */
void pf_notice_text(const char *text, char *out, unsigned cap);
/* The glyph rows of c (7 rows, 5 bits, bit 4 the leftmost column). */
const unsigned char *pf_notice_glyph(char c);
/* Draws on the device's current render target if it is the back buffer. */
void pf_notice_draw(void *device, const char *text);

#endif
