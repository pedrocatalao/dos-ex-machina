/* parts.c — the moulded parts: LEDs, the power button, vents, the
 * speaker grilles and the floppy drive. */
#include "internal.h"
#include "segdisp.h"

static void led(canvas *c, float cx, float cy, float rad, int r, int g, int b) {
    /* Matched to the reference PNG's LEDs at 16x magnification:
     *  - a THIN dark outline hugging the lens (heavier at the top), not a
     *    wide mounting ring
     *  - a saturated, fairly uniform body, deepening toward lower-right
     *  - a soft whitish hot spot in the upper-left quarter
     *  - NO glow halo on the surrounding plastic                        */
    for (int j2 = (int)(cy - rad * 1.75f); j2 <= (int)(cy + rad * 1.75f); j2++)
        for (int i2 = (int)(cx - rad * 1.75f); i2 <= (int)(cx + rad * 1.75f); i2++) {
            float dx = (i2 - cx) / rad, dy = (j2 - cy) / rad;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > 1.72f)
                continue;
            if (d > 0.94f) {
                /* The lens sits in a HOLE, and the hole is what reads 3D:
                 * dark ring, a soft shadow on the plastic above it, and a
                 * lit chamfer lip on the plastic below it.  The hole is a
                 * proper moulded well - the ring is wide and the chamfer
                 * around it reaches well out into the plastic - rather than
                 * a hairline, which read as a lens glued on the surface. */
                if (d <= 1.30f) { /* the ring itself */
                    float a = 1.0f - fmaxf(0.0f, (d - 1.14f) / 0.16f);
                    a *= fminf(1.0f, (d - 0.90f) / 0.06f);
                    float top = (dy < 0.0f) ? 1.0f : 0.66f;
                    px_blend(c, i2, j2, 26, 22, 14, a * 0.90f * top);
                } else {
                    float t = (d - 1.30f) / 0.42f; /* 0 at ring, 1 outside */
                    if (dy < -0.15f)               /* shadow above the hole */
                        px_shade(c, i2, j2, 1.0f - 0.22f * (1.0f - t) * (-dy / d), 0.0f);
                    else if (dy > 0.15f) /* lit lip below it */
                        px_shade(c, i2, j2, 1.0f + 0.26f * (1.0f - t) * (dy / d),
                                 (1.0f - t) * (dy / d) * 0.08f);
                }
                continue;
            }
            /* body: uniform, deepening to lower-right */
            float f = 0.94f - 0.22f * fmaxf(0.0f, (dx + dy) * 0.5f);
            if (d > 0.72f)
                f *= 1.0f - (d - 0.72f) / 0.28f * 0.30f;
            /* hot spot up-left: soft gaussian, whitening not just brightening */
            float hx = (dx + 0.30f) / 0.42f, hy = (dy + 0.32f) / 0.42f;
            float w2 = expf(-(hx * hx + hy * hy));
            int rr = (int)(r * f + (255 - r * f) * w2);
            int gg = (int)(g * f + (255 - g * f) * w2);
            int bb = (int)(b * f + (255 - b * f) * w2 * 0.9f);
            px_blend(c, i2, j2, rr, gg, bb, 1.0f);
        }
}

/* Rectangular LED window.  A drive-activity light is a FLAT-FRONTED light
 * pipe, not a bead: the face is a plane, the corners are barely eased, and
 * the plastic is frosted so it scatters rather than reflecting a highlight.
 * The generous corner radius and the gaussian hot spot this replaces are
 * exactly what made it read as a little round lens. */
static void led_rect(canvas *c, float cx, float cy, float w, float h, int r, int g, int b) {
    float hw = w * 0.5f, hh = h * 0.5f;
    float rad = h * 0.15f; /* eased, not rounded */
    float ring = fmaxf(1.2f, h * 0.16f);
    float pitch = fmaxf(1.6f, h * 0.17f); /* diffuser striation pitch */
    for (int j2 = (int)(cy - hh - ring * 3); j2 <= (int)(cy + hh + ring * 3); j2++)
        for (int i2 = (int)(cx - hw - ring * 3); i2 <= (int)(cx + hw + ring * 3); i2++) {
            float sd = rr_sd((float)i2, (float)j2, cx, cy, hw, hh, rad);
            float dy = (j2 - cy) / hh;
            if (sd <= 0.0f) {
                float e = -sd / h; /* depth in from the frame */
                /* A plane takes an even wash.  Only the very bottom falls
                 * off, where the frame shades it. */
                float f = 0.99f - 0.12f * fmaxf(0.0f, dy);
                if (e < 0.16f)
                    f *= 0.66f + 2.10f * e; /* the moulded frame */
                /* frosted plastic: fine grain, plus the horizontal tool
                 * striations a moulded pipe carries */
                f += (hash2(i2, j2, 7) - 0.5f) * 0.14f;
                f += sinf((float)j2 * 3.14159265f / pitch) * 0.045f;
                /* the bevel along the top of the face catches a thin line -
                 * a flat front has an EDGE, which is what sells it as flat */
                float bev = (e < 0.11f && dy < 0.0f) ? 0.17f * (1.0f - e / 0.11f) : 0.0f;
                px_blend(c, i2, j2, (int)(r * f + 255.0f * bev), (int)(g * f + 255.0f * bev),
                         (int)(b * f + 255.0f * bev), 1.0f);
            } else if (sd <= ring) {
                float a = 1.0f - sd / ring;
                float top = (dy < 0.0f) ? 1.0f : 0.66f;
                px_blend(c, i2, j2, 26, 22, 14, a * 0.85f * top);
            } else if (sd <= ring * 3.0f) {
                float t = (sd - ring) / (ring * 2.0f);
                if (dy < -0.15f)
                    px_shade(c, i2, j2, 1.0f - 0.15f * (1.0f - t), 0.0f);
                else if (dy > 0.15f)
                    px_shade(c, i2, j2, 1.0f + 0.18f * (1.0f - t), (1.0f - t) * 0.05f);
            }
        }
}

