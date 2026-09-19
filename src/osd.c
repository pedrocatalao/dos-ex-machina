/* osd.c — see osd.h.  The picture is drawn on the CPU into an RGBA image
 * the size of the tube's own 640x480, and the composite pass mixes it into
 * the picture before the scanlines, the mask and the curvature. */
#include "osd.h"
#include "font.h"
#include <SDL3/SDL_scancode.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name; /* on the glass, and the key in crt.cfg */
    float *val;
    float lo, hi;
    int page;
} setting;

enum { PAGE_PICTURE, PAGE_GEOMETRY, PAGE_TUBE, PAGES };
static const char *const PAGE_NAME[PAGES] = {"PICTURE", "GEOMETRY", "TUBE"};

#define MAXS 20
static struct {
    setting s[MAXS];
    int n;
    int shown, row;
    uint8_t px[OSD_W * OSD_H * 4];
    float drawn[MAXS]; /* the values the picture shows */
    int drawn_row, drawn_valid;
} O;

/* crt.cfg's keys are the names the old panel wrote, so a saved file from
 * before the OSD still loads */
static void add(const char *name, float *v, float lo, float hi, int page) {
    if (O.n < MAXS)
        O.s[O.n++] = (setting){name, v, lo, hi, page};
}

void osd_init(gpu_knobs *k) {
    O.n = 0;
    add("BRIGHTNESS", &k->brightness, 0.0f, 1.0f, PAGE_PICTURE);
    add("CONTRAST", &k->contrast, 0.4f, 1.8f, PAGE_PICTURE);
    add("SHARPNESS", &k->sharpness, 0.0f, 1.0f, PAGE_PICTURE); /* text's edges */
    add("BLOOM", &k->bloom, 0.0f, 1.5f, PAGE_PICTURE);
    add("PERSISTENCE", &k->persistence, 0.0f, 1.0f, PAGE_PICTURE);
    add("SCANLINES", &k->scan, 0.0f, 1.0f, PAGE_PICTURE);
    add("CURVATURE", &k->warp, 0.0f, 0.45f, PAGE_GEOMETRY);
    add("JITTER", &k->jitter, 0.0f, 1.0f, PAGE_GEOMETRY);
    add("HORIZ SYNC", &k->hsync, 0.0f, 1.0f, PAGE_GEOMETRY);
    add("RGB SHIFT", &k->rgb_shift, 0.0f, 1.0f, PAGE_GEOMETRY);
    /* the tube's own dot structure, the grille's stripes and the columns'
     * division: on this page because a page holds six rows, and PICTURE's
     * are taken */
    add("MASK", &k->mask, 0.0f, 1.0f, PAGE_GEOMETRY);
    add("PIXEL GRID", &k->vgrid, 0.0f, 1.0f, PAGE_GEOMETRY);
    add("BURN IN", &k->burn_in, 0.0f, 1.0f, PAGE_TUBE);
    add("STATIC NOISE", &k->noise, 0.0f, 1.0f, PAGE_TUBE);
    add("FLICKERING", &k->flicker, 0.0f, 1.0f, PAGE_TUBE);
    add("GLOW LINE", &k->glow_line, 0.0f, 1.0f, PAGE_TUBE);
    add("CHASSIS GLOW", &k->chassis_glow, 0.0f, 1.5f, PAGE_TUBE);
    add("AMBIENT LIGHT", &k->ambient, 0.0f, 1.0f, PAGE_TUBE);
    O.drawn_valid = 0;
}

int osd_visible(void) {
    return O.shown;
}

void osd_toggle(void) {
    O.shown = !O.shown;
    if (O.shown)
        O.row = 0;
    O.drawn_valid = 0;
}

/* the first setting on a page */
static int page_first(int page) {
    for (int i = 0; i < O.n; i++)
        if (O.s[i].page == page)
            return i;
    return 0;
}

/* One step is one of the hundred the value shows, and Shift is ten; the
 * value lands on the step, so it reads back as the number it was set to. */
static void adjust(int dir, int coarse) {
    setting *s = &O.s[O.row];
    float span = s->hi - s->lo;
    int p = (int)((*s->val - s->lo) / span * 100.0f + 0.5f) + dir * (coarse ? 10 : 1);
    p = p < 0 ? 0 : p > 100 ? 100 : p;
    *s->val = s->lo + span * (float)p / 100.0f;
}

