/* marks.c — what is printed, stuck or engraved on the case: the badge,
 * the sound-card sticker, the maker's mark and the corner engraving. */
#include "internal.h"
#include "font.h"
#include "gen/sb_logo.h"
#include "gen/mark.h"
#include "gen/corner_sticker.h"
#include "gen/road.h"

/* The sound-card sticker.  Everything else on this machine was moulded or
 * printed at the factory; this is the one mark a PREVIOUS OWNER left, so it
 * is applied ON the case - clear laminate margin, contact shadow round the
 * edge, and the gloss catch vinyl has and plastic does not.
 *
 * The artwork is the real logo, baked (tools/mklogo.py).  Setting it in the
 * 8x8 case font got the words right and everything else wrong: the mark has
 * letterforms of its own - the triangular A over its rule, the notched E -
 * and faking those is exactly the sort of thing that reads as a cartoon.
 *
 * It is die-cut to the artwork, with no laminate margin around it, so the
 * corner radius has to come off the print itself. */
void sb_sticker(canvas *c, float cx, float cy, float w) {
    float lw = w; /* the print IS the label */
    float lh = lw * (float)SB_LOGO_HT / (float)SB_LOGO_W;
    float h = lh;
    float rad = h * 0.10f, hw = w * 0.5f, hh = h * 0.5f;
    /* Applied by hand, so not quite square: a degree clockwise.  Everything
     * below works in the label's own frame - a canvas pixel is turned back
     * through that angle and asked where on the label it falls. */
    const float ang = 1.0f * 3.14159265f / 180.0f;
    const float ca = cosf(ang), sa = sinf(ang);
    float sw = h * 0.13f;                               /* shadow reach */
    float reach = sqrtf(hw * hw + hh * hh) + sw + 2.0f; /* covers the turned corners */
    int j0 = (int)(cy - reach), j1 = (int)(cy + reach) + 1;
    int i0 = (int)(cx - reach), i1 = (int)(cx + reach) + 1;
    float sx = (float)SB_LOGO_W / lw, sy = (float)SB_LOGO_HT / lh;
    canvas_grain = 0; /* printed vinyl has no grain */

    /* the shadow it casts on the pod, which is what puts it on top */
    for (int j2 = j0; j2 < j1; j2++)
        for (int i2 = i0; i2 < i1; i2++) {
            float dx = (float)i2 - cx, dy = (float)j2 - cy;
            float lx = dx * ca + dy * sa, ly = -dx * sa + dy * ca;
            float sd = rr_sd(lx, ly, 0.0f, 0.0f, hw, hh, rad);
            if (sd <= 0.0f || sd > sw)
                continue;
            float t = sd / sw, dyn = ly / hh;
            /* deeper below, away from the key light */
            float side = 0.55f + 0.60f * fmaxf(0.0f, dyn);
            px_shade(c, i2, j2, 1.0f - 0.17f * (1.0f - t) * (1.0f - t) * side, 0.0f);
        }

    /* the artwork, box-filtered down to whatever size it landed at, with
     * the die-cut edge anti-aliased off the same distance the shadow uses */
    for (int j2 = j0; j2 < j1; j2++)
        for (int i2 = i0; i2 < i1; i2++) {
            float dx = (float)i2 - cx, dy = (float)j2 - cy;
            float lx = dx * ca + dy * sa, ly = -dx * sa + dy * ca;
            float cov = 0.5f - rr_sd(lx, ly, 0.0f, 0.0f, hw, hh, rad);
            if (cov <= 0.0f)
                continue;
            if (cov > 1.0f)
                cov = 1.0f;
            float uc = (lx + hw) * sx, vc = (ly + hh) * sy; /* source texel footprint */
            int u0 = (int)floorf(uc - sx * 0.5f), u1 = (int)ceilf(uc + sx * 0.5f);
            int v0 = (int)floorf(vc - sy * 0.5f), v1 = (int)ceilf(vc + sy * 0.5f);
            if (u0 < 0)
                u0 = 0;
            if (v0 < 0)
                v0 = 0;
            if (u1 > SB_LOGO_W)
                u1 = SB_LOGO_W;
            if (v1 > SB_LOGO_HT)
                v1 = SB_LOGO_HT;
            if (u1 <= u0)
                u1 = u0 + 1;
            if (v1 <= v0)
                v1 = v0 + 1;
            if (u0 >= SB_LOGO_W || v0 >= SB_LOGO_HT)
                continue;
            int r = 0, g = 0, b = 0, n = 0;
            for (int v = v0; v < v1; v++)
                for (int u = u0; u < u1; u++) {
                    const uint8_t *sp = sb_logo + ((size_t)v * SB_LOGO_W + u) * 4;
                    r += sp[0];
                    g += sp[1];
                    b += sp[2];
                    n++;
                }
            if (!n)
                continue;
            /* A label that has sat on a warm case for thirty years is not
             * the print file any more.  Two corrections:
             *
             * The black.  Print black on vinyl is not 0,0,0 - nothing on
             * this machine is - and the artwork comes off an SVG where it
             * is.  Lift the black point onto the same dark blue-grey the
             * 486 badge is printed in, as a levels move rather than a flat
             * add, so white stays white and everything between scales.
             *
             * Then desaturate a touch and take the top off the brightness. */
            {
                const float BK_R = 0x22, BK_G = 0x26, BK_B = 0x30;
                float R = BK_R + (float)r / n * (255.0f - BK_R) / 255.0f;
                float G = BK_G + (float)g / n * (255.0f - BK_G) / 255.0f;
                float B = BK_B + (float)b / n * (255.0f - BK_B) / 255.0f;
                float lum = 0.299f * R + 0.587f * G + 0.114f * B;
                const float DULL = 0.15f, FADE = 0.95f;
                R = (R + (lum - R) * DULL) * FADE;
                G = (G + (lum - G) * DULL) * FADE;
                B = (B + (lum - B) * DULL) * FADE;
                px_blend(c, i2, j2, (int)R, (int)G, (int)B, cov);
            }
        }

    /* Vinyl is glossy, but not new vinyl-glossy: a worn label scatters, so
     * the catch is broader and weaker.  Narrowing and brightening it is
     * what would put it back to looking freshly applied. */
    for (int j2 = j0; j2 < j1; j2++)
        for (int i2 = i0; i2 < i1; i2++) {
            float dx = (float)i2 - cx, dy = (float)j2 - cy;
            float lx = dx * ca + dy * sa, ly = -dx * sa + dy * ca;
            if (rr_sd(lx, ly, 0.0f, 0.0f, hw, hh, rad) > 0.0f)
                continue;
            float u = (lx + hw) / w + ((ly + hh) / h) * 0.50f;
            float d = (u - 0.40f) / 0.28f;
            float a = expf(-d * d) * 0.085f;
            if (a > 0.004f)
                px_blend(c, i2, j2, 255, 255, 255, a);
        }
    canvas_grain = 1;
}

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