/* The IEC power mark - a broken ring with a bar through the break - cut
 * into the cap.  Built as a distance field rather than baked, since it is
 * two strokes and should stay crisp at any size. */
static void power_symbol(canvas *c, float cx, float cy, float size) {
    int dw = (int)(size + 4.0f), dh = dw;
    if (dw < 6)
        return;
    float *dep = calloc((size_t)dw * dh, sizeof *dep);
    if (!dep)
        return;
    float R = size * 0.36f, t = size * 0.085f;           /* ring radius, stroke width */
    float ox = dw * 0.5f, oy = dh * 0.5f + size * 0.03f; /* the bar sits high; centre low */
    for (int j = 0; j < dh; j++)
        for (int i = 0; i < dw; i++) {
            float x = (float)i + 0.5f - ox, y = (float)j + 0.5f - oy;
            float r = sqrtf(x * x + y * y);
            /* the ring, minus a 76-degree gap centred on straight up */
            float sd = fabsf(r - R) - t * 0.5f;
            float ang = atan2f(x, -y);      /* 0 = up, grows clockwise */
            float gap = 0.66f - fabsf(ang); /* >0 inside the gap */
            if (gap > 0.0f) {
                /* end caps: distance to the two rounded stroke ends */
                float ex1 = R * sinf(0.66f), ey1 = -R * cosf(0.66f);
                float d1 = sqrtf((x - ex1) * (x - ex1) + (y - ey1) * (y - ey1));
                float d2 = sqrtf((x + ex1) * (x + ex1) + (y - ey1) * (y - ey1));
                sd = fminf(d1, d2) - t * 0.5f;
            }
            /* the bar: from the ring's top down to just past the centre */
            float bx = fabsf(x) - t * 0.5f, by = fmaxf(-(R + t * 0.5f) - y, y - size * 0.04f);
            float bsd = fmaxf(bx, by);
            sd = fminf(sd, bsd);
            float cov = 0.5f - sd;
            if (cov < 0.0f)
                cov = 0.0f;
            if (cov > 1.0f)
                cov = 1.0f;
            dep[(size_t)j * dw + i] = cov;
        }
    engrave_field(c, cx - ox, cy - oy, dw, dh, dep);
    free(dep);
}

/* A vent cut: a through-hole, so what shows in it is the dark inside of the
 * machine rather than a shaded groove.  Written to work at any aspect - the
 * shading is driven by the cut's NARROW dimension, not by its length, so a
 * tall slot gets the same short overhang shadow at its top that a wide one
 * gets, instead of having its whole upper half in shade. */
void vent_slot(canvas *c, float x, float y, float w, float h) {
    vent_slot_r(c, x, y, w, h, fminf(w, h) * 0.46f, 1.0f, 1.0f); /* the ends are rounded */
}

/* The same slot with its own corner radius (0 is a square-ended cut), how
 * deep it reads (1 = the foot vent's black trough, less = a shallower cut
 * whose floor takes light) and how strongly its rims are modelled. */
void vent_slot_r(canvas *c, float x, float y, float w, float h, float rad, float deep, float rim) {
    float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
    float th = fminf(w, h); /* the narrow dimension */
    float lip = fmaxf(1.2f, th * 0.70f);
    for (int j2 = (int)(y - lip - 1); j2 <= (int)(y + h + lip + 1); j2++)
        for (int i2 = (int)(x - lip - 1); i2 <= (int)(x + w + lip + 1); i2++) {
            float sd = rr_sd((float)i2, (float)j2, cx, cy, hw, hh, rad);
            if (sd <= 0.5f) {
                float cov = fminf(1.0f, 0.5f - sd);
                /* the overhang at the top reaches about two widths in */
                float dt = ((float)j2 + 0.5f - y) / fmaxf(th * 2.2f, 1.0f);
                float v = 0.19f + 0.20f * fminf(1.0f, dt);
                /* the wall away from the light takes a little of it */
                v += 0.11f * (((float)i2 + 0.5f) - x) / fmaxf(w, 1.0f);
                /* and the bottom end lifts, where light gets past the lip */
                float db = ((y + h) - ((float)j2 + 0.5f)) / fmaxf(th * 1.8f, 1.0f);
                if (db < 1.0f)
                    v += 0.15f * (1.0f - db);
                v = 1.0f + (v - 1.0f) * deep;
                px_shade(c, i2, j2, 1.0f + (v - 1.0f) * cov, 0.0f);
            } else if (sd < lip) {
                /* Which rim catches the light is a question about the NORMAL,
                 * not about being above or below centre - get it from the
                 * field's gradient and the same answer serves either aspect. */
                float gx = rr_sd((float)i2 + 1, (float)j2, cx, cy, hw, hh, rad) -
                           rr_sd((float)i2 - 1, (float)j2, cx, cy, hw, hh, rad);
                float gy = rr_sd((float)i2, (float)j2 + 1, cx, cy, hw, hh, rad) -
                           rr_sd((float)i2, (float)j2 - 1, cx, cy, hw, hh, rad);
                float gl = sqrtf(gx * gx + gy * gy);
                if (gl < 1e-4f)
                    continue;
                float lam = (gx * LIGHT_X + gy * LIGHT_Y) / gl; /* outward . light */
                float e = 1.0f - sd / lip;
                e *= e;
                px_shade(c, i2, j2, 1.0f - lam * 0.17f * rim * e,
                         fmaxf(-lam, 0.0f) * e * 0.05f * rim);
            }
        }
}

/* A louvre: a horizontal slot in the shell with a slat behind it.  What
 * reads as the OPENING is the black under the upper edge, where the
 * overhang shadows the hole; below that the slat's top face shows through,
 * angled at the light and brightening toward the lower lip until it is
 * lighter than the case around it.  The rims are modelled from the
 * distance field's normal, as the vent's are, only harder. */
