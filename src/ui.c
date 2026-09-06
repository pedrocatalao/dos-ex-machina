/* ui.c — mouse-driven slider panel for the CRT parameters. */
#include "ui.h"
#include "font.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef struct {
    const char *name;
    float *val;
    float lo, hi;
} param;

#define MAXP 20
/* The panel: its parameters, visibility, drag, bitmap and layout */
static struct {
    param param[MAXP];
    int n;
    int shown;
    int drag;
    uint8_t *buf;
    int bw, bh;
    /* panel geometry, in output pixels; scaled from display height */
    int x, y, w, row_h, label_w, slider_w, pad;
} panel = {.drag = -1};

static void add(const char *n, float *v, float lo, float hi) {
    if (panel.n < MAXP) {
        panel.param[panel.n].name = n;
        panel.param[panel.n].val = v;
        panel.param[panel.n].lo = lo;
        panel.param[panel.n].hi = hi;
        panel.n++;
    }
}

void ui_init(gpu_knobs *k) {
    panel.n = 0;
    add("BLOOM", &k->bloom, 0.0f, 1.5f);
    add("BURN IN", &k->burn_in, 0.0f, 1.0f);
    add("STATIC NOISE", &k->noise, 0.0f, 1.0f);
    add("JITTER", &k->jitter, 0.0f, 1.0f);
    add("GLOW LINE", &k->glow_line, 0.0f, 1.0f);
    add("AMBIENT LIGHT", &k->ambient, 0.0f, 1.0f);
    add("FLICKERING", &k->flicker, 0.0f, 1.0f);
    add("HORIZ SYNC", &k->hsync, 0.0f, 1.0f);
    add("RGB SHIFT", &k->rgb_shift, 0.0f, 1.0f);
    add("CHASSIS GLOW", &k->chassis_glow, 0.0f, 1.5f);
    add("PERSISTENCE", &k->persistence, 0.0f, 1.0f);
    add("SCANLINES", &k->scan, 0.0f, 1.0f);
    add("PIXEL GRID", &k->vgrid, 0.0f, 1.0f);
    add("CURVATURE", &k->warp, 0.0f, 0.45f);
    add("BRIGHTNESS", &k->brightness, 0.0f, 1.0f);
    add("CONTRAST", &k->contrast, 0.4f, 1.8f);
}
int ui_visible(void) {
    return panel.shown;
}
void ui_toggle(void) {
    panel.shown = !panel.shown;
    panel.drag = -1;
}

static void layout(int out_w, int out_h) {
    (void)out_w;
    float s = out_h / 1080.0f;
    if (s < 0.75f)
        s = 0.75f;
    panel.row_h = (int)(26 * s);
    panel.label_w = (int)(190 * s);
    panel.slider_w = (int)(260 * s);
    panel.pad = (int)(16 * s);
    panel.w = panel.label_w + panel.slider_w + (int)(70 * s) + panel.pad * 2;
    panel.x = (int)(28 * s);
    panel.y = (int)(28 * s);
}

int ui_mouse(int x, int y, int down, int moving) {
    if (!panel.shown)
        return 0;
    int ph = panel.pad * 2 + (int)(panel.row_h * 1.6f) + panel.n * panel.row_h;
    int inside = (x >= panel.x && x < panel.x + panel.w && y >= panel.y && y < panel.y + ph);
    if (!down && !moving) {
        panel.drag = -1;
        return inside;
    }
    if (down && !moving) {
        panel.drag = -1;
        if (!inside)
            return 0;
        int y0 = panel.y + panel.pad + (int)(panel.row_h * 1.6f);
        for (int i = 0; i < panel.n; i++) {
            int ry = y0 + i * panel.row_h;
            if (y >= ry && y < ry + panel.row_h) {
                panel.drag = i;
                break;
            }
        }
    }
    if (panel.drag >= 0) {
        int sx = panel.x + panel.pad + panel.label_w;
        float f = (float)(x - sx) / (float)panel.slider_w;
        if (f < 0)
            f = 0;
        if (f > 1)
            f = 1;
        *panel.param[panel.drag].val =
            panel.param[panel.drag].lo +
            f * (panel.param[panel.drag].hi - panel.param[panel.drag].lo);
        return 1;
    }
    return inside;
}

