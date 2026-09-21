/* plate.c — the EGA pictures CATALOG draws itself.
 *
 * Most titles in a catalogue have no artwork, and an empty picture box is
 * the biggest dead space on the screen.  This fills it: a plate dithered
 * out of blue and black, with a mark on it for whatever kind of thing the
 * title is.  One per category, so the right-hand side has something in it
 * for every title rather than for the lucky few.
 *
 * Everything here is drawn, not stored.  The marks are laid out on a grid
 * of 32 by 26 and each cell is painted as a square of three pixels, which
 * is what gives them the chunky look of art made for a 640x350 screen; the
 * shades that are not in the palette are dithered out of the sixteen EGA
 * colours, which the canvas keeps at the bottom of the palette and which
 * cost this program nothing.  Nothing here allocates and nothing here
 * depends on the clock, so a golden frame draws the same every time. */
#include "internal.h"
#include "setup/internal.h"
#include <string.h>

/* ---- the grid the marks are drawn on ---------------------------------- */

enum { GW = 32, GH = 26, CELL = 3, CLEAR = 0xFF };
#define MARK_W (GW * CELL)
#define MARK_H (GH * CELL)

static uint8_t g[GH][GW];

static void p_clear(void) {
    memset(g, CLEAR, sizeof g);
}

static void p_set(int x, int y, uint8_t c) {
    if (x >= 0 && x < GW && y >= 0 && y < GH)
        g[y][x] = c;
}

static void p_box(int x, int y, int w, int h, uint8_t c) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            p_set(x + i, y + j, c);
}

/* A disc, with the half radius added to the test so the edge sits where a
 * hand-drawn one would rather than a pixel short of it. */
static void p_disc(int cx, int cy, int r, uint8_t c) {
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++)
            if (i * i + j * j <= r * r + r / 2)
                p_set(cx + i, cy + j, c);
}