void louvre_slot(canvas *c, float x, float y, float w, float h, float rad) {
    float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
    float lip = fmaxf(1.5f, h * 0.75f);
    for (int j2 = (int)(y - lip - 1); j2 <= (int)(y + h + lip + 1); j2++)
        for (int i2 = (int)(x - lip - 1); i2 <= (int)(x + w + lip + 1); i2++) {
            float sd = rr_sd((float)i2, (float)j2, cx, cy, hw, hh, rad);
            if (sd <= 0.5f) {
                float cov = fminf(1.0f, 0.5f - sd);
                float t = ((float)j2 + 0.5f - y) / h; /* 0 top .. 1 bottom */
                float v;
                if (t < 0.50f)
                    v = 0.10f + 0.14f * (t / 0.50f); /* the hole, in the overhang's shadow */
                else {
                    float u = (t - 0.50f) / 0.50f; /* the slat's face, lit */
                    v = 0.34f + 0.90f * u * u;
                }
                /* the end wall inside the cut, turned from the light */
                float de = fminf((float)i2 + 0.5f - x, x + w - ((float)i2 + 0.5f)) / fmaxf(h, 1.0f);
                if (de < 0.8f)
                    v *= 0.55f + 0.45f * (de / 0.8f);
                px_shade(c, i2, j2, 1.0f + (v - 1.0f) * cov, 0.0f);
            } else if (sd < lip) {
                float gx = rr_sd((float)i2 + 1, (float)j2, cx, cy, hw, hh, rad) -
                           rr_sd((float)i2 - 1, (float)j2, cx, cy, hw, hh, rad);
                float gy = rr_sd((float)i2, (float)j2 + 1, cx, cy, hw, hh, rad) -
                           rr_sd((float)i2, (float)j2 - 1, cx, cy, hw, hh, rad);
                float gl = sqrtf(gx * gx + gy * gy);
                if (gl < 1e-4f)
                    continue;
                float lam = (gx * LIGHT_X + gy * LIGHT_Y) / gl;
                float e = 1.0f - sd / lip;
                e *= e;
                px_shade(c, i2, j2, 1.0f - lam * 0.30f * e, fmaxf(-lam, 0.0f) * e * 0.09f);
            }
        }
}

/* ---- moulded modules ---- */

/* A grille is a recessed WELL with a moulded lip, and slots inside it with
 * real cross-section: dark trough, lit lower lip, shadowed upper lip. */
void grille_panel(canvas *c, float x, float y, float w, float h, float pitch) {
    /* Perforated speaker area: a hex-packed grid of small punched holes
     * confined to a capsule-shaped zone.  The case face stays flush and
     * plain; each hole gets the full treatment - dark interior slightly
     * lighter at its bottom, an overhang shadow inside the top edge, a
     * faint lit lip on the plastic below, soft anti-aliased rim. */
    float ccx = x + w * 0.5f, ccy = y + h * 0.5f;
    float hw = w * 0.40f, hh = h * 0.44f;
    float crad = fminf(hw, hh) * 0.16f;    /* rectangular, corners just eased */
    float hp = fmaxf(2.0f, pitch * 0.21f); /* hole pitch  */
    float hr = hp * 0.25f;                 /* hole radius */
    /* The grid is CENTRED on the panel, not run left-to-right until it
     * runs out - otherwise the leftover margin differs from the starting
     * one and each grille comes out lopsided.  Rows alternate between n and
     * n-1 columns, which gives the hex offset while keeping every row
     * symmetric about the centre. */
    float rowstep = hp * 0.87f;
    int nrow = (int)floorf((hh * 2.0f) / rowstep);
    int ncol = (int)floorf((hw * 2.0f) / hp);
    if (nrow < 1)
        nrow = 1;
    if (ncol < 1)
        ncol = 1;
    for (int row = 0; row < nrow; row++) {
        float cy2 = ccy + (row - (nrow - 1) * 0.5f) * rowstep;
        int n = (row & 1) ? ncol - 1 : ncol;
        for (int ci = 0; ci < n; ci++) {
            float cx2 = ccx + (ci - (n - 1) * 0.5f) * hp;
            /* only holes fully inside the capsule */
            if (rr_sd(cx2, cy2, ccx, ccy, hw, hh, crad) > -hr * 1.2f)
                continue;
            /* At this pitch a hole spans only a few pixels, so shade it by
             * ANALYTIC COVERAGE rather than discrete zones - otherwise the
             * rim aliases and the grid reads as ragged dots. */
            for (int j2 = (int)(cy2 - hr - 2); j2 <= (int)(cy2 + hr + 2); j2++)
                for (int i2 = (int)(cx2 - hr - 2); i2 <= (int)(cx2 + hr + 2); i2++) {
                    float dxp = i2 + 0.5f - cx2, dyp = j2 + 0.5f - cy2;
                    float d = sqrtf(dxp * dxp + dyp * dyp);
                    float cov = hr - d + 0.5f; /* pixel coverage */
                    if (cov <= 0.0f) {
                        /* just outside: a whisper of lit lip below the hole */
                        if (cov > -1.2f && dyp > 0.0f)
                            px_shade(c, i2, j2,
                                     1.0f + 0.10f * (1.2f + cov) / 1.2f * (dyp / fmaxf(d, 0.001f)),
                                     0.0f);
                        continue;
                    }
                    if (cov > 1.0f)
                        cov = 1.0f;
                    float u = dyp / fmaxf(hr, 0.001f);        /* -1 top .. +1 bottom */
                    float v = 15.0f + 11.0f * fmaxf(0.0f, u); /* floor lit at bottom */
                    if (u < -0.15f)
                        v *= 0.60f; /* overhang shadow     */
                    px_blend(c, i2, j2, (int)v, (int)v, (int)(v * 1.04f), cov);
                }
        }
    }
}