int osd_key(int sc, int shift) {
    if (!O.shown)
        return 0;
    switch (sc) {
    case SDL_SCANCODE_ESCAPE:
        O.shown = 0;
        return 1;
    case SDL_SCANCODE_UP:
        O.row = (O.row + O.n - 1) % O.n;
        return 1;
    case SDL_SCANCODE_DOWN:
        O.row = (O.row + 1) % O.n;
        return 1;
    case SDL_SCANCODE_LEFT:
        adjust(-1, shift);
        return 1;
    case SDL_SCANCODE_RIGHT:
        adjust(1, shift);
        return 1;
    case SDL_SCANCODE_TAB:
        /* the next page, and Shift+TAB the one before */
        O.row = page_first((O.s[O.row].page + (shift ? PAGES - 1 : 1)) % PAGES);
        return 1;
    case SDL_SCANCODE_PAGEUP:
        O.row = page_first((O.s[O.row].page + PAGES - 1) % PAGES);
        return 1;
    case SDL_SCANCODE_PAGEDOWN:
        O.row = page_first((O.s[O.row].page + 1) % PAGES);
        return 1;
    case SDL_SCANCODE_HOME:
        *O.s[O.row].val = O.s[O.row].lo;
        return 1;
    case SDL_SCANCODE_END:
        *O.s[O.row].val = O.s[O.row].hi;
        return 1;
    default:
        return 0;
    }
}

/* ---- the picture ------------------------------------------------------ */

/* Straight alpha over what is there, for the box and what goes on it. */
static void put(int x, int y, int r, int g, int b, int a) {
    if (x < 0 || y < 0 || x >= OSD_W || y >= OSD_H)
        return;
    uint8_t *p = O.px + ((size_t)y * OSD_W + x) * 4;
    int da = p[3], oa = a + da * (255 - a) / 255;
    if (oa <= 0)
        return;
    for (int k = 0; k < 3; k++) {
        int src = k == 0 ? r : k == 1 ? g : b;
        p[k] = (uint8_t)((src * a + p[k] * da * (255 - a) / 255) / oa);
    }
    p[3] = (uint8_t)oa;
}

static void rect(int x, int y, int w, int h, int r, int g, int b, int a) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            put(i, j, r, g, b, a);
}

/* The monitor's own character generator: the 8x8, doubled, which is
 * chunkier than anything the PC puts on the screen. */
#define CH 16
static int text(int x, int y, const char *s, int grey, int a) {
    int n = 0;
    for (; s[n]; n++) {
        const uint8_t *gl = font_glyph((unsigned char)s[n]);
        for (int j = 0; j < 8; j++)
            for (int i = 0; i < 8; i++)
                if (gl[j] & (0x80 >> i))
                    rect(x + (n * 8 + i) * 2, y + j * 2, 2, 2, grey, grey, grey, a);
    }
    return n * CH;
}

/* The box: the lower half of the tube, a little in from its edges, so the
 * top half of the picture stays in view while it is adjusted. */
#define BOX_X 24
#define BOX_Y 232
#define BOX_W (OSD_W - 2 * BOX_X)
#define BOX_H 230
#define PAD 14
#define ROW_H 24
#define NAME_X (BOX_X + PAD + 20)
#define BAR_X (BOX_X + 290)
#define BAR_W 220
#define BAR_H 12

static const int BOX_A = 205;               /* how much of the picture it hides */
static const int BOX_RGB[3] = {22, 22, 88}; /* a dark blue */
static const int INK = 190, INK_ON = 236, INK_DIM = 120;
/* The selected row: the box's own blue, brighter by this much.  Scaled, not
 * mixed toward white, so it keeps the box's hue and saturation - a paler
 * band turns what shows through it grey where the box shows it blue. */
static const float BAND_GAIN = 1.6f;

/* Set a rectangle outright, not over what is there. */
static void fill(int x, int y, int w, int h, const int rgb[3], int a) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) {
            if (i < 0 || j < 0 || i >= OSD_W || j >= OSD_H)
                continue;
            uint8_t *q = O.px + ((size_t)j * OSD_W + i) * 4;
            q[0] = (uint8_t)rgb[0];
            q[1] = (uint8_t)rgb[1];
            q[2] = (uint8_t)rgb[2];
            q[3] = (uint8_t)a;
        }
}

/* How opaque a lighter fill must be for the picture to show through it no
 * more than through the box.  The OSD is mixed into the signal before the
 * tube turns it into light, and that turn is steep at the bright end: the
 * same share of picture swings the light of a lighter fill far more than of
 * a darker one, so at the box's own alpha the selected row reads as the
 * more transparent.  The swing goes as the slope of the tube's 2.2 curve,
 * so the picture's share is scaled by the fills' luma to the power 1.2. */
