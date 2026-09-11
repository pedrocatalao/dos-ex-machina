/* marks.c — what is printed, stuck or engraved on the case: the MULTIMEDIA
 * sticker, and the two marks cut into the plastic, the DXM mark on the base
 * and the dotted mark above the left speaker. */
#include "internal.h"
#include "gen/corner_sticker.h"
#include "gen/mark.h"
#include "gen/multimedia.h"

/* The maker's mark cut INTO the case: the badge's alpha, box-filtered to
 * the size it lands at, read as depth and shaded from its gradient by the
 * same engraver the corner mark uses.  The print's colour is then laid in
 * the cut at reduced strength - a paint-filled engraving - so the X keeps
 * its stripes without the mark reading as a sticker. */
void engrave_mark(canvas *c, float cx, float cy, float w, float fill) {
    float h = w * (float)DXM_MARK_HT / (float)DXM_MARK_W;
    int dw = (int)ceilf(w), dh = (int)ceilf(h);
    if (dw < 4 || dh < 4)
        return;
    float *dep = calloc((size_t)dw * dh, sizeof *dep);
    if (!dep)
        return;
    float sx = (float)DXM_MARK_W / w, sy = (float)DXM_MARK_HT / h;
    float x = cx - w * 0.5f, y = cy - h * 0.5f;
    int saved = canvas_grain;
    canvas_grain = 0;
    for (int j = 0; j < dh; j++)
        for (int i = 0; i < dw; i++) {
            int u0 = (int)(i * sx), u1 = (int)((i + 1) * sx);
            if (u1 <= u0)
                u1 = u0 + 1;
            int v0 = (int)(j * sy), v1 = (int)((j + 1) * sy);
            if (v1 <= v0)
                v1 = v0 + 1;
            if (u1 > DXM_MARK_W)
                u1 = DXM_MARK_W;
            if (v1 > DXM_MARK_HT)
                v1 = DXM_MARK_HT;
            if (u0 >= DXM_MARK_W || v0 >= DXM_MARK_HT)
                continue;
            long r = 0, g = 0, b = 0, a = 0;
            int n = 0;
            for (int v = v0; v < v1; v++)
                for (int u = u0; u < u1; u++) {
                    const uint8_t *sp = dxm_mark + ((size_t)v * DXM_MARK_W + u) * 4;
                    r += sp[0] * sp[3];
                    g += sp[1] * sp[3];
                    b += sp[2] * sp[3];
                    a += sp[3];
                    n++;
                }
            float al = (float)a / (255.0f * (float)n);
            dep[(size_t)j * dw + i] = al;
            /* the paint in the cut: the print's colour, dulled, at part strength */
            if (a && fill > 0.0f) {
                float R = (float)r / a, G = (float)g / a, B = (float)b / a;
                /* pushed away from grey rather than toward it, and taken
                 * down a step: paint sitting in a cut reads deeper than the
                 * same paint on the surface */
                float lum = 0.299f * R + 0.587f * G + 0.114f * B;
                R = (R + (R - lum) * 0.35f) * 0.82f;
                G = (G + (G - lum) * 0.35f) * 0.82f;
                B = (B + (B - lum) * 0.35f) * 0.82f;
                if (R < 0)
                    R = 0;
                if (G < 0)
                    G = 0;
                if (B < 0)
                    B = 0;
                if (R > 255)
                    R = 255;
                if (G > 255)
                    G = 255;
                if (B > 255)
                    B = 255;
                px_blend(c, (int)x + i, (int)y + j, (int)R, (int)G, (int)B, al * fill);
            }
        }
    canvas_grain = saved;
    engrave_field(c, x, y, dw, dh, dep);
    free(dep);
}

/* Cut a depth field into the plastic.  dep is dw x dh, 0 = untouched
 * surface, 1 = full depth; its top-left lands at (x,y).
 *
 * An engraving changes no colour - the material at the bottom of the cut
 * is the material on the face - so this only ever shades what is already
 * there.  The shading comes from the depth field's gradient: the wall you
 * meet first is turned away from the key light and falls dark, the far wall
 * is turned into it and catches a highlight, and the floor between them
 * sits in its own shade.  A fixed light-above / dark-below pair, which is
 * fine for lettering, would be wrong for a mark whose walls face every
 * direction. */