/* 3.5" drive: same geometry as before (recessed plate, finger cuts, slot
 * through them, protruding eject, inserted diskette), but every edge is now
 * an eased ramp at the LED's fidelity - no hard 1px lines anywhere. */
void floppy_drive(canvas *c, float x, float y, float w, float h, float led_out[4]) {
    float mm = h / 25.4f;
    (void)w;
    float fw = 101.6f * mm;
    /* the drive is a separate moulding: darker and greyer than the case */
    int pr = (int)(PLASTIC_R * 0.74f), pg = (int)(PLASTIC_G * 0.72f), pb = (int)(PLASTIC_B * 0.76f);
    /* chassis cut-out: chamfered case edge, thin gap, recessed plate */
    rrect(c, x - 0.5f * mm, y - 0.5f * mm, fw + 1.0f * mm, h + 1.0f * mm, 1.5f * mm,
          (int)(PLASTIC_R * 0.38f), (int)(PLASTIC_G * 0.38f), (int)(PLASTIC_B * 0.38f), 0.92f,
          1.04f);
    chamfer_ring(c, x - 0.5f * mm, y - 0.5f * mm, fw + 1.0f * mm, h + 1.0f * mm, 1.5f * mm,
                 0.5f * mm);
    rrect(c, x, y, fw, h, 1.3f * mm, pr, pg, pb, 0.97f, 1.01f);
    housing_edge(c, x, y, fw, h, 1.3f * mm, 1.2f * mm, 0.0f, 0, 1.3f);

    float sly = y + 8.6f * mm, slh = 4.8f * mm;
    float slx = x + 5.0f * mm, slw = fw - 10.0f * mm;
    float tcw = 36.0f * mm, tcx = x + (fw - tcw) * 0.5f, tcy0 = y + 3.4f * mm;
    float bcw = 41.0f * mm, bcx = x + (fw - bcw) * 0.5f, bcy1 = y + 21.2f * mm;
    chamfer_ring(c, tcx, tcy0, tcw, sly - tcy0, 1.6f * mm, 0.9f * mm);
    chamfer_ring(c, bcx, sly + slh, bcw, bcy1 - (sly + slh), 1.6f * mm, 0.9f * mm);

    /* finger cuts: floors with smooth gradients, eased walls */
    struct {
        float cx, cw, y0, y1;
    } cut[2] = {
        {tcx, tcw, tcy0, sly},
        {bcx, bcw, sly + slh, bcy1},
    };
    for (int k = 0; k < 2; k++) {
        float cx0 = cut[k].cx, cw0 = cut[k].cw, y0 = cut[k].y0, y1 = cut[k].y1;
        float ch0 = y1 - y0, rr = 1.6f * mm;
        if (k == 0) {
            /* With a diskette inserted the spring door has swung INWARD, so
             * the top cut is an empty hole into the drive - not a moulded
             * floor.  Near-black, lifting a little at the bottom where light
             * from the room reaches in past the lip. */
            rrect(c, cx0, y0, cw0, ch0 + rr, rr, 17, 17, 19, 0.55f, 1.55f);
        } else
            rrect(c, cx0, y0 - rr, cw0, ch0 + rr, rr, pr, pg, pb, 0.46f, 0.84f);
        /* eased side walls (the hole needs no lit walls - it is open) */
        if (k == 1) {
            soft_vedge(c, y0 + rr * 0.4f, y1 - rr * 0.4f, cx0 + 0.2f * mm, 1.1f * mm, 0.62f, 1);
            soft_vedge(c, y0 + rr * 0.4f, y1 - rr * 0.4f, cx0 + cw0 - 0.2f * mm, 1.1f * mm, 0.72f,
                       0);
        }
        /* eased overhang shadow at the top of the cut - deeper on the hole */
        soft_hedge(c, cx0, cx0 + cw0, y0, (k == 0) ? 2.4f * mm : 1.8f * mm,
                   (k == 0) ? 0.22f : 0.42f, 0.0f, 1);
        /* lit lip at the bottom inner wall; on the hole this is the front
         * face catching light at the opening's edge, so it stays subtle */
        soft_hedge(c, cx0 + 0.8f * mm, cx0 + cw0 - 0.8f * mm, y1 - 1, 1.3f * mm,
                   (k == 1) ? 1.34f : 1.12f, (k == 1) ? 0.05f : 0.0f, 0);
    }

    /* slot: chamfered opening, eased interior faces */
    chamfer_ring(c, slx, sly, slw, slh, slh * 0.24f, 1.2f * mm);
    rrect(c, slx, sly, slw, slh, slh * 0.24f, 12, 11, 10, 1.0f, 1.0f);
    {
        float ch1 = 1.4f * mm;
        for (int j2 = 0; j2 < (int)ch1; j2++) {
            float t2 = (float)j2 / ch1;
            float e = sinf((1.0f - t2) * 1.5708f);
            e *= e;
            int vt = (int)(14.0f + 52.0f * e);
            int vb = (int)(14.0f + 86.0f * e);
            for (int i2 = (int)(slx + 1.2f * mm); i2 < (int)(slx + slw - 1.2f * mm); i2++) {
                px_blend(c, i2, (int)sly + j2, vt, vt - 2, vt - 4, 1.0f);
                px_blend(c, i2, (int)(sly + slh) - 1 - j2, (int)(vb * 1.05f), vb, (int)(vb * 0.88f),
                         1.0f);
            }
        }
        soft_hedge(c, slx + 1.5f * mm, slx + slw - 1.5f * mm, sly + slh, 1.0f * mm, 1.30f, 0.04f,
                   1);
    }
    /* ---- the inserted diskette --------------------------------------
     * A 3.5" disk is a square with rounded corners; inserted, we see its
     * trailing edge end-on, so those corners round in from LEFT and RIGHT.
     * The shell is thicker at the two ends than in the middle, because the
     * centre section is recessed to take the label - and that label wraps
     * around the bottom edge, so a pale band shows along the lower half of
     * the middle. */
    {
        float dkw = 90.0f * mm, dkx = x + (fw - dkw) * 0.5f;
        float dky = sly + 1.2f * mm, dkh = slh - 2.4f * mm;
        float corner = 2.6f * mm; /* the shell's rounded corners */
        float recess = 0.5f * mm; /* label recess in the middle  */
        float lab_x0 = dkx + 11.0f * mm, lab_x1 = dkx + dkw - 11.0f * mm;
        for (int i2 = (int)dkx; i2 < (int)(dkx + dkw); i2++) {
            float fx = (float)i2 - dkx;
            /* The shell's rounded ends curve AWAY from the viewer, so they
             * shade off toward each end exactly as the chassis side bars do -
             * a cosine falloff, not a change in height. */
            float ce = fminf(fx, dkw - 1.0f - fx);
            float u = (ce < corner) ? (1.0f - ce / corner) : 0.0f; /* 0 flat, 1 end */
            float curve = 0.40f + 0.60f * cosf(u * 1.30f);
            /* the ends are FULL height; the middle is recessed for the label */
            int inLabel = (i2 >= (int)lab_x0 && i2 < (int)lab_x1);
            float top = dky + (inLabel ? recess : 0.0f);
            float bot = dky + dkh - (inLabel ? recess * 0.5f : 0.0f);
            for (int j2 = (int)top; j2 < (int)bot; j2++) {
                float t2 = (bot > top) ? ((float)j2 - top) / (bot - top) : 0.0f;
                float n = hash2(i2, j2, 7) * 5.0f;
                int r2, g2, b2;
                if (inLabel && t2 > 0.42f) {
                    /* the paper label, wrapped around the bottom edge */
                    float lt = (t2 - 0.42f) / 0.58f;
                    int v = (int)(196.0f - 46.0f * lt);
                    r2 = v;
                    g2 = (int)(v * 0.985f);
                    b2 = (int)(v * 0.92f); /* warm paper */
                } else {
                    int v = (int)(60.0f - 22.0f * t2); /* dark shell */
                    r2 = v;
                    g2 = v;
                    b2 = v + 4;
                }
                px_blend(c, i2, j2, (int)((r2 + n) * curve), (int)((g2 + n) * curve),
                         (int)((b2 + n) * curve), 1.0f);
            }
            /* grazing light along the disk's top edge */
            px_blend(c, i2, (int)top, (int)(210 * curve), (int)(210 * curve), (int)(216 * curve),
                     0.55f);
            px_blend(c, i2, (int)top + 1, (int)(150 * curve), (int)(150 * curve),
                     (int)(156 * curve), 0.30f);
            /* and the shadow it casts into the slot below itself */
            px_blend(c, i2, (int)bot - 1, 12, 12, 14, 0.45f);
        }
        /* the step where the recessed label area meets the thicker ends */
        for (int e = 0; e < 2; e++) {
            float ex2 = e ? lab_x1 : lab_x0;
            for (int j2 = (int)(dky); j2 < (int)(dky + dkh); j2++)
                px_blend(c, (int)ex2, j2, 18, 18, 20, 0.35f);
        }
    }

    /* eject: cut opening + protruding cap, eased shine */
    /* Eject button, matched to a photograph of a real drive: a rounded cap
     * standing in its opening, outlined by a THIN dark gap all round - a
     * fine line, not a frame - with the cap face LIGHTER than the
     * surrounding plastic and carrying a soft top-to-bottom gradient.
     * There is no cast shadow and no exposed stem: the whole cue is that
     * hairline gap plus the cap being brighter than its surroundings. */
    float ew = 11.6f * mm, eh = 5.3f * mm;
    float ex = x + fw - ew - 9.0f * mm, ey = y + 16.2f * mm;
    float gap = 0.40f * mm; /* the dark outline */

    float top = 1.45f * mm; /* how far it stands proud */
    float rad = 0.30f * mm; /* barely-there corner ease  */

    /* Only the RECESS ABOVE the cap is dark - that is the opening the
     * button has come out of.  The sides and bottom get no outline: the
     * cap simply sits on the face there, and a soft shadow underneath does
     * the work instead. */
    rrect(c, ex - gap, ey - gap - top, ew + gap * 2, gap + top, rad, (int)(pr * 0.44f),
          (int)(pg * 0.44f), (int)(pb * 0.44f), 0.92f, 1.0f);

    /* soft blurred shadow cast below the extended cap.  Two passes of a
     * widening, fading band read as penumbra rather than a drawn line. */
    for (int k = 0; k < (int)(2.2f * mm); k++) {
        float u = (float)k / (2.2f * mm);
        float a = (1.0f - u) * (1.0f - u) * 0.34f;
        float spread = u * 1.4f * mm;
        int yy = (int)(ey + eh) + k;
        for (int i2 = (int)(ex - spread); i2 < (int)(ex + ew + spread); i2++) {
            /* fade the shadow off toward its ends so it has no hard edge */
            float e = 1.0f;
            float dl = (i2 - (ex - spread)) / (1.6f * mm),
                  dr = ((ex + ew + spread) - i2) / (1.6f * mm);
            if (dl < 1.0f)
                e = dl;
            if (dr < 1.0f)
                e = fminf(e, dr);
            if (e <= 0.0f)
                continue;
            px_shade(c, i2, yy, 1.0f - a * e, 0.0f);
        }
    }

    /* PERSPECTIVE: the drive sits below eye level, so we look slightly down
     * on it and see the button's TOP FACE - a thin lit band above the front
     * face, drawn as a shallow trapezoid (narrowing with distance) and
     * bright because it faces up toward the light.  Without this the cap
     * reads as flush no matter how the gap is drawn. */
    for (int k = 0; k < (int)top; k++) {
        float u = (float)k / top;     /* 0 at the front, 1 far edge */
        float inset = u * 0.9f * mm;  /* the trapezoid narrowing    */
        float sh = 1.30f - 0.16f * u; /* lit, easing back           */
        int yy = (int)(ey - k);
        for (int i2 = (int)(ex + inset); i2 < (int)(ex + ew - inset); i2++) {
            float n = plastic_tex(i2, yy);
            px_blend(c, i2, yy, (int)(pr * (sh + n)), (int)(pg * (sh + n)), (int)(pb * (sh + n)),
                     1.0f);
        }
    }
    /* the crease where the top face turns into the front face */
    for (int i2 = (int)ex; i2 < (int)(ex + ew); i2++)
        px_blend(c, i2, (int)ey, 255, 252, 246, 0.30f);

    /* the cap face */
    rrect(c, ex, ey, ew, eh, rad, (int)(pr * 1.10f), (int)(pg * 1.10f), (int)(pb * 1.10f), 1.10f,
          0.94f);
    housing_edge(c, ex, ey, ew, eh, rad, 0.9f * mm, 0.0f, 1, 0.9f);

    /* activity light: rectangular window, as in the reference */
    led_rect(c, x + 16.5f * mm, y + 18.6f * mm, 4.6f * mm, 2.2f * mm, 26, 44, 26); /* UNLIT */
    led_out[0] = x + 16.5f * mm - 2.3f * mm;
    led_out[1] = y + 18.6f * mm - 1.1f * mm;
    led_out[2] = 4.6f * mm;
    led_out[3] = 2.2f * mm;
}