/* The mark on the speaker pod, cut INTO the plastic rather than stuck on
 * it.  An engraving changes no colour at all - the material is the same
 * material at the bottom of the cut as on the face - so this pass only ever
 * shades what is already there.  Painting the mark in and then shading it
 * is what would give the game away.
 *
 * The artwork is baked from assets/corner-sticker.png (tools/mklogo.py) and
 * its alpha is read as DEPTH.  Shading comes from that depth field's
 * gradient: the wall you meet first is turned away from the key light and
 * falls dark, the far wall is turned into it and catches a highlight, and
 * the floor between them sits in its own shade.  A fixed light-above /
 * dark-below pair - which is fine for lettering - would be wrong here,
 * because the mark is all circles and the walls face every direction. */
/* Cut a depth field into the plastic.  dep is dw x dh, 0 = untouched
 * surface, 1 = full depth; its top-left lands at (x,y).  Shading comes from
 * the field's gradient - see corner_engraving() for why - so this serves
 * any mark whose walls face every direction. */
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

/* One texel of the road artwork, premultiplied; transparent outside it. */
static void road_texel(int i, int j, float out[4]) {
    if (i < 0 || j < 0 || i >= DXM_ROAD_W || j >= DXM_ROAD_HT) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return;
    }
    const uint8_t *p = dxm_road + ((size_t)j * DXM_ROAD_W + i) * 4;
    float a = p[3] / 255.0f;
    out[0] = p[0] * a;
    out[1] = p[1] * a;
    out[2] = p[2] * a;
    out[3] = a;
}

/* The road mark: the artwork alone, fitted into maxw x maxh with its right
 * edge at `right` and centred on cy.  Each pixel takes sixteen bilinear
 * taps, so it holds up scaled up or down. */
static void road_decal(canvas *c, float right, float cy, float maxw, float maxh) {
    float sc = fminf(maxw / (float)DXM_ROAD_W, maxh / (float)DXM_ROAD_HT);
    float w = DXM_ROAD_W * sc, h = DXM_ROAD_HT * sc;
    float ox = right - w, oy = cy - h * 0.5f;
    for (int y = (int)floorf(oy); y <= (int)ceilf(oy + h); y++)
        for (int x = (int)floorf(ox); x <= (int)ceilf(ox + w); x++) {
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float u = ((float)x + (sx + 0.5f) / 4.0f - ox) / sc - 0.5f;
                    float v = ((float)y + (sy + 0.5f) / 4.0f - oy) / sc - 0.5f;
                    int i = (int)floorf(u), j = (int)floorf(v);
                    float fu = u - (float)i, fv = v - (float)j, t[4][4];
                    road_texel(i, j, t[0]);
                    road_texel(i + 1, j, t[1]);
                    road_texel(i, j + 1, t[2]);
                    road_texel(i + 1, j + 1, t[3]);
                    for (int k = 0; k < 4; k++)
                        acc[k] += (t[0][k] * (1.0f - fu) + t[1][k] * fu) * (1.0f - fv) +
                                  (t[2][k] * (1.0f - fu) + t[3][k] * fu) * fv;
                }
            float a = acc[3] / 16.0f;
            if (a <= 0.004f)
                continue;
            px_blend(c, x, y, (int)(acc[0] / acc[3]), (int)(acc[1] / acc[3]),
                     (int)(acc[2] / acc[3]), a);
        }
}