void engrave_field(canvas *c, float x, float y, int dw, int dh, const float *dep) {
    float *bl = calloc((size_t)dw * dh, sizeof *bl);
    if (!bl)
        return;
    /* Soften it by a pixel before differentiating.  A tool leaves a wall
     * with some width to it; off the raw mask the gradient lives in a
     * single pixel and the cut reads as an outline drawn round the mark. */
    for (int j = 0; j < dh; j++)
        for (int i = 0; i < dw; i++) {
            float acc = 0.0f;
            int n = 0;
            for (int v = -1; v <= 1; v++)
                for (int u = -1; u <= 1; u++) {
                    int si = i + u, sj = j + v;
                    if (si < 0 || sj < 0 || si >= dw || sj >= dh) {
                        n++;
                        continue;
                    }
                    acc += dep[(size_t)sj * dw + si];
                    n++;
                }
            bl[(size_t)j * dw + i] = acc / (float)n;
        }
    for (int j = 0; j < dh; j++)
        for (int i = 0; i < dw; i++) {
            float d0 = bl[(size_t)j * dw + i];
            float gx = ((i + 1 < dw) ? bl[(size_t)j * dw + i + 1] : d0) -
                       ((i > 0) ? bl[(size_t)j * dw + i - 1] : d0);
            float gy = ((j + 1 < dh) ? bl[(size_t)(j + 1) * dw + i] : d0) -
                       ((j > 0) ? bl[(size_t)(j - 1) * dw + i] : d0);
            if (d0 < 0.004f && fabsf(gx) + fabsf(gy) < 0.004f)
                continue;
            /* the normal of a CUT tilts with the depth gradient, not against
             * it, which is what puts the highlight on the far wall */
            float lam = (gx * LIGHT_X + gy * LIGHT_Y) * 0.5f;
            px_shade(c, (int)x + i, (int)y + j, 1.0f - d0 * 0.085f + lam * 1.15f,
                     fmaxf(lam, 0.0f) * 0.20f);
        }
    free(bl);
}

/* The dotted mark: the alpha of assets/corner-sticker.png, box-filtered to
 * the size it lands at and cut into the plastic as depth (engrave_field),
 * `w` wide and centred on (cx, cy). */
void corner_engraving(canvas *c, float cx, float cy, float w) {
    float h = w * (float)CORNER_STICKER_HT / (float)CORNER_STICKER_W;
    int dw = (int)w, dh = (int)h;
    if (dw < 4 || dh < 4)
        return;
    float *dep = calloc((size_t)dw * dh, sizeof *dep);
    if (!dep)
        return;

    /* box-filter the alpha down to the size it actually landed at */
    {
        float sx = (float)CORNER_STICKER_W / (float)dw;
        float sy = (float)CORNER_STICKER_HT / (float)dh;
        for (int j = 0; j < dh; j++)
            for (int i = 0; i < dw; i++) {
                int u0 = (int)(i * sx), u1 = (int)((i + 1) * sx);
                if (u1 <= u0)
                    u1 = u0 + 1;
                int v0 = (int)(j * sy), v1 = (int)((j + 1) * sy);
                if (v1 <= v0)
                    v1 = v0 + 1;
                if (u1 > CORNER_STICKER_W)
                    u1 = CORNER_STICKER_W;
                if (v1 > CORNER_STICKER_HT)
                    v1 = CORNER_STICKER_HT;
                long a = 0;
                int n = 0;
                for (int v = v0; v < v1; v++)
                    for (int u = u0; u < u1; u++) {
                        a += corner_sticker[((size_t)v * CORNER_STICKER_W + u) * 4 + 3];
                        n++;
                    }
                dep[(size_t)j * dw + i] = (float)a / (255.0f * (float)n);
            }
    }
    engrave_field(c, cx - w * 0.5f, cy - h * 0.5f, dw, dh, dep);
    free(dep);
}