/* The well a rectangular part sits in, built the way the LED's and the
 * knobs' are: a dark ring hugging the part, a soft shadow on the plastic
 * above where the lip throws it, a lit chamfer lip below.  ring and reach
 * are in px; the rect is the part's own outline. */
static void well_rect(canvas *c, float x, float y, float w, float h, float rad, float ring,
                      float reach) {
    float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
    int saved = canvas_grain;
    canvas_grain = 0;
    for (int j2 = (int)(y - reach) - 1; j2 <= (int)(y + h + reach) + 1; j2++)
        for (int i2 = (int)(x - reach) - 1; i2 <= (int)(x + w + reach) + 1; i2++) {
            float sd = rr_sd((float)i2 + 0.5f, (float)j2 + 0.5f, cx, cy, hw, hh, rad);
            if (sd <= -0.5f || sd > reach)
                continue;
            float up = -((float)j2 + 0.5f - cy) / hh;
            if (up > 1.0f)
                up = 1.0f;
            if (up < -1.0f)
                up = -1.0f;
            if (sd <= ring) { /* the ring itself */
                float a = fminf(1.0f, sd + 0.5f) *
                          (1.0f - fmaxf(0.0f, (sd - ring * 0.6f) / (ring * 0.4f)));
                float top = (up > 0.0f) ? 1.0f : 0.66f;
                px_blend(c, i2, j2, 26, 22, 14, a * 0.85f * top);
            } else {
                float t = (sd - ring) / (reach - ring); /* 0 at ring, 1 outside */
                if (up > 0.15f)                         /* shadow above */
                    px_shade(c, i2, j2, 1.0f - 0.20f * (1.0f - t) * up, 0.0f);
                else if (up < -0.15f) /* lit lip below */
                    px_shade(c, i2, j2, 1.0f + 0.24f * (1.0f - t) * (-up),
                             (1.0f - t) * (-up) * 0.07f);
            }
        }
    canvas_grain = saved;
}