static int matched_alpha(const int base[3], const int fill_rgb[3], int base_a) {
    float yb = 0.2126f * (float)base[0] + 0.7152f * (float)base[1] + 0.0722f * (float)base[2];
    float yf =
        0.2126f * (float)fill_rgb[0] + 0.7152f * (float)fill_rgb[1] + 0.0722f * (float)fill_rgb[2];
    if (yf <= yb || yb <= 0.0f)
        return base_a;
    float share = (1.0f - (float)base_a / 255.0f) * powf(yb / yf, 1.2f);
    return (int)(255.0f * (1.0f - share) + 0.5f);
}

static void draw(void) {
    memset(O.px, 0, sizeof O.px);
    fill(BOX_X, BOX_Y, BOX_W, BOX_H, BOX_RGB, BOX_A);

    int page = O.s[O.row].page;
    /* the pages across the top, this one lit and underlined */
    int x = BOX_X + PAD, y = BOX_Y + PAD;
    for (int p = 0; p < PAGES; p++) {
        int w = text(x, y, PAGE_NAME[p], p == page ? INK_ON : INK_DIM, 255);
        if (p == page)
            rect(x, y + CH + 2, w, 2, INK_ON, INK_ON, INK_ON, 255);
        x += w + 32;
    }
    char count[16];
    snprintf(count, sizeof count, "%d/%d", O.row + 1, O.n);
    text(BOX_X + BOX_W - PAD - (int)strlen(count) * CH, y, count, INK_DIM, 255);

    /* the settings on this page */
    y = BOX_Y + PAD + CH + 18;
    for (int i = 0; i < O.n; i++) {
        if (O.s[i].page != page)
            continue;
        int on = (i == O.row), ink = on ? INK_ON : INK;
        int ty = y + (ROW_H - CH) / 2;
        if (on) {
            int band[3];
            for (int c = 0; c < 3; c++) {
                int v = (int)((float)BOX_RGB[c] * BAND_GAIN + 0.5f);
                band[c] = v > 255 ? 255 : v;
            }
            fill(BOX_X + 4, y, BOX_W - 8, ROW_H, band, matched_alpha(BOX_RGB, band, BOX_A));
            text(BOX_X + PAD, ty, "\x10", INK_ON, 255); /* the pointer: a right triangle */
        }
        text(NAME_X, ty, O.s[i].name, ink, 255);
        float f = (*O.s[i].val - O.s[i].lo) / (O.s[i].hi - O.s[i].lo);
        f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
        int by = y + (ROW_H - BAR_H) / 2;
        rect(BAR_X, by, BAR_W, BAR_H, ink, ink, ink, 70);
        rect(BAR_X, by, (int)((float)BAR_W * f + 0.5f), BAR_H, ink, ink, ink, 255);
        char v[8];
        snprintf(v, sizeof v, "%3d", (int)(f * 100.0f + 0.5f));
        text(BAR_X + BAR_W + 12, ty, v, ink, 255);
        y += ROW_H;
    }

    /* what the keys do, along the foot */
    text(BOX_X + PAD, BOX_Y + BOX_H - PAD - CH, "\x18\x19 SELECT  \x1b\x1a ADJUST  TAB PAGE",
         INK_DIM, 255);
}

const uint8_t *osd_frame(int *changed) {
    *changed = 0;
    if (!O.shown) {
        O.drawn_valid = 0;
        return NULL;
    }
    int same = O.drawn_valid && O.drawn_row == O.row;
    for (int i = 0; same && i < O.n; i++)
        if (O.drawn[i] != *O.s[i].val)
            same = 0;
    if (!same) {
        draw();
        for (int i = 0; i < O.n; i++)
            O.drawn[i] = *O.s[i].val;
        O.drawn_row = O.row;
        O.drawn_valid = 1;
        *changed = 1;
    }
    return O.px;
}

void osd_save(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    for (int i = 0; i < O.n; i++)
        fprintf(f, "%s=%.4f\n", O.s[i].name, (double)*O.s[i].val);
    fclose(f);
}

void osd_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        for (int i = 0; i < O.n; i++)
            if (!strcmp(O.s[i].name, line)) {
                *O.s[i].val = (float)atof(eq + 1);
                break;
            }
    }
    fclose(f);
}
