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
#include "gen/uifont.h"
#include "gen/uifont_bold.h"
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
    return banner_count();
}

void cv_banner(int which) {
    const banner *b = banner_open(which);
    if (!b)
        return;
    for (int i = 0; i < BANNER_COLOURS; i++)
        memcpy(pal[BANNER_FIRST + i], b->pal + i * 3, 3);
    for (int y = 0; y < b->h && y < SCR_H; y++)
        for (int x = 0; x < b->w && x < SCR_W; x++)
            put(x, y, (uint8_t)(BANNER_FIRST + b->px[y * b->w + x]));
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

/* How wide a VGA glyph actually is: the character generator gives every one
 * eight columns whether it needs them or not.  Only the few characters the
 * proportional face has not got are drawn from it now. */
static void ink(const uint8_t *g, int *x0, int *x1) {
    uint8_t m = 0;
    for (int j = 0; j < 16; j++)
        m |= g[j];
    if (!m) { /* a space: give it a word's worth and no more */
        *x0 = 0;
        *x1 = 2;
        return;
    }
    int a = 0, b = 7;
    while (!(m & (0x80 >> a)))
        a++;
    while (!(m & (0x80 >> b)))
        b--;
    *x0 = a;
    *x1 = b;
}

/* One glyph of the proportional face, hung off the baseline: the box BDF
 * gives sits with its bottom left corner at (xoff, yoff) from the origin,
 * which is what lets a comma drop below the line and a quote ride above it. */
static void glyph(const dxm_glyph *g, const uint8_t *bits, int pen, int base, uint8_t fg) {
    int stride = (g->w + 7) / 8;
    for (int j = 0; j < g->h; j++)
        for (int i = 0; i < g->w; i++)
            if (bits[g->at + j * stride + i / 8] & (0x80 >> (i & 7)))
                put(pen + g->xoff + i, base - g->yoff - g->h + j, fg);
}

/* The machine's own programs are set in Helvetica, the face the workstations
 * of the day put their interfaces in; the arrows and the marks it has no
 * glyph for come from the VGA font, which is where the rest of the machine
 * speaks from.  Returns how far the pen moved, so a caller can put the next
 * word after it without counting cells. */
static int text(int x, int y, const char *s, uint8_t fg, int bg, int bold) {
    const dxm_glyph *G = bold ? dxm_uib_glyphs : dxm_ui_glyphs;
    const uint8_t *B = bold ? dxm_uib_bits : dxm_ui_bits;
    int pen = x, base = y + DXM_UI_ASCENT;
    for (int n = 0; s[n]; n++) {
        unsigned char c = (unsigned char)s[n];
        if (c >= DXM_UI_FIRST && c <= DXM_UI_LAST) {
            const dxm_glyph *g = &G[c - DXM_UI_FIRST];
            if (bg >= 0)
                cv_rect(pen, y, g->adv, CV_LINE, (uint8_t)bg);
            glyph(g, B, pen, base, fg);
            pen += g->adv;
        } else {
            const uint8_t *v = font_glyph16(c);
            int x0, x1;
            ink(v, &x0, &x1);
            for (int j = 0; j < 16; j++)
                for (int i = x0; i <= x1; i++)
                    if (v[j] & (0x80 >> i))
                        put(pen + i - x0, y + j, fg);
            pen += x1 - x0 + 2;
        }
    }
    return pen - x;
}

int cv_text(int x, int y, const char *s, uint8_t fg, int bg) {
    return text(x, y, s, fg, bg, 0);
}
int cv_text_bold(int x, int y, const char *s, uint8_t fg, int bg) {
    return text(x, y, s, fg, bg, 1);
}

int cv_width(const char *s) {
    int w = 0;
    for (int n = 0; s[n]; n++) {
        unsigned char c = (unsigned char)s[n];
        if (c >= DXM_UI_FIRST && c <= DXM_UI_LAST)
            w += dxm_ui_glyphs[c - DXM_UI_FIRST].adv;
        else {
            int x0, x1;
            ink(font_glyph16(c), &x0, &x1);
            w += x1 - x0 + 2;
        }
    }
    return w;
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
