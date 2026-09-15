/* parts.c — the moulded parts: LEDs, the power button, vents, the
 * speaker grilles and the floppy drive. */
#include "internal.h"
#include "segdisp.h"

/* the condensed cut the case's printed legends are set in (text_helv) */
#define KEY_SQUEEZE 0.82f

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
    /* the drive is smooth moulding: none of the case's grain on it */
    int grain_was = canvas_grain;
    canvas_grain = 0;
    /* the drive is a separate moulding, in the keys' plastic */
    int pr = (int)KEY_R, pg = (int)KEY_G, pb = (int)KEY_B;
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
             * floor: a dark grey at the top, where the room's light reaches
             * in, fading down to near black where it meets the disk.  Its
             * top corners round; it runs on under the slot's face below. */
            const int HOLE_TOP = 46, HOLE_BOT = 7;
            for (int j2 = (int)floorf(y0); j2 < (int)ceilf(y1 + rr); j2++)
                for (int i2 = (int)floorf(cx0); i2 < (int)ceilf(cx0 + cw0); i2++) {
                    float sd = rr_sd((float)i2 + 0.5f, (float)j2 + 0.5f, cx0 + cw0 * 0.5f,
                                     y0 + (ch0 + rr) * 0.5f, cw0 * 0.5f, (ch0 + rr) * 0.5f, rr);
                    float cov = fminf(1.0f, fmaxf(0.0f, 0.5f - sd));
                    if (cov <= 0.0f)
                        continue;
                    float t = fminf(1.0f, fmaxf(0.0f, ((float)j2 + 0.5f - y0) / ch0));
                    float v = (float)HOLE_TOP + (float)(HOLE_BOT - HOLE_TOP) * t;
                    px_blend(c, i2, j2, (int)v, (int)v, (int)(v + 2.0f), cov);
                }
        } else
            rrect(c, cx0, y0 - rr, cw0, ch0 + rr, rr, pr, pg, pb, 0.46f, 0.84f);
        /* eased side walls (the hole needs no lit walls - it is open) */
        if (k == 1) {
            soft_vedge(c, y0 + rr * 0.4f, y1 - rr * 0.4f, cx0 + 0.2f * mm, 1.1f * mm, 0.62f, 1);
            soft_vedge(c, y0 + rr * 0.4f, y1 - rr * 0.4f, cx0 + cw0 - 0.2f * mm, 1.1f * mm, 0.72f,
                       0);
        }
        /* the overhang shadow at the top of the cut, and the lit lip along
         * the bottom inner wall - on the floor cut only: the hole is a fade
         * of its own, grey at the top to black at the disk */
        if (k == 1) {
            soft_hedge(c, cx0, cx0 + cw0, y0, 1.8f * mm, 0.42f, 0.0f, 1);
            soft_hedge(c, cx0 + 0.8f * mm, cx0 + cw0 - 0.8f * mm, y1 - 1, 1.3f * mm, 1.34f, 0.05f,
                       0);
        }
    }

    /* slot: chamfered opening, eased interior faces.  Where the open door
     * hole above meets the slot there is no plastic between them, so no
     * chamfer along the slot's top and no top wall inside it: those run
     * only either side of the hole.  The chamfer is drawn round the whole
     * slot and the hole's pixels put back over it. */
    {
        int hx0 = (int)ceilf(tcx), hx1 = (int)floorf(tcx + tcw);
        int hy0 = (int)floorf(sly - 1.2f * mm) - 2, hy1 = (int)floorf(sly);
        int hw = hx1 - hx0, hh = hy1 - hy0;
        uint8_t *keep = (hw > 0 && hh > 0) ? malloc((size_t)hw * hh * 4) : NULL;
        if (keep)
            for (int j2 = 0; j2 < hh; j2++)
                for (int i2 = 0; i2 < hw; i2++) {
                    int xx = hx0 + i2, yy = hy0 + j2;
                    uint8_t *d = keep + ((size_t)j2 * hw + i2) * 4;
                    if (xx < 0 || yy < 0 || xx >= c->w || yy >= c->h)
                        memset(d, 0, 4);
                    else
                        memcpy(d, c->px + ((size_t)yy * c->w + xx) * 4, 4);
                }
        chamfer_ring(c, slx, sly, slw, slh, slh * 0.24f, 1.2f * mm);
        if (keep) {
            for (int j2 = 0; j2 < hh; j2++)
                for (int i2 = 0; i2 < hw; i2++) {
                    int xx = hx0 + i2, yy = hy0 + j2;
                    if (xx >= 0 && yy >= 0 && xx < c->w && yy < c->h)
                        memcpy(c->px + ((size_t)yy * c->w + xx) * 4,
                               keep + ((size_t)j2 * hw + i2) * 4, 4);
                }
            free(keep);
        }
    }
    rrect(c, slx, sly, slw, slh, slh * 0.24f, 12, 11, 10, 1.0f, 1.0f);
    {
        float ch1 = 1.4f * mm;
        for (int j2 = 0; j2 < (int)ch1; j2++) {
            float t2 = (float)j2 / ch1;
            float e = sinf((1.0f - t2) * 1.5708f);
            e *= e;
            int vt = (int)(14.0f + 52.0f * e);
            int vb = (int)(14.0f + 86.0f * e);
            /* right across, into the rounded ends: the faces carry on round
             * the corners and meet the end walls, rather than stopping short
             * and leaving the ends bare */
            for (int i2 = (int)slx; i2 < (int)(slx + slw); i2++) {
                float rad = slh * 0.24f;
                int yt = (int)sly + j2, yb = (int)(sly + slh) - 1 - j2;
                /* the top wall only where there is plastic above it */
                if (((float)i2 + 0.5f < tcx || (float)i2 + 0.5f > tcx + tcw) &&
                    rr_sd((float)i2 + 0.5f, (float)yt + 0.5f, slx + slw * 0.5f, sly + slh * 0.5f,
                          slw * 0.5f, slh * 0.5f, rad) <= 0.0f)
                    px_blend(c, i2, yt, vt, vt - 2, vt - 4, 1.0f);
                if (rr_sd((float)i2 + 0.5f, (float)yb + 0.5f, slx + slw * 0.5f, sly + slh * 0.5f,
                          slw * 0.5f, slh * 0.5f, rad) <= 0.0f)
                    px_blend(c, i2, yb, (int)(vb * 1.05f), vb, (int)(vb * 0.88f), 1.0f);
            }
        }
        /* the ends' inner walls, as the top and bottom faces are: a band
         * at each end easing from the wall's own tone at the edge to the
         * dark of the slot, the right-hand wall turned to the light and the
         * left-hand one away from it, rounded off with the slot's corners */
        {
            float cw1 = 1.2f * mm, rad = slh * 0.24f;
            for (int j2 = (int)sly; j2 < (int)(sly + slh); j2++)
                for (int k2 = 0; k2 < (int)cw1; k2++) {
                    float t2 = (float)k2 / cw1;
                    float e = sinf((1.0f - t2) * 1.5708f);
                    e *= e;
                    for (int side = 0; side < 2; side++) {
                        int i2 = side ? (int)(slx + slw) - 1 - k2 : (int)slx + k2;
                        if (rr_sd((float)i2 + 0.5f, (float)j2 + 0.5f, slx + slw * 0.5f,
                                  sly + slh * 0.5f, slw * 0.5f, slh * 0.5f, rad) > 0.0f)
                            continue;
                        /* lit toward the top, as the end walls of a slot are */
                        float v = (float)(j2 - (int)sly) / slh;
                        int lit = (int)(14.0f + (side ? 70.0f : 38.0f) * e * (1.0f - 0.35f * v));
                        px_blend(c, i2, j2, lit, lit - 2, lit - 4, e);
                    }
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
     * the middle.  Fully inserted, the mechanism has dropped it about a
     * millimetre down into the drive: it is the 3.3 mm of a real disk, its
     * lower edge hidden behind the slot's bottom lip, and it sits back in
     * the dark of the slot, where little light reaches it. */
    {
        float dkw = 90.0f * mm, dkx = x + (fw - dkw) * 0.5f;
        float dky = sly + 2.2f * mm, dkh = 3.3f * mm;
        float clip = sly + slh - 0.5f * mm; /* the bottom lip stands in front below this */
        const float INSIDE = 0.70f;         /* how much of the light reaches it in there */
        const float LABEL_T = 0.22f;        /* where the label starts, down the edge */
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
            for (int j2 = (int)top; j2 < (int)bot && (float)j2 < clip; j2++) {
                float t2 = (bot > top) ? ((float)j2 - top) / (bot - top) : 0.0f;
                float n = hash2(i2, j2, 7) * 5.0f;
                int r2, g2, b2;
                if (inLabel && t2 > LABEL_T) {
                    /* the paper label, wrapped around the bottom edge: it
                     * starts high on the edge, so the part of the edge the
                     * lip leaves in view is mostly label */
                    float lt = (t2 - LABEL_T) / (1.0f - LABEL_T);
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
                /* in the slot's shade: darker still near the top, under
                 * the slot's top wall */
                float shade = INSIDE * (0.80f + 0.20f * fminf(1.0f, t2 * 2.5f));
                px_blend(c, i2, j2, (int)((r2 + n) * curve * shade),
                         (int)((g2 + n) * curve * shade), (int)((b2 + n) * curve * shade), 1.0f);
            }
            /* the faint catch of what little light gets in, along the
             * disk's top edge */
            px_blend(c, i2, (int)top, (int)(150 * curve), (int)(150 * curve), (int)(156 * curve),
                     0.16f);
            /* and the shadow it casts below itself, where that shows */
            if ((float)bot - 1.0f < clip)
                px_blend(c, i2, (int)bot - 1, 12, 12, 14, 0.45f);
        }
        /* the step where the recessed label area meets the thicker ends */
        for (int e = 0; e < 2; e++) {
            float ex2 = e ? lab_x1 : lab_x0;
            for (int j2 = (int)(dky); j2 < (int)(dky + dkh) && (float)j2 < clip; j2++)
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
    float ex = x + fw - ew - 11.0f * mm, ey = y + 17.0f * mm;
    float gap = 0.40f * mm; /* the dark outline */

    float top = 1.05f * mm; /* how far it stands proud */
    float rad = 0.65f * mm; /* a slight ease on the corners */

    /* Only the RECESS ABOVE the cap is dark - that is the opening the
     * button has come out of.  The sides and bottom get no outline: the
     * cap simply sits on the face there, and a soft shadow underneath does
     * the work instead. */
    rrect(c, ex - gap, ey - gap - top, ew + gap * 2, gap + top, rad, (int)(pr * 0.44f),
          (int)(pg * 0.44f), (int)(pb * 0.44f), 0.92f, 1.0f);

    /* soft blurred shadow cast below the extended cap.  Two passes of a
     * widening, fading band read as penumbra rather than a drawn line. */
    /* the shadow's length follows how far the cap stands out */
    float slen = 2.2f * mm * (top / (1.45f * mm));
    for (int k = 0; k < (int)slen; k++) {
        float u = (float)k / slen;
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
        float sh = 1.40f - 0.14f * u; /* lit, a shade above the front face */
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
    rrect(c, ex, ey, ew, eh, rad, (int)(pr * 1.10f), (int)(pg * 1.10f), (int)(pb * 1.10f), 1.02f,
          0.92f);
    housing_edge(c, ex, ey, ew, eh, rad, 0.9f * mm, 0.0f, 1, 0.9f);

    /* activity light: rectangular window, as in the reference */
    led_rect(c, x + 21.0f * mm, y + 18.6f * mm, 5.4f * mm, 2.2f * mm, 26, 44, 26); /* UNLIT */
    led_out[0] = x + 21.0f * mm - 2.7f * mm;
    led_out[1] = y + 18.6f * mm - 1.1f * mm;
    led_out[2] = 5.4f * mm;
    led_out[3] = 2.2f * mm;
    canvas_grain = grain_was;
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
    /* smooth moulding: none of the case's grain on the cap or its well */
    int grain_was = canvas_grain;
    canvas_grain = 0;
    /* painted, and centred over the cap */
    {
        /* in the face the mouse lamps' words are set in, and clear of the
         * well's shadow above the button */
        const char *pl = "POWER";
        float cap = 2.0f * mm, tr = cap * 0.05f;
        float tw3 = helv_width(pl, cap, KEY_SQUEEZE, tr);
        float ly = mid - pw * 0.39f - 1.9f * mm - cap - 0.9f * mm;
        text_helv(c, px0 + (pw - tw3) * 0.5f, ly, pl, cap, KEY_SQUEEZE, tr, 88, 83, 72);
    }
    /* the well the button sits in, then the thin cut, then the cap */
    well_rect(c, px0, mid - pw * 0.39f, pw, pw * 0.78f, pw * 0.10f, 0.45f * mm, 1.9f * mm);
    rrect(c, px0, mid - pw * 0.39f, pw, pw * 0.78f, pw * 0.10f, 64, 61, 56, 0.80f, 0.92f);
    /* the cap is moulded in the keys' plastic, as the drive is, not in the
     * case colour */
    L->pwr_btn[0] = px0; /* where a click powers the machine off */
    L->pwr_btn[1] = mid - pw * 0.39f;
    L->pwr_btn[2] = pw;
    L->pwr_btn[3] = pw * 0.78f;
    /* the cap itself, with its mark and the shadow it throws, is a live
     * key drawn last (power_key), so a click can press it */
    {
        float kw = pw * 0.91f, kh = pw * 0.70f, dummy[4];
        keys_slot(KEY_POWER, px0 + pw * 0.045f + kw * 0.5f,
                  mid - pw * 0.39f + pw * 0.04f + kh * 0.5f, kw, kh, mm, "", 0.0f, 0,
                  KEY_STYLE_POWER, dummy);
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
    canvas_grain = grain_was;
}

/* ---- the OSD button ----------------------------------------------------- */

/* A key's legend: its function in lightly condensed Helvetica Bold, cream,
 * `cap` px capitals from `ty` down, centred across on `mid`, on canvas c;
 * the case's finish is taken at (fx, fy) on the case, and it dims a little
 * as the key goes down.  The same on every key, whatever its cap is like. */
static void key_legend(canvas *c, float mid, float ty, const char *label, float cap, float press,
                       float fx, float fy, int cw, int ch) {
    const float sq = 0.92f; /* a little condensed */
    float track = cap * 0.06f, fat = fmaxf(0.5f, cap * 0.05f);
    float tw = helv_width(label, cap, sq, track) + fat;
    float lx = mid - tw * 0.5f;
    float ink[3] = {232.0f * (1.0f - 0.04f * press), 224.0f * (1.0f - 0.04f * press),
                    202.0f * (1.0f - 0.04f * press)};
    finish_rgb(fx, fy, cw, ch, ink);
    int lr = (int)ink[0], lg = (int)ink[1], lb = (int)ink[2];
    int saved = canvas_grain;
    canvas_grain = 0;
    text_helv(c, lx, ty, label, cap, sq, track, lr, lg, lb);
    text_helv(c, lx + fat, ty, label, cap, sq, track, lr, lg, lb);
    canvas_grain = saved;
}

/* The power cap's bevel, pressed `press` of the way: the lit edges along
 * the top and the left give up their highlight and go to a faint shade,
 * a touch darker than the face, as the cap goes down out of the light;
 * the shaded edges along the bottom and the right stay as they are.
 * bevel(..., 1) at press 0. */
#define POWER_EDGE_SHADE 0.10f /* the lit edges' shade, fully pressed */
static void power_bevel(canvas *c, float x, float y, float w, float h, float t, float press) {
    for (int k = 0; k < (int)t; k++) {
        float a = 0.5f * (1.0f - (float)k / t);
        /* the lit edges: white fading out, a little black fading in */
        float lit = a * (1.0f - press), dark = POWER_EDGE_SHADE * (1.0f - (float)k / t) * press;
        for (int i = (int)x + k; i < (int)(x + w) - k; i++) {
            px_blend(c, i, (int)y + k, 255, 255, 255, lit * 0.55f);
            px_blend(c, i, (int)y + k, 0, 0, 0, dark);
            px_blend(c, i, (int)(y + h) - 1 - k, 0, 0, 0, a * 0.45f);
        }
        for (int j = (int)y + k; j < (int)(y + h) - k; j++) {
            px_blend(c, (int)x + k, j, 255, 255, 255, lit * 0.40f);
            px_blend(c, (int)x + k, j, 0, 0, 0, dark * 0.75f);
            px_blend(c, (int)(x + w) - 1 - k, j, 0, 0, 0, a * 0.35f);
        }
    }
}

/* The power button's cap, as a key: a slab of the keys' plastic in its
 * well, lit at the top and falling off down it, an edge bevel, the power
 * mark cut into its face and the soft shadow it throws down onto the lip
 * and the case below.  Pressed, it stays where it is: only the highlight
 * along its top and left edges goes, turning to a faint shade a touch
 * darker than the face (power_bevel); the face, the mark and the shadow
 * stay as they are.
 * Worked out in the case's coordinates and written ox, oy in, as flat_key
 * is.  (cx, cy) is the cap's centre and w x h the cap. */
static void power_key(canvas *c, float cx, float cy, float w, float h, float mm, float press,
                      int ox, int oy, int cw, int ch) {
    /* the cap does not move: a press shows only in its light */
    float x = cx - w * 0.5f, y = cy - h * 0.5f, rad = h * 0.114f;
    int saved = canvas_grain;
    canvas_grain = 0;
    /* the shadow the cap throws down onto the lip and the case: the knobs'
     * bell, run along the cap's lower edge, less of it as the cap goes in */
    {
        float sw = 2.6f * mm, depth = 0.22f;
        float cb = y + h;
        for (int j2 = (int)floorf(cb); j2 <= (int)floorf(cb + sw) + 1; j2++)
            for (int i2 = (int)floorf(x - sw); i2 <= (int)floorf(x + w + sw) + 1; i2++) {
                float sd = rr_sd((float)i2 + 0.5f, (float)j2 + 0.5f, cx, cb - h * 0.5f, w * 0.5f,
                                 h * 0.5f, rad);
                if (sd <= 0.0f || sd >= sw || (float)j2 + 0.5f < cb)
                    continue;
                float t = sd / sw, f = (1.0f - t) * (1.0f - t) * (1.0f - t) * (1.0f + 3.0f * t);
                px_shade(c, i2 - ox, j2 - oy, 1.0f - depth * f, 0.0f);
            }
    }
    /* the face: lit at the top, falling off down it, and the case's finish
     * where it sits */
    for (int j = (int)floorf(y) - 1; j <= (int)floorf(y + h) + 1; j++)
        for (int i = (int)floorf(x) - 1; i <= (int)floorf(x + w) + 1; i++) {
            float sd =
                rr_sd((float)i + 0.5f, (float)j + 0.5f, cx, y + h * 0.5f, w * 0.5f, h * 0.5f, rad);
            float cov = fminf(1.0f, fmaxf(0.0f, 0.5f - sd));
            if (cov <= 0.0f)
                continue;
            float ty = ((float)j + 0.5f - y) / h;
            /* the face, a very little darker as it goes down */
            float sh = (1.16f + (0.84f - 1.16f) * ty) * (1.0f - 0.03f * press);
            float rgb[3] = {KEY_R * sh, KEY_G * sh, KEY_B * sh};
            finish_rgb((float)i, (float)j, cw, ch, rgb);
            px_blend(c, i - ox, j - oy, (int)rgb[0], (int)rgb[1], (int)rgb[2], cov);
        }
    power_bevel(c, x - (float)ox, y - (float)oy, w, h, fmaxf(1.0f, w * 0.066f), press);
    /* the mark, cut into the cap's face: about 5 mm on a 16 mm cap */
    power_symbol(c, cx - (float)ox, y + h * 0.5f - (float)oy, w * 0.33f);

    canvas_grain = saved;
}

/* The flat cap the turbo module's keys are: a slab of the keys' plastic
 * lying on the module's plate, its face lit from above and falling off
 * down it, an edge bevel, and the shadow it throws on the plate below - the
 * power cap's construction at a fraction of the size.  Pressed, the face
 * dims, the bevel turns in and the shadow goes, and the legend goes down
 * with it.  Its legend is the other keys' (key_legend), but for + and -,
 * which are drawn as bars. */
static void flat_key(canvas *c, float cx, float cy, float w, float h, float mm, const char *label,
                     float cap, float press, int ox, int oy, int cw, int ch) {
    /* Everything is worked out in the case's own coordinates and only
     * written ox, oy in: a redraw into a patch then lands on exactly the
     * pixels the bake drew, where shifting the geometry itself would round
     * the face's gradient a level differently here and there. */
    float x = cx - w * 0.5f, y = cy - h * 0.5f, rad = h * 0.12f;
    int saved = canvas_grain;
    /* the shadow on the plate under it */
    canvas_grain = 0;
    {
        float sw = 1.0f * mm, cb = y + h, depth = 0.30f * (1.0f - 0.8f * press);
        for (int j2 = (int)floorf(cb); j2 <= (int)floorf(cb + sw) + 1; j2++) {
            float t = ((float)j2 + 0.5f - cb) / sw;
            if (t < 0.0f || t >= 1.0f)
                continue;
            float f = (1.0f - t) * (1.0f - t);
            for (int i2 = (int)floorf(x); i2 < (int)floorf(x + w); i2++)
                px_shade(c, i2 - ox, j2 - oy, 1.0f - depth * f, 0.0f);
        }
    }
    canvas_grain = saved;
    /* the face: lit at the top, falling off down it, smooth, and the
     * case's finish where it sits */
    for (int j = (int)floorf(y) - 1; j <= (int)floorf(y + h) + 1; j++)
        for (int i = (int)floorf(x) - 1; i <= (int)floorf(x + w) + 1; i++) {
            float sd = rr_sd((float)i + 0.5f, (float)j + 0.5f, cx, cy, w * 0.5f, h * 0.5f, rad);
            float cov = fminf(1.0f, fmaxf(0.0f, 0.5f - sd));
            if (cov <= 0.0f)
                continue;
            float ty = ((float)j + 0.5f - y) / h;
            float sh = (1.16f + (0.84f - 1.16f) * ty) * (1.0f - 0.08f * press);
            float rgb[3] = {KEY_R * sh, KEY_G * sh, KEY_B * sh};
            finish_rgb((float)i, (float)j, cw, ch, rgb);
            px_blend(c, i - ox, j - oy, (int)rgb[0], (int)rgb[1], (int)rgb[2], cov);
        }
    /* the bevel, raised; turned in once it is most of the way down */
    bevel(c, x - (float)ox, y - (float)oy, w, h, fmaxf(1.0f, 0.45f * mm), press < 0.5f);
    /* the legend, going down with the face.  + and - are not set in type:
     * a font's signs are small marks cut for text, and blown up they go
     * thin and soft.  They are drawn as bars instead - as heavy as MODE's
     * strokes and a third of the cap across - in the legends' cream. */
    if (!strcmp(label, "+") || !strcmp(label, "-")) {
        float mx = cx - (float)ox, my = cy - (float)oy + press * 0.35f * mm;
        float half = fminf(w, h) * 0.16f;   /* half the bar's length */
        float t = fmaxf(1.4f, cap * 0.20f); /* the stroke */
        float ink[3] = {232.0f * (1.0f - 0.04f * press), 224.0f * (1.0f - 0.04f * press),
                        202.0f * (1.0f - 0.04f * press)};
        finish_rgb(cx, cy, cw, ch, ink);
        int plus = label[0] == '+';
        int saved_grain = canvas_grain;
        canvas_grain = 0;
        for (int j = (int)floorf(my - half) - 1; j <= (int)ceilf(my + half) + 1; j++)
            for (int i = (int)floorf(mx - half) - 1; i <= (int)ceilf(mx + half) + 1; i++) {
                float px = (float)i + 0.5f - mx, py = (float)j + 0.5f - my;
                /* coverage of a bar: box-filtered along both axes */
                float hb = fminf(1.0f, fmaxf(0.0f, half + 0.5f - fabsf(px))) *
                           fminf(1.0f, fmaxf(0.0f, t * 0.5f + 0.5f - fabsf(py)));
                float vb = plus ? fminf(1.0f, fmaxf(0.0f, t * 0.5f + 0.5f - fabsf(px))) *
                                      fminf(1.0f, fmaxf(0.0f, half + 0.5f - fabsf(py)))
                                : 0.0f;
                float a = fmaxf(hb, vb);
                if (a > 0.0f)
                    px_blend(c, i, j, (int)ink[0], (int)ink[1], (int)ink[2], a);
            }
        canvas_grain = saved_grain;
        return;
    }
    key_legend(c, cx - (float)ox, cy - cap * 0.5f - 0.5f + press * 0.35f * mm - (float)oy, label,
               cap, press, cx, cy, cw, ch);
}

/* A key of the case's own, standing proud of the plastic: a keycap in
 * mid-grey moulding, seen a little from above.  A firm dark outline - the
 * hole the key stands in - and inside it the cap: four SLOPES, flat angled
 * faces running in from the rim to the flat top, the top slope catching
 * the light, the two sides half-lit, the bottom in shade, round a face of
 * one flat colour, and a soft shadow on the case under it.  Its function
 * is printed on the face in Helvetica Bold, `cap` px capitals.
 *
 * `press` is how far it is pushed in, 0 up to 1 down: the cap sinks into
 * its hole, so a band of the hole's dark wall shows above it and less of
 * the front slope below; the shadow it throws draws in and fades, and the
 * lit slope and the face lose a little light as they go under the rim.
 * `stained` gives the cap the grime of a key that is pressed a lot.  (cx, cy) is
 * its centre, w x h the whole cap.  The key is drawn after the case's
 * finish, so it takes the finish itself: (ox, oy) is where canvas c sits
 * on the cw x ch case, so the yellowing and the key light are the ones at
 * that spot. */
/* how many pixels the keycap's outline eases over, at its outer edge and
 * where it meets the cap: 1 is a hard line */
#define OUTLINE_SOFT 1.5f
/* the outline's colour: a very dark warm brown, a little short of black */
static const float OUTLINE_RGB[3] = {44.0f, 41.0f, 34.0f};
/* the slight well round a keycap: how far out it reaches, mm, and how much
 * it darkens the case at the gap */
#define WELL_REACH 0.9f
#define WELL_DEPTH 0.12f

static void case_key(canvas *c, float cx, float cy, float w, float h, float mm, const char *label,
                     float cap, int stained, float press, float ox, float oy, int cw, int ch) {
    float x = cx - w * 0.5f, y = cy - h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
    float rad = h * 0.14f;
    float line = fmaxf(1.2f, 0.38f * mm); /* the outline */
    float slope = h * 0.12f;              /* the angled faces, in from the outline */
    float top_slope = slope * 0.80f;      /* the top one is foreshortened */
    const float R = KEY_R, G = KEY_G, B = KEY_B;
    float sink = press * 0.36f * mm; /* how far the cap has gone in */
    float reach = 1.0f * mm * (1.0f - 0.35f * press), drop = 0.40f * mm * (1.0f - 0.6f * press);
    float shade = 0.24f * (1.0f - 0.45f * press);
    int saved = canvas_grain;
    /* the shadow on the case, mostly under it */
    canvas_grain = 0;
    for (int j = (int)(y - reach); j <= (int)(y + h + reach + drop) + 1; j++)
        for (int i = (int)(x - reach); i <= (int)(x + w + reach) + 1; i++) {
            float sd = rr_sd((float)i + 0.5f, (float)j + 0.5f - drop, cx, cy, hw, hh, rad);
            if (sd <= -1.0f || sd > reach)
                continue;
            float t = fmaxf(0.0f, sd) / reach;
            px_shade(c, i, j, 1.0f - shade * (1.0f - t) * (1.0f - t), 0.0f);
        }
    /* A very slight well round the key: the case dips a little as it runs
     * into the gap, so the dark outline has a soft surround rather than
     * starting straight off the flat plastic - shaded a touch more along the
     * top, where the rim stands between it and the light, and no lit lip
     * along the bottom, where the key's own shadow already falls. */
    {
        float well = WELL_REACH * mm;
        for (int j = (int)(y - well) - 1; j <= (int)(y + h + well) + 1; j++)
            for (int i = (int)(x - well) - 1; i <= (int)(x + w + well) + 1; i++) {
                float sd = rr_sd((float)i + 0.5f, (float)j + 0.5f, cx, cy, hw, hh, rad);
                if (sd <= -0.5f || sd >= well)
                    continue;
                float t = fmaxf(0.0f, sd) / well; /* 0 at the gap, 1 at the rim */
                float up = fminf(1.0f, fmaxf(0.0f, (cy - ((float)j + 0.5f)) / hh));
                float dip = WELL_DEPTH * (1.0f + 0.6f * up) * (1.0f - t) * (1.0f - t);
                px_shade(c, i, j, 1.0f - dip, 0.0f);
            }
    }
    canvas_grain = saved;
    /* the flat face: the cap inset by the outline and the slopes, sunk by
     * `sink`; the bottom slope loses that much, the hole's wall gains it */
    float fx0 = x + line + slope, fx1 = x + w - line - slope;
    float fy0 = y + line + top_slope + sink, fy1 = y + h - line - slope;
    float wall = y + line + sink; /* the cap's rim, below the outline */
    for (int j = (int)y - 1; j <= (int)(y + h) + 1; j++)
        for (int i = (int)x - 1; i <= (int)(x + w) + 1; i++) {
            float fx = (float)i + 0.5f, fy = (float)j + 0.5f;
            float sd = rr_sd(fx, fy, cx, cy, hw, hh, rad);
            /* the outer edge, eased over a pixel and a half rather than one */
            float cov = fminf(1.0f, fmaxf(0.0f, (0.75f - sd) / OUTLINE_SOFT));
            if (cov <= 0.0f)
                continue;
            /* the outline - the gap round the key, in shadow - and how much
             * of it this pixel is: eased into the cap over a pixel too, so
             * it softens into the slopes instead of stopping on a hard line */
            float o = fminf(1.0f, fmaxf(0.0f, (sd + line) / OUTLINE_SOFT + 0.5f));
            float r, g, b;
            if (o >= 1.0f) {
                r = OUTLINE_RGB[0];
                g = OUTLINE_RGB[1];
                b = OUTLINE_RGB[2];
            } else if (fy < wall) {
                /* the hole's wall, seen above the sunk cap: darker the
                 * deeper, with the cap's own shadow at its foot */
                float u = (wall - fy) / fmaxf(sink, 1.0f);
                float k = 0.34f + 0.10f * u;
                r = R * k;
                g = G * k;
                b = B * k;
            } else {
                float k;
                /* which slope, if any: the one whose edge this pixel is
                 * nearest to, by how far it lies outside the flat face.
                 * Each is one flat tone - a moulded chamfer is a plane -
                 * the top one facing the light, the bottom in shade, the
                 * sides half-lit. */
                float dl = fx0 - fx, dr = fx - fx1, dt = fy0 - fy, db = fy - fy1;
                float out = fmaxf(fmaxf(dl, dr), fmaxf(dt, db));
                if (out <= 0.0f) {
                    k = 1.0f - 0.05f * press; /* the face: one flat colour */
                } else if (out == dt) {
                    k = 1.62f - 0.16f * press;
                } else if (out == db) {
                    k = 0.66f + 0.05f * press;
                } else {
                    k = ((out == dl) ? 1.22f : 0.88f) - 0.03f * press;
                }
                /* a crease where the slopes meet the face */
                if (out > 0.0f && out < 1.0f)
                    k *= 0.92f;
                k *= 1.0f + plastic_tex(i + (int)ox, j + (int)oy) *
                                0.25f; /* a smoother moulding than the case */
                float mr = 1.0f, mg = 1.0f, mb = 1.0f;
                if (stained) {
                    float u = (fx + ox) / mm, v = (fy + oy) / mm;
                    float blot = vnoise(u * 0.45f, v * 0.5f, 51) * 0.6f +
                                 vnoise(u * 1.3f, v * 1.2f, 53) * 0.4f;
                    float d = fmaxf(0.0f, blot - 0.42f) / 0.58f;
                    /* blotches, then the fine mottle under them */
                    d = d * d * 0.22f + (vnoise(u * 2.5f, v * 2.5f, 57) - 0.5f) * 0.037f;
                    mr = 1.0f - d * 0.70f;
                    mg = 1.0f - d;
                    mb = 1.0f - d * 1.40f;
                }
                r = R * k * mr;
                g = G * k * mg;
                b = B * k * mb;
            }
            if (o > 0.0f && o < 1.0f) {
                r += (OUTLINE_RGB[0] - r) * o;
                g += (OUTLINE_RGB[1] - g) * o;
                b += (OUTLINE_RGB[2] - b) * o;
            }
            {
                float rgb[3] = {r, g, b};
                finish_rgb((float)i + ox, (float)j + oy, cw, ch, rgb);
                px_blend(c, i, j, (int)rgb[0], (int)rgb[1], (int)rgb[2], cov);
            }
        }
    /* the legend, going down with the face */
    key_legend(c, cx, (fy0 + fy1) * 0.5f - cap * 0.5f, label, cap, press, cx + ox, cy + oy, cw, ch);
}

/* The keys, kept the way the knobs are: where each goes, and the plastic
 * under it, so a press redraws the key and not the machine.  The plastic
 * is saved before ANY key is drawn, and a redraw puts back every key its
 * square reaches, each at the depth it is at - MODE's square takes in the
 * tops of - and +, which are keys too, not plastic. */
static struct {
    struct {
        float cx, cy, w, h, mm, cap;
        const char *label;
        int stained, placed;
        int style;   /* KEY_STYLE_CAP, _FLAT or _POWER */
        float depth; /* as last drawn */
        uint8_t *bg; /* the square beneath, RGBA, with no key in it */
        int bx, by, bw, bh;
    } k[KEY_COUNT];
    int cw, ch; /* the case they were placed on */
    uint8_t *patch;
} KEYS;

void keys_slot(int which, float cx, float cy, float w, float h, float mm, const char *label,
               float cap, int stained, int style, float out[4]) {
    if (which < 0 || which >= KEY_COUNT)
        return;
    KEYS.k[which].style = style;
    KEYS.k[which].cx = cx;
    KEYS.k[which].cy = cy;
    KEYS.k[which].w = w;
    KEYS.k[which].h = h;
    KEYS.k[which].mm = mm;
    KEYS.k[which].cap = cap;
    KEYS.k[which].label = label;
    KEYS.k[which].stained = stained;
    KEYS.k[which].placed = 1;
    KEYS.k[which].depth = 0.0f;
    out[0] = cx - w * 0.5f;
    out[1] = cy - h * 0.5f;
    out[2] = w;
    out[3] = h;
}

/* draw key `k` onto canvas c, which sits at (ox, oy) on the case */
static void key_draw_at(canvas *c, int k, float ox, float oy) {
    if (KEYS.k[k].style == KEY_STYLE_POWER) {
        power_key(c, KEYS.k[k].cx, KEYS.k[k].cy, KEYS.k[k].w, KEYS.k[k].h, KEYS.k[k].mm,
                  KEYS.k[k].depth, (int)ox, (int)oy, KEYS.cw, KEYS.ch);
        return;
    }
    if (KEYS.k[k].style == KEY_STYLE_FLAT) {
        flat_key(c, KEYS.k[k].cx, KEYS.k[k].cy, KEYS.k[k].w, KEYS.k[k].h, KEYS.k[k].mm,
                 KEYS.k[k].label, KEYS.k[k].cap, KEYS.k[k].depth, (int)ox, (int)oy, KEYS.cw,
                 KEYS.ch);
        return;
    }
    case_key(c, KEYS.k[k].cx - ox, KEYS.k[k].cy - oy, KEYS.k[k].w, KEYS.k[k].h, KEYS.k[k].mm,
             KEYS.k[k].label, KEYS.k[k].cap, KEYS.k[k].stained, KEYS.k[k].depth, ox, oy, KEYS.cw,
             KEYS.ch);
}

/* the square a key's drawing can reach: the cap, its shadow, its wall */
static void key_square(int k, int *bx, int *by, int *bw, int *bh) {
    float mm = KEYS.k[k].mm, reach = 1.0f * mm + 2.0f;
    /* the power cap's shadow falls further: 2.6 mm below it */
    float below = KEYS.k[k].style == KEY_STYLE_POWER ? 2.8f * mm + 2.0f : reach + 0.5f * mm;
    *bx = (int)(KEYS.k[k].cx - KEYS.k[k].w * 0.5f - reach);
    *by = (int)(KEYS.k[k].cy - KEYS.k[k].h * 0.5f - reach);
    *bw = (int)(KEYS.k[k].w + 2.0f * reach) + 2;
    *bh = (int)(KEYS.k[k].h + reach + below) + 2;
}

void keys_reset(void) {
    for (int k = 0; k < KEY_COUNT; k++)
        KEYS.k[k].placed = 0;
}

void keys_draw(canvas *c) {
    KEYS.cw = c->w;
    KEYS.ch = c->h;
    /* first the plastic under every key, with no key on it yet */
    for (int k = 0; k < KEY_COUNT; k++) {
        if (!KEYS.k[k].placed)
            continue;
        int bx, by, bw, bh;
        key_square(k, &bx, &by, &bw, &bh);
        free(KEYS.k[k].bg);
        KEYS.k[k].bg = malloc((size_t)bw * bh * 4);
        KEYS.k[k].bx = bx;
        KEYS.k[k].by = by;
        KEYS.k[k].bw = bw;
        KEYS.k[k].bh = bh;
        if (!KEYS.k[k].bg)
            continue;
        for (int j = 0; j < bh; j++)
            for (int i = 0; i < bw; i++) {
                int x = bx + i, y = by + j;
                uint8_t *d = KEYS.k[k].bg + ((size_t)j * bw + i) * 4;
                if (x < 0 || y < 0 || x >= c->w || y >= c->h)
                    memset(d, 0, 4);
                else
                    memcpy(d, c->px + ((size_t)y * c->w + x) * 4, 4);
            }
    }
    /* then the keys, over it; the alpha under them stays the case's */
    for (int k = 0; k < KEY_COUNT; k++)
        if (KEYS.k[k].placed) {
            key_draw_at(c, k, 0.0f, 0.0f);
            for (int j = 0; j < KEYS.k[k].bh && KEYS.k[k].bg; j++)
                for (int i = 0; i < KEYS.k[k].bw; i++) {
                    int x = KEYS.k[k].bx + i, y = KEYS.k[k].by + j;
                    if (x >= 0 && y >= 0 && x < c->w && y < c->h)
                        c->px[((size_t)y * c->w + x) * 4 + 3] =
                            KEYS.k[k].bg[((size_t)j * KEYS.k[k].bw + i) * 4 + 3];
                }
        }
}

const uint8_t *chassis_key_set(int which, float press, int *x, int *y, int *w, int *h) {
    if (which < 0 || which >= KEY_COUNT || !KEYS.k[which].placed || !KEYS.k[which].bg)
        return NULL;
    int bx = KEYS.k[which].bx, by = KEYS.k[which].by;
    int bw = KEYS.k[which].bw, bh = KEYS.k[which].bh;
    KEYS.patch = realloc(KEYS.patch, (size_t)bw * bh * 4);
    if (!KEYS.patch)
        return NULL;
    if (press < 0.0f)
        press = 0.0f;
    if (press > 1.0f)
        press = 1.0f;
    KEYS.k[which].depth = press;
    /* the plastic, from whichever keys' saved squares cover it: each saved
     * square is keyless, so any of them will do where it reaches */
    memcpy(KEYS.patch, KEYS.k[which].bg, (size_t)bw * bh * 4);
    canvas P;
    P.w = bw;
    P.h = bh;
    P.px = KEYS.patch;
    /* every key this square reaches, in the order the bake drew them */
    for (int k = 0; k < KEY_COUNT; k++) {
        if (!KEYS.k[k].placed || !KEYS.k[k].bg)
            continue;
        if (KEYS.k[k].bx >= bx + bw || KEYS.k[k].bx + KEYS.k[k].bw <= bx ||
            KEYS.k[k].by >= by + bh || KEYS.k[k].by + KEYS.k[k].bh <= by)
            continue;
        key_draw_at(&P, k, (float)bx, (float)by);
    }
    for (size_t n = 0; n < (size_t)bw * bh; n++)
        KEYS.patch[n * 4 + 3] = KEYS.k[which].bg[n * 4 + 3];
    *x = bx;
    *y = by;
    *w = bw;
    *h = bh;
    return KEYS.patch;
}

/* The OSD button, under the left pod, level with the knobs under the right
 * one: a control on its own, tied to nothing, so the monitor's three
 * controls make one row across the tube. */
void osd_button(float cx, float cy, float mm, float btn[4]) {
    keys_slot(KEY_OSD, cx, cy, WIDE_KEY_W * mm, WIDE_KEY_H * mm, mm, "OSD", WIDE_KEY_CAP * mm, 1,
              KEY_STYLE_CAP, btn);
}

/* A printed rule, axis-aligned and antialiased: x,y,w,h in px. */
static void printed_rule(canvas *c, float x, float y, float w, float h, int r, int g, int b) {
    for (int j = (int)y - 1; j <= (int)(y + h) + 1; j++)
        for (int i = (int)x - 1; i <= (int)(x + w) + 1; i++) {
            float ax = fminf((float)i + 1.0f, x + w) - fmaxf((float)i, x);
            float ay = fminf((float)j + 1.0f, y + h) - fmaxf((float)j, y);
            if (ax > 0.0f && ay > 0.0f)
                px_blend(c, i, j, r, g, b, fminf(1.0f, ax) * fminf(1.0f, ay));
        }
}

/* A quarter turn of printed line, `t` px thick, radius `rc`, centred on
 * (ax, ay): the quadrant with x on the `sx` side and y on the `sy` side
 * of the centre (-1 or +1 each). */
static void printed_corner(canvas *c, float ax, float ay, float rc, int sx, int sy, float t, int r,
                           int g, int b) {
    for (int j = (int)(ay - rc - t) - 1; j <= (int)(ay + rc + t) + 1; j++)
        for (int i = (int)(ax - rc - t) - 1; i <= (int)(ax + rc + t) + 1; i++) {
            float dx = (float)i + 0.5f - ax, dy = (float)j + 0.5f - ay;
            if (dx * (float)sx < 0.0f || dy * (float)sy < 0.0f)
                continue;
            float d = fabsf(sqrtf(dx * dx + dy * dy) - rc);
            float a = fminf(1.0f, fmaxf(0.0f, t * 0.5f + 0.5f - d));
            if (a > 0.0f)
                px_blend(c, i, j, r, g, b, a);
        }
}

/* The mouse key and its lamps, on the left pod under the holes, the OSD
 * key's opposite number.  CTRL+F10 printed over a MOUSE
 * key; from under the key a printed line drops and branches, the way a
 * hi-fi front of the eighties drew a selector's outputs, down to two LEDs
 * side by side with HOST and DXM under them.  Which lamp is lit says who
 * has the mouse; the shader lights it.  The key gives the mouse to the
 * machine - the only way it can go from a press, since while the machine
 * has it there is no pointer to press with - and CTRL+F10 takes it back.
 *
 * Centred on cx, and between y0 and y1; returns 0 and draws nothing when
 * that, or maxw across, cannot hold it.  Records the key's well in btn and
 * the two LEDs, HOST then DXM, in led_out. */
int mouse_lamps(canvas *c, float cx, float y0, float y1, float maxw, float mm, float btn[4],
                float led_out[2][4]) {
    const int INK_R = 88, INK_G = 83, INK_B = 72;     /* the printing: POWER's ink */
    const int LINE_R = 104, LINE_G = 99, LINE_B = 88; /* the lines, a shade paler */
    const float SQ = 0.90f;                           /* the legends, a little condensed */
    float lr = 1.2f * mm, hole = lr * 1.72f, lt = fmaxf(1.0f, 0.30f * mm);
    /* Helvetica Bold throughout, sized by its capitals, in POWER's weight */
    float cap_key = WIDE_KEY_CAP * mm, cap_lamp = 2.0f * mm, tr_lamp = cap_lamp * 0.08f;
    float tw_short = helv_width("CTRL+F10", cap_lamp, SQ, tr_lamp);
    float tw_host = helv_width("HOST", cap_lamp, SQ, tr_lamp);
    float kw = WIDE_KEY_W * mm, kh = WIDE_KEY_H * mm; /* the key */
    float lx = kw * 0.5f - 0.8f * mm;                 /* the lamps, under the key's ends */
    float g1 = 1.6f * mm, stem = 3.0f * mm, drop = 3.7f * mm - hole, g2 = 1.4f * mm;
    float h = cap_lamp + g1 + kh + stem + drop + 2.0f * hole + g2 + cap_lamp;
    float wide = fmaxf(fmaxf(tw_short, kw), 2.0f * lx + tw_host);
    if (h * 1.20f > y1 - y0 || wide > maxw)
        return 0;
    /* centred between the holes and the foot of the pod, then lowered a
     * little, as one group - but never past the foot */
    const float GROUP_DROP = 6.0f; /* mm */
    float top = (y0 + y1) * 0.5f - h * 0.5f + GROUP_DROP * mm;
    if (top + h > y1)
        top = y1 - h;
    int saved = canvas_grain;
    canvas_grain = 0;
    text_helv(c, cx - tw_short * 0.5f, top, "CTRL+F10", cap_lamp, SQ, tr_lamp, INK_R, INK_G, INK_B);
    float ky = top + cap_lamp + g1 + kh * 0.5f;
    /* the branch first, so the key's shadow falls over its root */
    float by = ky + kh * 0.5f + stem, lcy = by + drop + hole;
    float rc = 0.5f * mm; /* the bar turns down to each lamp round a small corner */
    printed_rule(c, cx - lt * 0.5f, ky, lt, kh * 0.5f + stem + lt * 0.5f, LINE_R, LINE_G, LINE_B);
    printed_rule(c, cx - lx + rc, by - lt * 0.5f, 2.0f * (lx - rc), lt, LINE_R, LINE_G, LINE_B);
    printed_corner(c, cx - lx + rc, by + rc, rc, -1, -1, lt, LINE_R, LINE_G, LINE_B);
    printed_corner(c, cx + lx - rc, by + rc, rc, 1, -1, lt, LINE_R, LINE_G, LINE_B);
    canvas_grain = saved;
    keys_slot(KEY_MOUSE, cx, ky, kw, kh, mm, "MOUSE", cap_key, 1, KEY_STYLE_CAP, btn);
    {
        const char *words[2] = {"HOST", "DXM"};
        for (int s = 0; s < 2; s++) {
            float lcx = cx + (s ? 1.0f : -1.0f) * lx;
            canvas_grain = 0;
            /* the drop runs into the lamp's own dark ring */
            printed_rule(c, lcx - lt * 0.5f, by + rc, lt, drop - rc + 0.2f * mm, LINE_R, LINE_G,
                         LINE_B);
            canvas_grain = saved;
            led(c, lcx, lcy, lr, 44, 18, 14); /* UNLIT, red */
            led_out[s][0] = lcx - lr;
            led_out[s][1] = lcy - lr;
            led_out[s][2] = lr * 2.0f;
            led_out[s][3] = lr * 2.0f;
            float tw = helv_width(words[s], cap_lamp, SQ, tr_lamp);
            canvas_grain = 0;
            text_helv(c, lcx - tw * 0.5f, lcy + hole + g2, words[s], cap_lamp, SQ, tr_lamp, INK_R,
                      INK_G, INK_B);
            canvas_grain = saved;
        }
    }
    return 1;
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

/* One cap of the button cluster: a flat cap on the module's plate (flat_key),
 * with its function printed on it as on the other keys - placed here, drawn
 * last with them.  Records its outline in out, for the mouse. */
static void cluster_cap(int which, float x, float y, float w, float h, float mm, const char *label,
                        float out[4]) {
    float cap = fminf(2.1f * mm, h * 0.46f);
    keys_slot(which, x + w * 0.5f, y + h * 0.5f, w, h, mm, label, cap, 0, KEY_STYLE_FLAT, out);
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
    /* The module is drawn four millimetres taller than the power cap's
     * height would make it, and everything in it scales with that; its
     * top edge is two millimetres above the cap's top line. */
    float k = (pw * 0.78f + 1.4f * mm + 4.0f * mm) / (pw * 0.78f + 1.4f * mm);
    float lip = 0.7f * mm * k, gap = 0.8f * mm * k, part = 1.1f * mm * k;
    float gh = pw * 0.78f * k, gw = SEG_WIN_W_MM * mm * k, kw = 13.5f * mm * k;
    /* the legend strip: an LED and a printed word, FPS over MHz, in the
     * face the case's other legends are set in - a silk-screened legend,
     * not moulding */
    float lcap = 1.6f * mm * k, ltr = lcap * 0.05f, lr = 1.0f * mm * k;
    float sw =
        (1.2f * mm + 1.0f * mm + 0.6f * mm) * k + lr * 2.0f +
        fmaxf(helv_width("FPS", lcap, KEY_SQUEEZE, ltr), helv_width("MHZ", lcap, KEY_SQUEEZE, ltr));
    float h = gh + 2.0f * lip, w = lip + sw + gw + part + kw + lip;
    float y = mid - pw * 0.39f - 2.0f * mm, rad = h * 0.09f;
    well_rect(c, x, y, w, h, rad, 0.45f * mm, 1.9f * mm);
    rrect(c, x, y, w, h, rad, 64, 61, 56, 0.80f, 0.92f);
    {
        const char *words[2] = {"FPS", "MHZ"};
        float pitch = gh * 0.40f, lcx = x + lip + 1.2f * mm * k + lr;
        float tx = lcx + lr + 1.0f * mm * k;
        for (int i = 0; i < 2; i++) {
            float cy = y + h * 0.5f + (i ? 0.5f : -0.5f) * pitch;
            led(c, lcx, cy, lr, 44, 18, 14); /* UNLIT, red */
            mode_led[i][0] = lcx - lr;
            mode_led[i][1] = cy - lr;
            mode_led[i][2] = lr * 2.0f;
            mode_led[i][3] = lr * 2.0f;
            text_helv(c, tx, cy - lcap * 0.5f, words[i], lcap, KEY_SQUEEZE, ltr, 196, 190, 176);
        }
    }
    turbo_glass(c, x + lip + sw, y + lip, gw, gh, seg);
    float kx = x + lip + sw + gw + part, ky = y + lip;
    float ch = (gh - gap) * 0.5f, cw = (kw - gap) * 0.5f;
    cluster_cap(KEY_MODE, kx, ky, kw, ch, mm, "MODE", btn[0]);
    cluster_cap(KEY_MINUS, kx, ky + ch + gap, cw, ch, mm, "-", btn[1]);
    cluster_cap(KEY_PLUS, kx + cw + gap, ky + ch + gap, cw, ch, mm, "+", btn[2]);
}