/* One texel of an RGBA image, premultiplied; transparent outside it. */
static void texel(const uint8_t *img, int iw, int ih, int i, int j, float out[4]) {
    if (i < 0 || j < 0 || i >= iw || j >= ih) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return;
    }
    const uint8_t *p = img + ((size_t)j * (size_t)iw + (size_t)i) * 4;
    float a = p[3] / 255.0f;
    out[0] = p[0] * a;
    out[1] = p[1] * a;
    out[2] = p[2] * a;
    out[3] = a;
}

/* An RGBA image drawn into the box x, y, w, h: sixteen bilinear taps a
 * pixel in premultiplied space, so it holds up scaled either way and its
 * transparent edges do not fringe.  `white` is the image value that prints
 * as full white - a levels move that lifts the paper and leaves the ink -
 * and coverage within a few percent of full is taken as full, so the body
 * of a printed sticker is solid rather than faintly see-through.  `fade`
 * washes it out, 0 not at all: the colour drains by that share and the
 * darks lift toward the warm grey a sun-faded print goes to. */
static void decal(canvas *c, const uint8_t *img, int iw, int ih, float x, float y, float w, float h,
                  float white, float fade) {
    float sx = w / (float)iw, sy = h / (float)ih;
    for (int py = (int)floorf(y); py <= (int)ceilf(y + h); py++)
        for (int px = (int)floorf(x); px <= (int)ceilf(x + w); px++) {
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (int ty = 0; ty < 4; ty++)
                for (int tx = 0; tx < 4; tx++) {
                    float u = ((float)px + (tx + 0.5f) / 4.0f - x) / sx - 0.5f;
                    float v = ((float)py + (ty + 0.5f) / 4.0f - y) / sy - 0.5f;
                    int i = (int)floorf(u), j = (int)floorf(v);
                    float fu = u - (float)i, fv = v - (float)j, t[4][4];
                    texel(img, iw, ih, i, j, t[0]);
                    texel(img, iw, ih, i + 1, j, t[1]);
                    texel(img, iw, ih, i, j + 1, t[2]);
                    texel(img, iw, ih, i + 1, j + 1, t[3]);
                    for (int k = 0; k < 4; k++)
                        acc[k] += (t[0][k] * (1.0f - fu) + t[1][k] * fu) * (1.0f - fv) +
                                  (t[2][k] * (1.0f - fu) + t[3][k] * fu) * fv;
                }
            float a = acc[3] / 16.0f;
            if (a <= 0.004f)
                continue;
            float lift = 255.0f / white / acc[3];
            float r = acc[0] * lift, g = acc[1] * lift, b = acc[2] * lift;
            if (fade > 0.0f) {
                float l = 0.299f * r + 0.587f * g + 0.114f * b;
                r += (l - r) * fade;
                g += (l - g) * fade;
                b += (l - b) * fade;
                r += (205.0f - r) * fade * 0.45f;
                g += (198.0f - g) * fade * 0.45f;
                b += (184.0f - b) * fade * 0.45f;
            }
            if (a > 0.96f)
                a = 1.0f;
            px_blend(c, px, py, (int)r, (int)g, (int)b, a);
        }
}

/* The MULTIMEDIA sticker: the artwork in assets/multimedia-sticker.png,
 * the word over the colour stripes, `w` wide and `taller` beyond its own
 * proportions, centred on (cx, cy), a little washed out, the way thirty
 * years of light leave a print.  Not drawn at all if it would not fit in
 * maxw x maxh. */
void multimedia_sticker(canvas *c, float cx, float cy, float w, float taller, float maxw,
                        float maxh) {
    float h = w * (float)DXM_MULTIMEDIA_HT / (float)DXM_MULTIMEDIA_W + taller;
    if (w > maxw || h > maxh)
        return;
    decal(c, dxm_multimedia, DXM_MULTIMEDIA_W, DXM_MULTIMEDIA_HT, cx - w * 0.5f, cy - h * 0.5f, w,
          h, 255.0f, 0.30f);
}
