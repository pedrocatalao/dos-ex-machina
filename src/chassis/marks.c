/* marks.c — what is printed, stuck or engraved on the case: the Horizon
 * badge, the 3dfx sticker, and the two marks cut into the plastic, the DXM
 * mark on the base and the dotted mark above the left speaker. */
#include "internal.h"
#include "gen/corner_sticker.h"
#include "gen/horizon.h"
#include "gen/mark.h"
#include "gen/tdfx.h"

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
static void texel(const uint8_t *img, int iw, int ih, int i, int j, float out[4]);

/* how much of the image is under a point, bilinear on its alpha alone */
static float cover(const uint8_t *img, int iw, int ih, float u, float v) {
    int i = (int)floorf(u), j = (int)floorf(v);
    float fu = u - (float)i, fv = v - (float)j, t[4][4];
    texel(img, iw, ih, i, j, t[0]);
    texel(img, iw, ih, i + 1, j, t[1]);
    texel(img, iw, ih, i, j + 1, t[2]);
    texel(img, iw, ih, i + 1, j + 1, t[3]);
    return (t[0][3] * (1.0f - fu) + t[1][3] * fu) * (1.0f - fv) +
           (t[2][3] * (1.0f - fu) + t[3][3] * fu) * fv;
}

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
 * darks lift toward the warm grey a sun-faded print goes to.  `tilt` turns
 * it that many degrees clockwise about its centre, the way a sticker is
 * never put on quite square.  `wear` is what hands have done to it since,
 * 0 nothing: stains, the same warm blotches the case carries but sized to
 * the sticker, and a few fine scratches that have lifted the ink and show
 * light - both turned with the sticker, since they are on it.  `rise` is
 * how far it stands off the case, in pixels, 0 for a print with no edge to
 * speak of: a vinyl with some body to it catches the key light along the
 * edge that faces it, falls into its own shade along the one turned away,
 * and casts a faint shadow past that onto the plastic.  `gloss` is the
 * sheen of the vinyl, 0 for matt paper: the key light laid softly over
 * the whole face, stronger toward the side it comes from, with a broad
 * band of reflection across the middle the way a glossy print shows the
 * window behind you. */
