/* canvas.c — the 640x480 indexed surface SETUP is drawn on.
 *
 * Indexed, not RGB, and the split is the period one: entries 0..15 are the
 * interface's sixteen VGA colours, 16..255 belong to the artwork at the top
 * of the screen, which brings its own palette with it.  The gradient over
 * the artwork is already in the artwork (tools/mkbanners.py bakes it),
 * since indices cannot be blended.
 *
 * The whole thing is turned into RGB once a frame, on the way to the tube,
 * which is the only place colour actually happens. */
#include "internal.h"
#include "font.h"
#include "gen/banners.h"
#include <string.h>

/* the 16 VGA text colours, as the DAC actually produced them */
static const uint8_t VGA16[16][3] = {
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0xAA}, {0x00, 0xAA, 0x00}, {0x00, 0xAA, 0xAA},
    {0xAA, 0x00, 0x00}, {0xAA, 0x00, 0xAA}, {0xAA, 0x55, 0x00}, {0xAA, 0xAA, 0xAA},
    {0x55, 0x55, 0x55}, {0x55, 0x55, 0xFF}, {0x55, 0xFF, 0x55}, {0x55, 0xFF, 0xFF},
    {0xFF, 0x55, 0x55}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0x55}, {0xFF, 0xFF, 0xFF}};

/* The canvas is the picture plus its overscan; everything is drawn in the
 * picture's own coordinates and lands inside the border. */
static uint8_t canvas[CANVAS_W * CANVAS_H];
static uint8_t pal[256][3];
static uint8_t rgb[CANVAS_W * CANVAS_H * 3];

static void put(int x, int y, uint8_t colour) {
    if (x < 0 || x >= SCR_W || y < 0 || y >= SCR_H)
        return;
    canvas[(y + SCR_PAD_Y) * CANVAS_W + (x + SCR_PAD_X)] = colour;
}

static void pal_ui(void) {
    for (int i = 0; i < 16; i++)
        memcpy(pal[i], VGA16[i], 3);
}

void cv_clear(uint8_t colour) {
    memset(canvas, C_BLACK, sizeof canvas); /* the border stays black */
    for (int y = 0; y < SCR_H; y++)
        for (int x = 0; x < SCR_W; x++)
            put(x, y, colour);
    pal_ui();
}

int cv_banner_count(void) {
    return dxm_banner_count;
}

void cv_banner(int which) {
    if (dxm_banner_count <= 0)
        return;
    const dxm_banner *b = &dxm_banners[which % dxm_banner_count];
    for (int i = 0; i < DXM_BANNER_COLOURS; i++)
        memcpy(pal[DXM_BANNER_FIRST + i], b->pal + i * 3, 3);
    for (int y = 0; y < b->h && y < SCR_H; y++)
        for (int x = 0; x < DXM_BANNER_W && x < SCR_W; x++)
            put(x, y, (uint8_t)(DXM_BANNER_FIRST + b->px[y * DXM_BANNER_W + x]));
}

void cv_rect(int x, int y, int w, int h, uint8_t colour) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            put(i, j, colour);
}

void cv_frame(int x, int y, int w, int h, uint8_t colour) {
    cv_rect(x, y, w, 1, colour);
    cv_rect(x, y + h - 1, w, 1, colour);
    cv_rect(x, y, 1, h, colour);
    cv_rect(x + w - 1, y, 1, h, colour);
}

/* A panel you can see through: the pixels are knocked out to black in an
 * ordered pattern, so what is under it shows in the gaps.  Hard edges on
 * the canvas, but the tube's own blur closes them up into a veil - which
 * is exactly the trick the programs that did this were relying on. */
/* inside a rectangle whose corners are rounded off by r */
static int in_round(int i, int j, int x, int y, int w, int h, int r) {
    int cx = i < x + r ? x + r : (i > x + w - 1 - r ? x + w - 1 - r : i);
    int cy = j < y + r ? y + r : (j > y + h - 1 - r ? y + h - 1 - r : j);
    int dx = i - cx, dy = j - cy;
    return dx * dx + dy * dy <= r * r;
}

void cv_scrim(int x, int y, int w, int h, int n, int r) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) {
            if (r > 0 && !in_round(i, j, x, y, w, h, r))
                continue; /* outside the rounded edge: leave the artwork */
            int keep = (n <= 2) ? ((i + j) & 1) : ((i & 1) == 0 && (j & 1) == 0);
            if (!keep)
                put(i, j, C_BLACK);
        }
}

void cv_round_frame(int x, int y, int w, int h, uint8_t colour, int r) {
    cv_rect(x + r, y, w - 2 * r, 1, colour);
    cv_rect(x + r, y + h - 1, w - 2 * r, 1, colour);
    cv_rect(x, y + r, 1, h - 2 * r, colour);
    cv_rect(x + w - 1, y + r, 1, h - 2 * r, colour);
    for (int j = 0; j <= r; j++)
        for (int i = 0; i <= r; i++) {
            int d2 = (r - i) * (r - i) + (r - j) * (r - j);
            if (d2 > (r - 1) * (r - 1) && d2 <= r * r) {
                put(x + i, y + j, colour);
                put(x + w - 1 - i, y + j, colour);
                put(x + i, y + h - 1 - j, colour);
                put(x + w - 1 - i, y + h - 1 - j, colour);
            }
        }
}

/* The VGA character generator's own 8x16 face, the one the POST and DOS
 * draw with, so the machine speaks in one typeface throughout. */
void cv_text(int x, int y, const char *s, uint8_t fg, int bg) {
    for (int n = 0; s[n]; n++) {
        const uint8_t *g = font_glyph16((unsigned char)s[n]);
        int cx = x + n * 8;
        for (int j = 0; j < 16; j++)
            for (int i = 0; i < 8; i++) {
                if (g[j] & (0x80 >> i))
                    put(cx + i, y + j, fg);
                else if (bg >= 0)
                    put(cx + i, y + j, (uint8_t)bg);
            }
    }
}

/* The arrow the machine's own programs drew: a white wedge with a black
 * edge, so it reads over artwork as well as over a panel. */
static const char *const ARROW[] = {
    "X",         "XX",         "XoX",        "XooX",       "XoooX",     "XooooX",   "XoooooX",
    "XooooooX",  "XoooooooX",  "XooooooooX", "XoooooXXXX", "XooXooX",   "XoX XooX", "XX  XooX",
    "X    XooX", "      XooX", "       XoX", "        XX", "         X"};

void cv_pointer(int x, int y) {
    for (int j = 0; j < (int)(sizeof ARROW / sizeof ARROW[0]); j++)
        for (int i = 0; ARROW[j][i]; i++) {
            if (ARROW[j][i] == ' ')
                continue;
            put(x + i, y + j, ARROW[j][i] == 'o' ? C_WHITE : C_BLACK);
        }
}

const uint8_t *cv_rgb(void) {
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) {
        const uint8_t *c = pal[canvas[i]];
        rgb[i * 3] = c[0];
        rgb[i * 3 + 1] = c[1];
        rgb[i * 3 + 2] = c[2];
    }
    return rgb;
}