/* A line with a thickness: a disc walked from one end to the other. */
static void p_line(int x0, int y0, int x1, int y1, int r, uint8_t c) {
    int dx = x1 - x0, dy = y1 - y0;
    int steps =
        (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (steps < 1)
        steps = 1;
    for (int i = 0; i <= steps; i++)
        p_disc(x0 + dx * i / steps, y0 + dy * i / steps, r, c);
}

/* The keyline: every empty cell that touches a painted one goes black.
 * Drawn last, over the whole mark at once, which is how the outline stays
 * the same weight all the way round whatever was put down before it. */
static void p_outline(void) {
    uint8_t was[GH][GW];
    memcpy(was, g, sizeof was);
    for (int j = 0; j < GH; j++)
        for (int i = 0; i < GW; i++) {
            if (was[j][i] != CLEAR)
                continue;
            int touches =
                (i > 0 && was[j][i - 1] != CLEAR) || (i < GW - 1 && was[j][i + 1] != CLEAR) ||
                (j > 0 && was[j - 1][i] != CLEAR) || (j < GH - 1 && was[j + 1][i] != CLEAR);
            if (touches)
                g[j][i] = C_BLACK;
        }
}

/* ---- one mark for each kind of thing a catalogue holds ----------------- */

/* GAMES: a joystick, the one object that says the word on its own. */
static void mark_games(void) {
    for (int j = 0; j < 5; j++) /* the base, spreading as it comes forward */
        p_box(9 - j, 19 + j, 14 + 2 * j, 1, C_GREY);
    p_box(5, 24, 24, 1, C_DGREY); /* its front edge, in shadow */
    p_box(15, 9, 2, 10, C_DGREY); /* the stick */
    p_box(15, 9, 1, 10, C_GREY);  /* lit down one side */
    p_disc(16, 7, 4, C_RED);      /* the ball on top */
    p_disc(15, 6, 3, C_LRED);
    p_disc(14, 5, 1, C_WHITE); /* and the glint on it */
    p_disc(22, 20, 1, C_LRED); /* the fire button */
}

/* TOOLS: a spanner, laid across the plate. */
static void mark_tools(void) {
    p_line(9, 19, 23, 7, 1, C_GREY);
    p_line(9, 20, 23, 8, 1, C_DGREY); /* the underside */
    p_disc(24, 6, 4, C_GREY);         /* the jaws, each a ring */
    p_disc(24, 6, 2, CLEAR);
    p_box(24, 1, 6, 5, CLEAR); /* opened, one up and one down */
    p_disc(8, 20, 4, C_GREY);
    p_disc(8, 20, 2, CLEAR);
    p_box(2, 20, 6, 5, CLEAR);
}

/* LEARNING: a book, open, with the lines of type showing. */
static void mark_education(void) {
    for (int j = 0; j < 15; j++) {
        int in = j / 5; /* the leaves tuck in as they go down */
        p_box(3 + in, 6 + j, 12 - in, 1, C_WHITE);
        p_box(17, 6 + j, 12 - in, 1, C_WHITE);
    }
    p_box(15, 5, 2, 17, C_BROWN); /* the spine */
    p_box(15, 5, 1, 17, C_RED);
    for (int r = 8; r < 19; r += 3) { /* the type */
        p_box(5, r, 8, 1, C_GREY);
        p_box(19, r, 8, 1, C_GREY);
    }
}

/* MUSIC: a pair of quavers under their beam. */
static void mark_music(void) {
    p_disc(9, 19, 3, C_LCYAN);
    p_disc(21, 16, 3, C_LCYAN);
    p_box(11, 6, 2, 13, C_LCYAN);
    p_box(23, 3, 2, 13, C_LCYAN);
    for (int i = 0; i < 14; i++) /* the beam, sloping between the stems */
        p_box(11 + i, 6 - i * 3 / 13, 1, 3, C_LCYAN);
    p_disc(8, 18, 1, C_WHITE);
    p_disc(20, 15, 1, C_WHITE);
}

/* DEMOS: a sun with its rays, and a star either side of it. */
static void mark_demos(void) {
    static const int RAY[8][2] = {{0, -1}, {1, -1}, {1, 0},  {1, 1},
                                  {0, 1},  {-1, 1}, {-1, 0}, {-1, -1}};
    for (int k = 0; k < 8; k++)
        p_line(16 + RAY[k][0] * 7, 13 + RAY[k][1] * 7, 16 + RAY[k][0] * 11, 13 + RAY[k][1] * 11, 0,
               C_YELLOW);
    p_disc(16, 13, 5, C_BROWN);
    p_disc(16, 13, 4, C_YELLOW);
    p_disc(15, 12, 2, C_WHITE);
    p_box(4, 4, 1, 3, C_WHITE); /* the stars */
    p_box(3, 5, 3, 1, C_WHITE);
    p_box(28, 21, 1, 3, C_WHITE);
    p_box(27, 22, 3, 1, C_WHITE);
}

/* MISC: a floppy, which is what anything else arrived on. */
static void mark_misc(void) {
    p_box(5, 4, 22, 20, C_DGREY);
    p_box(5, 4, 22, 1, C_GREY); /* lit along the top and the left */
    p_box(5, 4, 1, 20, C_GREY);
    for (int j = 0; j < 4; j++) /* the bevelled corner */
        for (int i = 0; i < 4 - j; i++)
            p_set(26 - i, 4 + j, CLEAR);
    p_box(12, 4, 9, 8, C_GREY); /* the shutter */
    p_box(14, 4, 5, 7, C_DGREY);
    p_box(8, 14, 16, 9, C_WHITE); /* the label, written on */
    for (int r = 16; r < 22; r += 2)
        p_box(10, r, 12, 1, C_GREY);
}

/* ---- the plate -------------------------------------------------------- */

/* The ground the mark sits on: near black, with EGA blue dithered over it
 * thickest at the top and a pool of the lighter blue behind the middle, so
 * the plate reads as lit from above rather than as a filled rectangle. */
static void plate_ground(int x, int y, int w, int h) {
    cv_rect(x, y, w, h, G_WELL);
    cv_dither(x, y, w, h, C_BLUE, 6, 1);
    /* the pool behind the mark, up to the middle and down again: a wash
     * that started at its full weight would show the row it started on */
    cv_dither(x, y, w, h / 2, C_LBLUE, 0, 2);
    cv_dither(x, y + h / 2, w, h - h / 2, C_LBLUE, 2, 0);
    cv_frame(x, y, w, h, G_LINE);
}

static void blit(int px, int py) {
    for (int j = 0; j < GH; j++)
        for (int i = 0; i < GW; i++)
            if (g[j][i] != CLEAR)
                cv_rect(px + i * CELL, py + j * CELL, CELL, CELL, g[j][i]);
}

void plate_picture(int x, int y, int w, int h, const char *category) {
    int pw = ART_W < w ? ART_W : w, ph = ART_H < h ? ART_H : h;
    int px = x + (w - pw) / 2, py = y + (h - ph) / 2;
    plate_ground(px, py, pw, ph);
    if (!category || !category[0])
        return;
    p_clear();
    if (!strcmp(category, "GAMES"))
        mark_games();
    else if (!strcmp(category, "TOOLS"))
        mark_tools();
    else if (!strcmp(category, "LEARNING"))
        mark_education();
    else if (!strcmp(category, "MUSIC"))
        mark_music();
    else if (!strcmp(category, "DEMOS"))
        mark_demos();
    else
        mark_misc();
    p_outline();
    blit(px + (pw - MARK_W) / 2, py + (ph - MARK_H) / 2);
}

/* The colour bars, at the end of the rule under the tabs.  Seven of the EGA
 * sixteen in the order a test card puts them, four pixels wide: the mark a
 * printer left in the margin and a card left down the side of the screen,
 * and small enough to be part of the furniture rather than something to
 * look at. */
void plate_colourbar(int x, int y) {
    static const uint8_t BAR[7] = {C_WHITE,    C_YELLOW, C_LCYAN, C_LGREEN,
                                   C_LMAGENTA, C_LRED,   C_LBLUE};
    for (int i = 0; i < 7; i++)
        cv_rect(x + i * 5, y, 4, 3, BAR[i]);
}

int plate_colourbar_w(void) {
    return 7 * 5 - 1;
}