/* The power button: label, well, cap, mark, the shadow it throws, and
 * the LED under it.  Records the LED and the cap's underside in L. */
void power_button(canvas *c, float px0, float pw, float mid, float mm, float band_h,
                  dxm_layout *L) {
    /* painted, and centred over the cap */
    {
        const char *pl = "POWER";
        /* a step down from the moulded lettering's scale (the bitmap
         * font only scales by whole numbers), and clear of the well's
         * shadow above the button */
        float ls = fmaxf(1.0f, canvas_lbl * 0.70f);
        float tw3 = (float)strlen(pl) * 8.0f * ls;
        float ly = mid - pw * 0.39f - 1.9f * mm - 8.0f * ls - 0.7f * mm;
        text_smooth(c, px0 + (pw - tw3) * 0.5f, ly, pl, ls, 112, 107, 96);
    }
    /* the well the button sits in, then the thin cut, then the cap */
    well_rect(c, px0, mid - pw * 0.39f, pw, pw * 0.78f, pw * 0.10f, 0.45f * mm, 1.9f * mm);
    rrect(c, px0, mid - pw * 0.39f, pw, pw * 0.78f, pw * 0.10f, 64, 61, 56, 0.80f, 0.92f);
    /* the cap is moulded in the same darker brown as the drive, not in
     * the case colour - the same multipliers floppy_drive() uses */
    rrect(c, px0 + pw * 0.045f, mid - pw * 0.39f + pw * 0.04f, pw * 0.91f, pw * 0.70f, pw * 0.08f,
          (int)(PLASTIC_R * 0.74f), (int)(PLASTIC_G * 0.72f), (int)(PLASTIC_B * 0.76f), 1.16f,
          0.84f);
    bevel(c, px0 + pw * 0.045f, mid - pw * 0.39f + pw * 0.04f, pw * 0.91f, pw * 0.70f,
          fmaxf(1.0f, pw * 0.06f), 1);
    /* the mark, cut into the cap's face: about 5 mm on a 16 mm cap */
    power_symbol(c, px0 + pw * 0.5f, mid - pw * 0.39f + pw * 0.04f + pw * 0.35f, pw * 0.30f);
    /* the shadow the cap throws down onto the lip and the case: the
     * knobs' bell, run along the cap's lower edge */
    {
        float cx0 = px0 + pw * 0.045f, cw = pw * 0.91f,
              cb = mid - pw * 0.39f + pw * 0.04f + pw * 0.70f;
        float sw = 2.6f * mm, rad = pw * 0.08f;
        int saved = canvas_grain;
        canvas_grain = 0;
        for (int j2 = (int)cb; j2 <= (int)(cb + sw) + 1; j2++)
            for (int i2 = (int)(cx0 - sw); i2 <= (int)(cx0 + cw + sw) + 1; i2++) {
                float sd = rr_sd((float)i2 + 0.5f, (float)j2 + 0.5f, cx0 + cw * 0.5f,
                                 cb - pw * 0.35f, cw * 0.5f, pw * 0.35f, rad);
                if (sd <= 0.0f || sd >= sw || (float)j2 + 0.5f < cb)
                    continue;
                float t = sd / sw, f = (1.0f - t) * (1.0f - t) * (1.0f - t) * (1.0f + 3.0f * t);
                px_shade(c, i2, j2, 1.0f - 0.22f * f, 0.0f);
            }
        canvas_grain = saved;
    }
    /* power LED: a small round lens under the button */
    {
        float lr = 1.35f * mm;
        float lcx = px0 + pw * 0.5f;
        float cap_lo = mid - pw * 0.39f + pw * 0.78f; /* the cap's lower edge */
        float lcy = cap_lo + fmaxf(4.0f, band_h * 0.09f) + 1.6f * mm;
        led(c, lcx, lcy, lr, 26, 44, 26); /* UNLIT */
        L->pwr_led[0] = lcx - lr;
        L->pwr_led[1] = lcy - lr;
        L->pwr_led[2] = lr * 2.0f;
        L->pwr_led[3] = lr * 2.0f;
        L->pwr_shelf = cap_lo;
    }
}