static void decal(canvas *c, const uint8_t *img, int iw, int ih, float x, float y, float w, float h,
                  float white, float fade, float tilt, float wear, float rise, float gloss) {
    float sx = w / (float)iw, sy = h / (float)ih;
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    float ang = tilt * 3.14159265f / 180.0f, ca = cosf(ang), sa = sinf(ang);
    /* the key light, turned into the sticker's frame */
    float lx = LIGHT_X * ca + LIGHT_Y * sa, ly = -LIGHT_X * sa + LIGHT_Y * ca;
    /* the scratches, laid out once in the sticker's own frame: where each
     * starts, which way it runs (mostly shallow), and how far */
    enum { SCRATCHES = 6 };
    float scr[SCRATCHES][5]; /* x0, y0, dx, dy, length */
    for (int n = 0; n < SCRATCHES; n++) {
        float a2 = (hash2(n, 61, 67) < 0.7f) ? (hash2(n, 71, 73) - 0.5f) * 0.8f
                                             : hash2(n, 71, 73) * 6.28318f;
        float len = (0.15f + 0.45f * hash2(n, 79, 83)) * w;
        scr[n][0] = (hash2(n, 89, 97) - 0.5f) * w * 0.9f;
        scr[n][1] = (hash2(n, 101, 103) - 0.5f) * h * 0.9f;
        scr[n][2] = cosf(a2);
        scr[n][3] = sinf(a2);
        scr[n][4] = len;
    }
    /* the turned sticker reaches this much further than its own box */
    float pad = 0.5f * (w * fabsf(sa) + h * fabsf(sa)) + 1.0f + rise * 2.0f;
    for (int py = (int)floorf(y - pad); py <= (int)ceilf(y + h + pad); py++)
        for (int px = (int)floorf(x - pad); px <= (int)ceilf(x + w + pad); px++) {
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (int ty = 0; ty < 4; ty++)
                for (int tx = 0; tx < 4; tx++) {
                    /* the sample, turned back into the sticker's own square
                     * frame: y runs down, so this undoes a clockwise turn */
                    float qx = (float)px + (tx + 0.5f) / 4.0f - cx;
                    float qy = (float)py + (ty + 0.5f) / 4.0f - cy;
                    float rx = qx * ca + qy * sa, ry = -qx * sa + qy * ca;
                    float u = (rx + w * 0.5f) / sx - 0.5f;
                    float v = (ry + h * 0.5f) / sy - 0.5f;
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
            /* the pixel's centre in the sticker's frame */
            float qx = (float)px + 0.5f - cx, qy = (float)py + 0.5f - cy;
            float rx = qx * ca + qy * sa, ry = -qx * sa + qy * ca;
            float edge_lit = 0.0f, edge_shade = 0.0f;
            if (rise > 0.0f) {
                /* what is under this pixel one step toward the light and
                 * one step away from it */
                float a_lit = cover(img, iw, ih, (rx + lx * rise + w * 0.5f) / sx - 0.5f,
                                    (ry + ly * rise + h * 0.5f) / sy - 0.5f);
                float a_shd = cover(img, iw, ih, (rx - lx * rise + w * 0.5f) / sx - 0.5f,
                                    (ry - ly * rise + h * 0.5f) / sy - 0.5f);
                /* The shadow it casts: tight under the edge, on the plastic
                 * the sticker does not cover, where the sticker is a step
                 * toward the light.  The step is one pixel whatever the
                 * thickness - less would put the shadow under the sticker's
                 * own edge, where it cannot be seen, and more would read as
                 * the sticker standing off the case.  Squaring the coverage
                 * keeps the shadow to that pixel rather than letting the
                 * soft edge of the artwork spread it into a second. */
                float step = 1.0f;
                float a_over = cover(img, iw, ih, (rx + lx * step + w * 0.5f) / sx - 0.5f,
                                     (ry + ly * step + h * 0.5f) / sy - 0.5f);
                float shadow = a_over * a_over * (1.0f - a) * 0.40f;
                if (shadow > 0.002f)
                    px_shade(c, px, py, 1.0f - shadow, 0.0f);
                edge_lit = fmaxf(0.0f, a - a_lit);
                edge_shade = fmaxf(0.0f, a - a_shd);
            }
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
            if (wear > 0.0f) {
                /* stains: blotches a fifth of the sticker across, over a
                 * finer mottle, taking more blue out than red */
                float u = rx / w * 5.0f, v = ry / w * 5.0f;
                float blot = vnoise(u, v, 51) * 0.65f + vnoise(u * 3.0f, v * 3.0f, 53) * 0.35f;
                float d = fmaxf(0.0f, blot - 0.50f) / 0.50f;
                d = (d * d * 0.16f + (vnoise(u * 7.0f, v * 7.0f, 57) - 0.5f) * 0.03f) * wear;
                r *= 1.0f - d * 0.80f;
                g *= 1.0f - d;
                b *= 1.0f - d * 1.35f;
                /* scratches: within a pixel of a line, the ink is gone and
                 * the pale stock shows, fading out toward either end */
                float show = 0.0f;
                for (int n = 0; n < SCRATCHES; n++) {
                    float ex = rx - scr[n][0], ey = ry - scr[n][1];
                    float t = ex * scr[n][2] + ey * scr[n][3];
                    if (t < 0.0f || t > scr[n][4])
                        continue;
                    float off = fabsf(-ex * scr[n][3] + ey * scr[n][2]);
                    if (off > 0.8f)
                        continue;
                    float e = sinf(t / scr[n][4] * 3.14159f) * (1.0f - off / 0.8f);
                    show = fmaxf(show, e * (0.10f + 0.12f * hash2(n, 107, 109)));
                }
                show *= wear;
                r += (235.0f - r) * show;
                g += (228.0f - g) * show;
                b += (214.0f - b) * show;
            }
            if (rise > 0.0f) {
                /* the edge: lit where it faces the light, a shade darker
                 * where it turns away, the vinyl's own thickness */
                r += (255.0f - r) * edge_lit * 0.45f;
                g += (255.0f - g) * edge_lit * 0.45f;
                b += (255.0f - b) * edge_lit * 0.45f;
                float dim = 1.0f - edge_shade * 0.30f;
                r *= dim;
                g *= dim;
                b *= dim;
            }
            if (gloss > 0.0f) {
                /* how far toward the light this point is, -1 at the edge
                 * away from it to +1 at the edge facing it */
                float toward = -(rx * lx + ry * ly) / (0.5f * w);
                float sheen = 0.08f + 0.05f * toward;
                /* the band: a soft reflection two fifths of the way toward
                 * the light, a quarter of the sticker wide */
                float band = (toward - 0.4f) / 0.25f;
                sheen += 0.12f * expf(-band * band);
                sheen *= gloss;
                r += (255.0f - r) * sheen;
                g += (255.0f - g) * sheen;
                b += (255.0f - b) * sheen;
            }
            if (a > 0.96f)
                a = 1.0f;
            px_blend(c, px, py, (int)r, (int)g, (int)b, a);
        }
}

/* The Horizon badge: the artwork in assets/horizon-sticker.png, the
 * maker's name, `w` wide at its own proportions and centred on (cx, cy),
 * a little washed out, the way thirty years of light leave a print, and
 * half a degree off square.  Not drawn at all if it would not fit in
 * maxw x maxh. */
#define STICKER_TILT 0.5f /* degrees, clockwise */
void horizon_sticker(canvas *c, float cx, float cy, float w, float maxw, float maxh) {
    float h = w * (float)DXM_HORIZON_HT / (float)DXM_HORIZON_W;
    if (w > maxw || h > maxh)
        return;
    decal(c, dxm_horizon, DXM_HORIZON_W, DXM_HORIZON_HT, cx - w * 0.5f, cy - h * 0.5f, w, h, 255.0f,
          0.30f, STICKER_TILT, 0.0f, 0.0f, 0.0f);
}

/* The 3dfx sticker: the artwork in assets/3dfx-sticker.png, the one that
 * came in the box with the card and went straight on the case, `w` wide
 * at its own proportions and centred on (cx, cy).  Faded less than the
 * badge - a vinyl keeps its colour where paper loses it - turned the other
 * way from it - two stickers put on by hand do not lean
 * together - and, being where a hand rests, lightly stained and scratched.
 * A glossy vinyl rather than a print, `rise` pixels thick.  Not drawn at
 * all if it would not fit in maxw x maxh - the ink, that is: the artwork
 * sits in its file with clear margin round it, and a margin does not need
 * room on the case. */
#define TDFX_TILT -0.7f /* degrees, clockwise: so anticlockwise */
/* where the ink is in the file, as a share of its width and height */
#define TDFX_INK_W (229.0f / 300.0f)
#define TDFX_INK_H (214.0f / 257.0f)
void tdfx_sticker(canvas *c, float cx, float cy, float w, float rise, float maxw, float maxh) {
    float h = w * (float)DXM_TDFX_HT / (float)DXM_TDFX_W;
    if (w * TDFX_INK_W > maxw || h * TDFX_INK_H > maxh)
        return;
    decal(c, dxm_tdfx, DXM_TDFX_W, DXM_TDFX_HT, cx - w * 0.5f, cy - h * 0.5f, w, h, 255.0f, 0.12f,
          TDFX_TILT, 1.0f, rise, 1.0f);
}
