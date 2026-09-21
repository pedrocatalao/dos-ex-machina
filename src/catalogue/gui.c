/* gui.c — the look of CATALOG: dark and plain.  A warm dark ground with no
 * chrome on it, wells a shade darker with a hairline round them, light
 * grey Helvetica, white for what matters, gold for the keys and the one
 * chosen thing.  Drawn on the SETUP canvas, in the palette entries between
 * the VGA sixteen and the artwork. */
#include "internal.h"
#include "setup/internal.h"
#include <string.h>

/* the interface's colours, in the order of the enum */
static const uint8_t UI[G_COUNT - G_BG][3] = {
    {40, 40, 44},    /* G_BG */
    {24, 24, 28},    /* G_WELL */
    {88, 88, 96},    /* G_LINE */
    {212, 212, 206}, /* G_TEXT */
    {140, 140, 136}, /* G_TEXT2 */
    {250, 250, 246}, /* G_WHITE */
    {66, 66, 74},    /* G_BAR */
    {218, 168, 40},  /* G_GOLD */
    {70, 170, 90},   /* G_GREEN */
    {200, 70, 60},   /* G_RED */
    {96, 96, 100},   /* G_OFF */
    {52, 52, 58},    /* G_TROUGH */
    {120, 120, 126}, /* G_THUMB */
};

void gui_init(void) {
    cv_palette(G_BG, G_COUNT - G_BG, &UI[0][0]);
}

void gui_backdrop(void) {
    cv_clear(G_BG);
    gui_init();
    /* The ground is not one even slab: a wash of the bar's grey over the
     * top of it, dithered away as it comes down, so there is a light in the
     * screen.  Two shades apart at its thickest - enough to feel and not
     * enough to see. */
    cv_dither(0, 0, SCR_W, SCR_H * 2 / 3, G_BAR, 3, 0);
}

void gui_well(int x, int y, int w, int h) {
    cv_rect(x, y, w, h, G_WELL);
    cv_frame(x, y, w, h, G_LINE);
}

void gui_tab(int x, int y, const char *label, int on, int *w) {
    int lw = on ? cv_text_bold(x, y, label, G_WHITE, -1) : cv_text(x, y, label, G_TEXT2, -1);
    if (on)
        cv_rect(x, y + CV_LINE + 1, lw, 2, G_GOLD);
    if (w)
        *w = lw;
}

void gui_field(int x, int y, int w, int h, const char *text, const char *empty, int caret) {
    gui_well(x, y, w, h);
    int ty = y + (h - CV_LINE) / 2;
    if (text[0]) {
        int tw = cv_text(x + 8, ty, text, G_WHITE, -1);
        if (caret)
            cv_rect(x + 8 + tw + 1, ty + 1, 1, CV_LINE - 2, G_GOLD);
    } else {
        if (caret)
            cv_rect(x + 8, ty + 1, 1, CV_LINE - 2, G_GOLD);
        cv_text(x + 12, ty, empty, G_TEXT2, -1);
    }
}

/* A vertical scrollbar: a trough with a thumb sized to what is showing,
 * and a chevron at each end. */
void gui_scrollbar(int x, int y, int w, int h, float at, float shown) {
    cv_rect(x, y, w, h, G_TROUGH);
    for (int k = 0; k < 3; k++) {
        cv_rect(x + w / 2 - k, y + 4 + k, 2 * k + 1, 1, G_TEXT2);
        cv_rect(x + w / 2 - k, y + h - 5 - k, 2 * k + 1, 1, G_TEXT2);
    }
    int track = h - 2 * (w + 2), th = (int)((float)track * shown + 0.5f);
    if (th < 8)
        th = 8;
    if (th > track)
        th = track;
    int ty = y + w + 2 + (int)((float)(track - th) * at + 0.5f);
    cv_rect(x + 2, ty, w - 4, th, G_THUMB);
}

/* The picture: the artwork where a title has some, and where it has not -
 * which is most of them - the plate CATALOG draws for itself, with the mark
 * for the title's kind on it (plate.c). */
void gui_picture(int x, int y, int w, int h, const art_img *img, const char *category) {
    if (img && img->w) {
        int ix = x + (w - img->w) / 2, iy = y + (h - img->h) / 2;
        cv_image(ix, iy, img->w, img->h, img->px, ART_FIRST);
        return;
    }
    plate_picture(x, y, w, h, category);
}

int gui_hint(int x, int y, const char *key, const char *what, int hover, int off) {
    uint8_t kc = off ? G_OFF : (hover ? G_WHITE : G_GOLD);
    uint8_t wc = off ? G_OFF : (hover ? G_WHITE : G_TEXT);
    int pen = x;
    pen += cv_text_bold(pen, y, key, kc, -1) + 6;
    pen += cv_text(pen, y, what, wc, -1);
    return pen - x;
}

/* A filter: a small well holding what it is set to, with its name before
 * the value when it has one.  Gold when it is narrowing the list. */
void gui_chip(int x, int y, int w, int h, const char *label, const char *value, int active,
              int hover) {
    cv_rect(x, y, w, h, hover ? G_BAR : G_WELL);
    cv_frame(x, y, w, h, active ? G_GOLD : (hover ? G_TEXT2 : G_LINE));
    /* in the small face: a filter is a control, not reading */
    int tw = (label ? cv_width_small(label) + 5 : 0) + cv_width_small(value);
    int pen = x + (w - tw) / 2, ty = y + (h - CV_LINE) / 2;
    if (label)
        pen += cv_text_small(pen, ty, label, G_TEXT2) + 5;
    cv_text_small(pen, ty, value, active ? G_GOLD : G_WHITE);
}

/* A panel over the screen: the ground, a hairline, and a darker edge so it
 * stands off what is veiled behind it. */
void gui_panel(int x, int y, int w, int h) {
    cv_rect(x + 3, y + 3, w, h, G_WELL);
    cv_rect(x, y, w, h, G_BG);
    cv_frame(x, y, w, h, G_TEXT2);
}

void gui_bar(int x, int y, int w, int h, float t) {
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    cv_rect(x, y, w, h, G_TROUGH);
    cv_rect(x, y, (int)((float)w * t + 0.5f), h, G_GOLD);
}