/* ---- the turbo display ---------------------------------------------------- */

/* Signed distance from p to the segment a-b: a bar of half-thickness t with
 * 45-degree pointed ends, its tips at a and b, stood back by m all round
 * the ends (the shader draws the same shape). */
static float seg_sd(float px, float py, float ax, float ay, float bx, float by, float t, float m) {
    float cx = (ax + bx) * 0.5f, cy = (ay + by) * 0.5f;
    float dx = bx - ax, dy = by - ay, L = sqrtf(dx * dx + dy * dy) * 0.5f;
    dx /= 2.0f * L;
    dy /= 2.0f * L;
    float u = fabsf((px - cx) * dx + (py - cy) * dy), v = fabsf(-(px - cx) * dy + (py - cy) * dx);
    float bar = v - t, tip = (u + v - L) * 0.70710678f + m;
    return bar > tip ? bar : tip;
}

/* Coverage of the seven UNLIT segments of the three digits at window point
 * (qx, qy), in window-height units with y up; A is the window's aspect. */
static float seg_ghost(float qx, float qy, float A) {
    static const float ends[7][4] = SEG_ENDS;
    float dh = SEG_DH, dw = dh * SEG_WR, pitch = dw * SEG_PITCH, th = dh * SEG_T;
    float m = th * SEG_GAP, best = 1e9f;
    for (int k = 0; k < 3; k++) {
        float lx = qx - (A * 0.5f + (float)(k - 1) * pitch), ly = qy - 0.5f;
        lx -= ly * SEG_SLANT; /* take the lean out */
        for (int s = 0; s < 7; s++) {
            float ax = ends[s][0] * dw * 0.5f, ay = ends[s][1] * dh * 0.5f;
            float bx = ends[s][2] * dw * 0.5f, by = ends[s][3] * dh * 0.5f;
            float d = seg_sd(lx, ly, ax, ay, bx, by, th * 0.5f, m);
            if (d < best)
                best = d;
        }
        /* the decimal point, at the digit's lower right, never lit */
        float px2 = lx - (dw * 0.5f + th * 0.95f), py2 = ly + dh * 0.5f - th * 0.45f;
        float dp = sqrtf(px2 * px2 + py2 * py2) - th * 0.42f;
        if (dp < best)
            best = dp;
    }
    return best;
}

/* The turbo display's glass: a smoked acrylic window with the three dark
 * digits showing through it the way an unlit LED display does.  The
 * digits are LIT by the shader; here they are only the shadows of
 * themselves.  Records the window in out. */
static void turbo_glass(canvas *c, float x, float y, float w, float h, float out[4]) {
    float rad = h * 0.08f;
    float A = w / h, cx = x + w * 0.5f, cy = y + h * 0.5f;
    int saved = canvas_grain;
    canvas_grain = 0;
    for (int j2 = (int)y - 1; j2 <= (int)(y + h) + 1; j2++)
        for (int i2 = (int)x - 1; i2 <= (int)(x + w) + 1; i2++) {
            float sd = rr_sd((float)i2 + 0.5f, (float)j2 + 0.5f, cx, cy, w * 0.5f, h * 0.5f, rad);
            if (sd > 0.5f)
                continue;
            float a = fminf(1.0f, 0.5f - sd);
            /* the window, in its own units: x across, y UP */
            float qx = ((float)i2 + 0.5f - x) / h, qy = (y + h - ((float)j2 + 0.5f)) / h;
            /* smoked acrylic over a black board: near-black with the red
             * of the LEDs' own plastic in it, darker under the top lip
             * where the well shades it, and a faint sheen down the face */
            /* a black epoxy face, the way a bare LED display module is,
             * with the red of the diffusers only in the segments */
            float r = 21.0f, g = 15.0f, b = 14.0f;
            float lip = 1.0f - 0.45f * expf(-(1.0f - qy) / 0.10f);
            float sheen = 1.0f + 0.20f * expf(-((qy - 0.72f) * (qy - 0.72f)) / 0.06f);
            float f = lip * sheen;
            /* the unlit segments: the frosted light pipes are a pale
             * pinkish grey against the face, plainly there, as they are on
             * a real module - the shadows of the digits, not a hint */
            float gd = seg_ghost(qx, qy, A);
            float pxu = 1.0f / h; /* one pixel, in window units */
            float ghost = 1.0f - fminf(1.0f, fmaxf(0.0f, (gd + pxu * 0.5f) / pxu));
            r = r * f + (96.0f - r * f) * ghost * 0.80f;
            g = g * f + (74.0f - g * f) * ghost * 0.80f;
            b = b * f + (70.0f - b * f) * ghost * 0.80f;
            /* the frosted grain a moulded light pipe carries */
            float n = (hash2(i2, j2, 11) - 0.5f) * 5.0f;
            px_blend(c, i2, j2, (int)(r + n), (int)(g + n), (int)(b + n), a);
        }
    /* the glass's own edge catches a line along the top, under the lip */
    for (int i2 = (int)(x + rad); i2 < (int)(x + w - rad); i2++)
        px_blend(c, i2, (int)y, 255, 255, 255, 0.09f);
    canvas_grain = saved;
    out[0] = x;
    out[1] = y;
    out[2] = w;
    out[3] = h;
}

