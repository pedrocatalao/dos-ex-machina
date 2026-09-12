/* internal.h — shared inside src/setup only.  The public face is
 * src/setup.h. */
#ifndef DXM_SETUP_INTERNAL_H
#define DXM_SETUP_INTERNAL_H
#include <stdint.h>

/* The screen SETUP draws on: 640x400 in 256 colours.  The same geometry as
 * the machine's text screen, so the tube treats it exactly as it treats the
 * POST and DOS - and enough room for an interface with chrome rather than
 * rows of text.  The artwork is drawn at half that and doubled on the way
 * in: art of the period had visible pixels, the interface over it did not.
 *
 * The canvas is indexed, not RGB, the way such a program's own was: the
 * first sixteen entries are the interface's, the rest belong to whichever
 * artwork is on screen. */
#define SCR_W 640
#define SCR_H 400

/* The picture carries the same overscan border every other picture on this
 * machine does - the POST's text screen, and DOSBox's frames, which get it
 * in proportion (dosbox.c).  Without it the drawing stops short of the
 * aperture and the glass shows in a ring around it. */
#define SCR_PAD_X 14
#define SCR_PAD_Y 12
#define CANVAS_W (SCR_W + 2 * SCR_PAD_X)
#define CANVAS_H (SCR_H + 2 * SCR_PAD_Y)

/* the interface's sixteen, the VGA text palette by another name */
enum {
    C_BLACK = 0,
    C_BLUE,
    C_GREEN,
    C_CYAN,
    C_RED,
    C_MAGENTA,
    C_BROWN,
    C_GREY,
    C_DGREY,
    C_LBLUE,
    C_LGREEN,
    C_LCYAN,
    C_LRED,
    C_LMAGENTA,
    C_YELLOW,
    C_WHITE
};

/* canvas.c — the surface and what can be put on it */
void cv_clear(uint8_t colour);
void cv_banner(int which); /* the artwork at the top, and its palette */
void cv_rect(int x, int y, int w, int h, uint8_t colour);
void cv_frame(int x, int y, int w, int h, uint8_t colour);
/* Darken what is already there, leaving one pixel in `n` of it: the way a
 * program of the day made a panel you could see through, and the only way
 * on a canvas whose colours are indices and cannot be blended.  n=2 is a
 * veil, n=4 is nearly solid.  `r` rounds the corners off. */
void cv_scrim(int x, int y, int w, int h, int n, int r);
void cv_round_frame(int x, int y, int w, int h, uint8_t colour, int r);
void cv_text(int x, int y, const char *s, uint8_t fg, int bg); /* bg < 0: none */
void cv_pointer(int x, int y);
const uint8_t *cv_rgb(void); /* the canvas as SCR_W x SCR_H RGB8 */
int cv_banner_count(void);
#endif