static void px(int x, int y, int r, int g, int b, int a) {
    if (x < 0 || y < 0 || x >= panel.bw || y >= panel.bh)
        return;
    uint8_t *p = panel.buf + ((size_t)y * panel.bw + x) * 4;
    float A = a / 255.0f;
    p[0] = (uint8_t)(p[0] * (1 - A) + r * A);
    p[1] = (uint8_t)(p[1] * (1 - A) + g * A);
    p[2] = (uint8_t)(p[2] * (1 - A) + b * A);
    p[3] = (uint8_t)(p[3] + (255 - p[3]) * A);
}
static void box(int x, int y, int w, int h, int r, int g, int b, int a) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            px(i, j, r, g, b, a);
}
static void label(int x, int y, const char *s, float sc, int r, int g, int b, int a) {
    for (int n = 0; s[n]; n++) {
        const uint8_t *gl = font_glyph((unsigned char)s[n]);
        for (int j = 0; j < 8; j++)
            for (int i = 0; i < 8; i++)
                if (gl[j] & (0x80 >> i))
                    for (int sy = 0; sy < (int)sc; sy++)
                        for (int sx = 0; sx < (int)sc; sx++)
                            px(x + (int)((n * 8 + i) * sc) + sx, y + (int)(j * sc) + sy, r, g, b,
                               a);
    }
}

const uint8_t *ui_render(int out_w, int out_h, int *w, int *h) {
    if (!panel.shown)
        return NULL;
    layout(out_w, out_h);
    if (!panel.buf || panel.bw != out_w || panel.bh != out_h) {
        free(panel.buf);
        panel.bw = out_w;
        panel.bh = out_h;
        panel.buf = malloc((size_t)panel.bw * panel.bh * 4);
        if (!panel.buf) {
            panel.bw = panel.bh = 0;
            return NULL;
        }
    }
    memset(panel.buf, 0, (size_t)panel.bw * panel.bh * 4);
    float s = out_h / 1080.0f;
    if (s < 0.75f)
        s = 0.75f;
    float tsc = s * 1.5f;
    if (tsc < 1.0f)
        tsc = 1.0f;
    int ph = panel.pad * 2 + (int)(panel.row_h * 1.6f) + panel.n * panel.row_h;

    box(panel.x, panel.y, panel.w, ph, 10, 12, 14, 214);             /* panel */
    box(panel.x, panel.y, panel.w, (int)(2 * s), 90, 220, 110, 200); /* top rule */
    label(panel.x + panel.pad, panel.y + panel.pad, "CRT ADJUST", tsc, 120, 235, 140, 255);
    label(panel.x + panel.w - panel.pad - (int)(8 * tsc * 4), panel.y + panel.pad, "F1", tsc, 90,
          110, 95, 255);

    int y0 = panel.y + panel.pad + (int)(panel.row_h * 1.6f);
    for (int i = 0; i < panel.n; i++) {
        int ry = y0 + i * panel.row_h;
        int sx = panel.x + panel.pad + panel.label_w, sy = ry + panel.row_h / 2 - (int)(3 * s);
        float f =
            (*panel.param[i].val - panel.param[i].lo) / (panel.param[i].hi - panel.param[i].lo);
        if (f < 0)
            f = 0;
        if (f > 1)
            f = 1;
        label(panel.x + panel.pad, ry + panel.row_h / 2 - (int)(4 * tsc), panel.param[i].name, tsc,
              176, 190, 180, 255);
        box(sx, sy, panel.slider_w, (int)(4 * s), 42, 50, 46, 235);             /* track  */
        box(sx, sy, (int)(panel.slider_w * f), (int)(4 * s), 70, 190, 95, 245); /* filled */
        int kx = sx + (int)(panel.slider_w * f);
        box(kx - (int)(3 * s), ry + panel.row_h / 2 - (int)(9 * s), (int)(6 * s), (int)(18 * s),
            (panel.drag == i) ? 230 : 170, 245, (panel.drag == i) ? 190 : 180, 255); /* handle */
        char v[16];
        snprintf(v, sizeof v, "%.2f", (double)*panel.param[i].val);
        label(sx + panel.slider_w + (int)(12 * s), ry + panel.row_h / 2 - (int)(4 * tsc), v, tsc,
              140, 160, 150, 255);
    }
    *w = panel.bw;
    *h = panel.bh;
    return panel.buf;
}

void ui_save(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    for (int i = 0; i < panel.n; i++)
        fprintf(f, "%s=%.4f\n", panel.param[i].name, (double)*panel.param[i].val);
    fclose(f);
}
void ui_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        for (int i = 0; i < panel.n; i++)
            if (!strcmp(panel.param[i].name, line)) {
                *panel.param[i].val = (float)atof(eq + 1);
                break;
            }
    }
    fclose(f);
}