/* One cap of the button cluster: the power cap's construction at a
 * fraction of the size - cap, bevel, the shadow it throws on the plate
 * below - with its function painted on it.  Records its outline in out,
 * for the mouse. */
static void cluster_cap(canvas *c, float x, float y, float w, float h, float mm, const char *label,
                        float out[4]) {
    float rad = h * 0.12f;
    rrect(c, x, y, w, h, rad, (int)(PLASTIC_R * 0.74f), (int)(PLASTIC_G * 0.72f),
          (int)(PLASTIC_B * 0.76f), 1.16f, 0.84f);
    bevel(c, x, y, w, h, fmaxf(1.0f, 0.45f * mm), 1);
    {
        int saved = canvas_grain;
        canvas_grain = 0;
        float sw = 1.0f * mm, cb = y + h;
        for (int j2 = (int)cb; j2 <= (int)(cb + sw) + 1; j2++) {
            float t = ((float)j2 + 0.5f - cb) / sw;
            if (t < 0.0f || t >= 1.0f)
                continue;
            float f = (1.0f - t) * (1.0f - t);
            for (int i2 = (int)x; i2 < (int)(x + w); i2++)
                px_shade(c, i2, j2, 1.0f - 0.30f * f, 0.0f);
        }
        canvas_grain = saved;
    }
    {
        float ls = fmaxf(0.5f, canvas_lbl * 0.62f);
        float tw3 = (float)strlen(label) * 8.0f * ls;
        text_smooth(c, x + (w - tw3) * 0.5f, y + (h - 8.0f * ls) * 0.5f - 0.5f, label, ls, 196, 190,
                    176);
    }
    out[0] = x;
    out[1] = y;
    out[2] = w;
    out[3] = h;
}

/* The turbo module, one part: a single well in the case beside the power
 * cap, level with it, holding a dark plate that carries, left to right,
 * the legend - FPS over MHz, printed, with an LED against each for the
 * shader to light whichever is showing - the glass, and the three keys,
 * MODE across the top, - and + under it, parted only by the plate showing
 * between them.  The way a case carried its display and its buttons: one
 * moulded unit, not a hole for each.  Records the window in seg, each
 * key's outline in btn[] and the two LEDs in mode_led[]. */
void turbo_module(canvas *c, float x, float pw, float mid, float mm, float seg[4], float btn[3][4],
                  float mode_led[2][4]) {
    /* The module is drawn a millimetre taller than the power cap's
     * height would make it, and everything in it scales with that; its
     * top edge is on the cap's top line. */
    float k = (pw * 0.78f + 1.4f * mm + 1.0f * mm) / (pw * 0.78f + 1.4f * mm);
    float lip = 0.7f * mm * k, gap = 0.8f * mm * k, part = 1.1f * mm * k;
    float gh = pw * 0.78f * k, gw = SEG_WIN_W_MM * mm * k, kw = 13.5f * mm * k;
    /* the legend strip: an LED and a printed word, FPS over MHz, in the
     * VGA face at a 2.6 mm cell - a silk-screened legend, not moulding */
    float ls = 2.6f * mm * k / 16.0f, lr = 1.0f * mm * k;
    float sw = (1.2f * mm + 1.0f * mm + 0.6f * mm) * k + lr * 2.0f + 3.0f * 8.0f * ls;
    float h = gh + 2.0f * lip, w = lip + sw + gw + part + kw + lip;
    float y = mid - pw * 0.39f, rad = h * 0.09f;
    well_rect(c, x, y, w, h, rad, 0.45f * mm, 1.9f * mm);
    rrect(c, x, y, w, h, rad, 64, 61, 56, 0.80f, 0.92f);
    {
        const char *words[2] = {"FPS", "MHz"};
        float pitch = gh * 0.40f, lcx = x + lip + 1.2f * mm * k + lr;
        float tx = lcx + lr + 1.0f * mm * k;
        for (int i = 0; i < 2; i++) {
            float cy = y + h * 0.5f + (i ? 0.5f : -0.5f) * pitch;
            led(c, lcx, cy, lr, 44, 18, 14); /* UNLIT, red */
            mode_led[i][0] = lcx - lr;
            mode_led[i][1] = cy - lr;
            mode_led[i][2] = lr * 2.0f;
            mode_led[i][3] = lr * 2.0f;
            text_smooth16(c, tx, cy - 8.0f * ls, words[i], ls, 196, 190, 176);
        }
    }
    turbo_glass(c, x + lip + sw, y + lip, gw, gh, seg);
    float kx = x + lip + sw + gw + part, ky = y + lip;
    float ch = (gh - gap) * 0.5f, cw = (kw - gap) * 0.5f;
    cluster_cap(c, kx, ky, kw, ch, mm, "MODE", btn[0]);
    cluster_cap(c, kx, ky + ch + gap, cw, ch, mm, "-", btn[1]);
    cluster_cap(c, kx + cw + gap, ky + ch + gap, cw, ch, mm, "+", btn[2]);
}