/* The first and last inked columns of a glyph of the 8x8 font; 8 and -1
 * for a blank one. */
static void ink_cols(int ch, int *first, int *last) {
    const uint8_t *g = font_glyph(ch);
    *first = 8;
    *last = -1;
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 8; i++)
            if (g[j] & (0x80 >> i)) {
                if (i < *first)
                    *first = i;
                if (i > *last)
                    *last = i;
            }
}

/* The badge, drawn with its RIGHT edge at `right`: the road mark on the
 * left, then "PC-486" over "MULTIMEDIA" with the four colour lines under
 * them, starting on the same left edge and dissolving toward the right.
 * As wide as those need: the road, a gap, the name measured by the font's
 * ink, and a margin.  ref_w is the original label's width, which sets every
 * scale here.  Returns the left edge. */
float badge(canvas *c, float right, float pby, float ref_w, float pbh) {
    static const char *const NAME = "PC-486";
    float sc = fminf(fmaxf(1.0f, ref_w / 110.0f), pbh / 30.0f);
    float s1 = sc * 1.35f; /* the name's scale, as text() lays it out */
    int f0, l0, f1, l1, n = (int)strlen(NAME);
    ink_cols((unsigned char)NAME[0], &f0, &l0);
    ink_cols((unsigned char)NAME[n - 1], &f1, &l1);
    float ink_l = (float)f0 * s1, ink_r = (float)((n - 1) * 8 + l1) * s1 + floorf(s1);
    float rs = fminf(ref_w * 0.28f / (float)DXM_ROAD_W, pbh * 0.66f / (float)DXM_ROAD_HT);
    float rw = DXM_ROAD_W * rs, rh = DXM_ROAD_HT * rs;
    float ml = ref_w * 0.05f, gap = ref_w * 0.05f, mr = ref_w * 0.065f;
    float pbw = ml + rw + gap + (ink_r - ink_l) + mr;
    float pbx = right - pbw;
    float rx = pbx + ml, ry = pby + (pbh - rh) * 0.5f;
    float tx = rx + rw + gap - ink_l; /* where the name's cell starts */
    canvas_grain = 0;                 /* the badge is a printed label */
    rrect(c, pbx, pby, pbw, pbh, pbh * 0.10f, 0x22, 0x26, 0x30, 1.0f, 0.82f);
    housing_edge(c, pbx, pby, pbw, pbh, pbh * 0.10f, fmaxf(2.0f, pbh * 0.09f),
                 fmaxf(2.0f, pbh * 0.08f), 0, 1.0f);
    text(c, tx, pby + pbh * 0.16f, NAME, s1, 0xEC, 0xE8, 0xDC);
    /* the small print, smoothed: the blocky renderer cannot go below a
     * pixel per font pixel, and on a small display that made this line
     * wider than the name above it */
    text_smooth(c, tx, pby + pbh * 0.56f, "MULTIMEDIA", sc * 0.60f, 0xA8, 0xB0, 0xC6);
    {
        int cols[4][3] = {
            {0x2E, 0x4C, 0xA8}, {0x2E, 0x8C, 0x50}, {0xC8, 0x9A, 0x28}, {0xB8, 0x3C, 0x34}};
        /* the lines start where the lettering does, straight down */
        float x0 = rx + rw + gap, x1 = pbx + pbw - mr;
        for (int k = 0; k < 4; k++) {
            float ly0 = pby + pbh * (0.78f + k * 0.048f), lh = fmaxf(1.0f, pbh * 0.030f);
            for (int yy = (int)floorf(ly0); yy <= (int)ceilf(ly0 + lh); yy++) {
                float cy = (float)yy + 0.5f;
                float ay = fminf(1.0f, fminf(cy - ly0 + 0.5f, ly0 + lh - cy + 0.5f));
                if (ay <= 0.0f)
                    continue;
                for (int xx = (int)floorf(x0); xx <= (int)ceilf(x1); xx++) {
                    float cx = (float)xx + 0.5f;
                    float ax = fminf(1.0f, fminf(cx - x0 + 0.5f, x1 - cx + 0.5f));
                    if (ax <= 0.0f)
                        continue;
                    /* the ink runs out rightward: the colour is constant and
                     * the coverage falls to nothing, so the line dissolves
                     * into the label rather than turning white */
                    float m = fminf(1.0f, fmaxf(0.0f, ((cx - x0) / (x1 - x0) - 0.40f) / 0.60f));
                    m = m * m * (3.0f - 2.0f * m);
                    px_blend(c, xx, yy, cols[k][0], cols[k][1], cols[k][2], ay * ax * (1.0f - m));
                }
            }
        }
    }
    road_decal(c, rx + rw, ry + rh * 0.5f, rw, rh);
    canvas_grain = 1;
    return pbx;
}
