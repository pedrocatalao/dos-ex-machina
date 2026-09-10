/* surface.c — the case body as one object: the base coat, the top roll,
 * the side strips, the monitor's set-back sides, the moulding traces,
 * thirty years of wear, and the finishing light. */
#include "internal.h"

/* the base coat: the whole case in its plastic, lit top to bottom */
void surface_base(canvas *c, int W, int H) {
    rrect(c, 0, 0, (float)W, (float)H, 0.0f, PLASTIC_R, PLASTIC_G, PLASTIC_B, 1.03f, 0.90f);
}

/* The top band rolls BACK: above the upper parting the case is no longer
 * a flat face, it is the front turning over into the top of the box.  So
 * it takes a catch just past the roll - where the surface is tilted up
 * toward the key light - and then falls away into shade as it turns out
 * of sight, exactly as the side strips do going outward.  Leaving it
 * flat is what made the machine read as a panel rather than a box. */
void surface_top_roll(canvas *c, int W, const chassis_geom *G) {
    float gap_hi = G->gap_hi;
    if (gap_hi > 4.0f) {
        for (int j2 = 0; j2 < (int)gap_hi; j2++) {
            /* u: 0 at the parting, 1 at the very top edge of the machine */
            float u = 1.0f - (float)j2 / gap_hi;
            /* A pure falloff, with no separate catch band.  The highlight that
             * used to sit at u=0.13 belonged to the parting that ran along the
             * bottom of this roll; with that gone it had nothing to describe
             * and simply read as a bright line drawn across the case. */
            float sh = 0.46f + 0.54f * cosf(u * 1.24f); /* 1.0 exactly at u=0, so
                                                    it meets the face below
                                                    with no step */
            for (int i2 = 0; i2 < W; i2++) {
                uint8_t *q = c->px + ((size_t)j2 * c->w + i2) * 4;
                for (int k = 0; k < 3; k++) {
                    float v = q[k] * sh;
                    q[k] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
                }
            }
        }
    }
}

/* Edge strips: the case turning away from the viewer.  They are the
 * SIDES of the box, so they fall off in shade toward each outer edge -
 * darker plastic, never black - which is what reads as roundness.  A
 * thin lit line sits where the side meets the front face, the way a
 * moulded corner catches the light. */
void surface_edges(canvas *c, int W, int H, const chassis_geom *G) {
    float edge = G->edge;
    float gap_d = G->gap_d;
    float gap_lo = G->gap_lo;
    {
        for (int side = 0; side < 2; side++) {
            float x0 = side ? (float)W - edge : 0.0f;
            for (int i2 = 0; i2 < (int)edge; i2++) {
                /* u: 0 at the front face, 1 at the outer edge of the machine */
                float u = side ? (float)i2 / edge : 1.0f - (float)i2 / edge;
                /* cosine falloff - a cylinder turning away from the light */
                float sh = 0.42f + 0.58f * cosf(u * 1.28f);
                /* the corner catches a highlight just off the face - on the
                 * BASE.  The monitor's sides are set back behind a wall (see
                 * below), and a wall has no rolled corner to catch anything. */
                float lit = expf(-((u - 0.10f) * (u - 0.10f)) / 0.0075f) * 0.16f;
                int x = (int)x0 + i2;
                for (int j2 = 0; j2 < H; j2++) {
                    float l =
                        ((float)j2 >= gap_lo + gap_d * 0.5f) ? lit : 0.0f; /* from the floor down */
                    uint8_t *q = c->px + ((size_t)j2 * c->w + x) * 4;
                    for (int k = 0; k < 3; k++) {
                        float v = q[k] * sh + l * 255.0f;
                        q[k] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
                    }
                }
            }
        }
        /* the base's corner seam, and the monitor's step wall above it: a
         * plain dark line, the wall seen edge-on */
        /* the wall runs down to the parting's floor, where the base's own
         * corner seam takes over - the two meet on the same row */
        {
            float yfloor = gap_lo + gap_d * 0.5f;
            float cr = FACE_R_TOP * (float)H / 268.0f;
            /* the seam's light flank lands on the front face on both
             * corners: past the line on the left, before it on the right */
            /* The seam is CENTRED on the wall above it, which is drawn
             * either side of `edge`: starting the seam at `edge` set it a
             * pixel or two to the right of the wall, and the monitor read
             * as nudged left of the base. */
            float sw2 = fmaxf(1.0f, W * 0.0012f), s0 = floorf(sw2 * 0.5f - 0.5f);
            seam(c, edge - s0, yfloor, (float)H - yfloor, 1, sw2, 1);
            seam(c, (float)W - edge - s0, yfloor, (float)H - yfloor, 1, sw2, -1);
            float ww = fmaxf(1.5f, W * 0.0011f);
            for (int side = 0; side < 2; side++) {
                float xw = side ? (float)W - edge : edge;
                for (int j2 = (int)ceilf(cr); j2 < (int)yfloor;
                     j2++) /* the arc owns the row above */
                    for (int i2 = (int)(xw - ww - 1); i2 <= (int)(xw + ww + 1); i2++) {
                        float d = fabsf((float)i2 + 0.5f - xw) / ww;
                        if (d > 1.0f)
                            continue;
                        px_shade(c, i2, j2, 1.0f - 0.42f * (1.0f - d * d), 0.0f);
                    }
                /* the front face's own corner catches the same light line
                 * along the wall that the base's seam has along it below:
                 * the one edge, continued up the monitor */
                int s0i = (int)(xw - s0); /* where the seam below starts */
                int fx0 = side ? s0i - (int)sw2 : s0i + (int)sw2;
                for (int t = 0; t < (int)sw2; t++)
                    for (int j2 = (int)ceilf(cr); j2 < (int)yfloor; j2++)
                        px_blend(c, fx0 + t, j2, 255, 252, 244, 0.24f);
            }
        }
    }
}

/* Along the MONITOR only - from the top down to the parting - the
 * sides are set back from the front face: the display shell is a
 * touch narrower than the base it stands on.  Applied AFTER the
 * parting is cut, so the groove's lit lip is shaded on the recessed
 * strip too, rather than running bright across it.  What sells a step back is
 * not the strip being darker but the occlusion beside the wall: the
 * floor of the recess falls into shade right next to the step and
 * comes back out over a couple of millimetres.  The base below and
 * the top band above keep their edge flush. */
void surface_monitor_recess(canvas *c, int W, int H, const chassis_geom *G) {
    float edge = G->edge;
    float gap_d = G->gap_d;
    float gap_lo = G->gap_lo;
    {
        float mmr = (float)H / 268.0f;
        /* from the very top of the machine - the roll included, since the
         * shell is narrower all the way up - down to the lower parting,
         * where it stops dead: the groove is the step's own edge */
        float y1 = gap_lo + gap_d * 0.5f; /* down to the parting's floor */
        float occ = 4.0f * mmr;
        float cr = FACE_R_TOP * mmr;
        for (int side = 0; side < 2; side++) {
            float x0 = side ? (float)W - edge : 0.0f;
            float ccx = side ? (float)W - edge - cr : edge + cr;
            for (int i2 = 0; i2 < (int)edge; i2++) {
                int x = (int)x0 + i2;
                for (int j2 = 0; j2 < (int)y1; j2++) {
                    /* distance from the step wall: the straight wall below the
                     * corner's start, the arc above it - so the crease beside
                     * the wall bends away with the wall instead of running on
                     * up to the top edge */
                    float dw;
                    if ((float)j2 < cr) {
                        float dx = (float)x + 0.5f - ccx, dy = (float)j2 + 0.5f - cr;
                        dw = sqrtf(dx * dx + dy * dy) - cr;
                    } else
                        dw = side ? (float)i2 : edge - 1.0f - (float)i2;
                    if (dw < 0.0f)
                        dw = 0.0f;
                    float t = dw / occ;
                    if (t > 1.0f)
                        t = 1.0f;
                    float m = 0.90f * (1.0f - 0.24f * (1.0f - t) * (1.0f - t));
                    uint8_t *q = c->px + ((size_t)j2 * c->w + x) * 4;
                    for (int k = 0; k < 3; k++) {
                        float v = q[k] * m;
                        q[k] = (uint8_t)(v > 255 ? 255 : v);
                    }
                }
            }
        }
        /* The front face's top corners are rounded off, and where the face
         * curves away the set-back side shows through: the same floor and
         * the same wall, now following the arc.  Without the side strips
         * these would be the corners of the machine. */
        {
            float ww = fmaxf(1.5f, W * 0.0011f);
            for (int side = 0; side < 2; side++) {
                float ccx = side ? (float)W - edge - cr : edge + cr, ccy = cr;
                for (int j2 = 0; j2 < (int)cr + 2; j2++)
                    for (int k2 = -(int)ww - 1; k2 < (int)cr + 2; k2++) {
                        int x = side ? (int)((float)W - edge) - 1 - k2 : (int)edge + k2;
                        float dx = (float)x + 0.5f - ccx, dy = (float)j2 + 0.5f - ccy;
                        if ((side ? -dx : dx) > 0.0f || dy > 0.0f)
                            continue;                            /* the quadrant */
                        float d = sqrtf(dx * dx + dy * dy) - cr; /* >0: past the face */
                        if (d <= -ww - 1.0f)
                            continue;
                        uint8_t *q = c->px + ((size_t)j2 * c->w + x) * 4;
                        /* the floor only inside the face's column - the strip
                         * beside it was shaded by the recess pass already; the
                         * wall's line, though, needs its full width either side */
                        if (d > 0.0f && k2 >= 0) {
                            /* the recess floor, shaded by its distance from the wall */
                            float t = d / occ;
                            if (t > 1.0f)
                                t = 1.0f;
                            float m = 0.90f * (1.0f - 0.24f * (1.0f - t) * (1.0f - t));
                            float cov = fminf(1.0f, d);
                            m = 1.0f + (m - 1.0f) * cov;
                            for (int k = 0; k < 3; k++) {
                                float v = q[k] * m;
                                q[k] = (uint8_t)(v > 255 ? 255 : v);
                            }
                        }
                        /* the wall, along the arc: edge-on and dark where it is
                         * still vertical, turning to face the light - a highlight
                         * - as it comes round to horizontal along the top */
                        float a = fabsf(d) / ww;
                        if (a < 1.0f) {
                            float up = -dy / cr;
                            if (up < 0.0f)
                                up = 0.0f;
                            if (up > 1.0f)
                                up = 1.0f;
                            float f = (1.0f - a * a);
                            px_shade(c, x, j2, 1.0f - 0.42f * f * (1.0f - up) + 0.16f * f * up,
                                     0.05f * f * up);
                        }
                    }
            }
        }

        /* The ledge between them.  Where the flush base meets the set-back
         * monitor side there is a horizontal surface - the top of the base's
         * wall - and from a little above it shows as a thin lit shelf along
         * the foot of the recess: brightest at its front edge, falling into
         * a crease where it meets the monitor's wall behind. */
        { /* the same rows and the same profile as the parting's rising side,
           * so its highlight runs out to the edges as one line */
            for (int side = 0; side < 2; side++) {
                float x0 = side ? (float)W - edge : 0.0f;
                for (int i2 = 0; i2 < (int)edge; i2++) {
                    int x = (int)x0 + i2;
                    /* the crease beside the wall carries on into the back of the
                     * ledge and fades out toward its lip, so the floor and the
                     * ledge meet on the same tone instead of stepping */
                    float dw = side ? (float)i2 : edge - 1.0f - (float)i2;
                    float tw = dw / occ;
                    if (tw > 1.0f)
                        tw = 1.0f;
                    float crease = 0.24f * (1.0f - tw) * (1.0f - tw);
                    for (int j2 = (int)(gap_lo + gap_d * 0.5f); j2 < (int)(gap_lo + gap_d) + 1;
                         j2++) {
                        float t = ((float)j2 + 0.5f - gap_lo) / gap_d; /* 0.5 floor .. 1 lip */
                        if (t > 1.0f)
                            break;
                        float prof = 0.5f - 0.5f * cosf(t * 6.28318f);
                        float lam = -sinf(t * 6.28318f) * 0.91f; /* rising: lit */
                        float back = 1.0f - (t - 0.5f) * 2.0f;   /* 1 at the floor, 0 at the lip */
                        float m = (1.0f + lam * 0.40f - prof * 0.15f) * (1.0f - crease * back) *
                                  (0.90f + 0.10f * (1.0f - back));
                        uint8_t *q = c->px + ((size_t)j2 * c->w + x) * 4;
                        for (int k = 0; k < 3; k++) {
                            float v = q[k] * m;
                            q[k] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
                        }
                    }
                }
            }
        }
    }
}

/* ---- moulded marks and manufacturing traces -------------------------
 * Text pressed INTO the tool, not printed on the part, plus the traces
 * every injection moulding carries: the parting line where the two tool
 * halves met, and the ejector-pin circles that pushed the part out. */
void surface_moulding_traces(canvas *c, int W, int H, const chassis_geom *G) {
    float inset = G->inset;
    float edge = G->edge;
    {
        float mmu = (float)H / 268.0f; /* same mm as the band */
        float ms = fmaxf(1.0f, canvas_lbl * 0.72f);
        /* the compliance block, low and to the left, where nobody looks */
        moulded_text(c, edge + inset * 0.9f, (float)H - inset * 0.95f + 9.0f * ms + 1.5f * mmu,
                     "MADE IN PORTUGAL", ms, 0);

        /* ejector pin marks: faint discs on the broad flat areas */
        {
            float pr2 = 3.6f * mmu;
            float pts[6][2] = {{0.20f, 0.16f}, {0.20f, 0.86f}, {0.80f, 0.16f},
                               {0.80f, 0.86f}, {0.50f, 0.07f}, {0.34f, 0.93f}};
            for (int k = 0; k < 6; k++) {
                float ex2 = edge + (W - 2 * edge) * pts[k][0], ey2 = H * pts[k][1];
                for (int j2 = (int)(ey2 - pr2 - 1); j2 <= (int)(ey2 + pr2 + 1); j2++)
                    for (int i2 = (int)(ex2 - pr2 - 1); i2 <= (int)(ex2 + pr2 + 1); i2++) {
                        float dx2 = i2 - ex2, dy2 = j2 - ey2, dd = sqrtf(dx2 * dx2 + dy2 * dy2);
                        if (dd > pr2)
                            continue;
                        /* very slightly proud, so it catches the light at its rim */
                        float rim = 1.0f - fabsf(dd - pr2 * 0.86f) / (pr2 * 0.16f);
                        if (rim < 0.0f)
                            rim = 0.0f;
                        px_shade(c, i2, j2, 1.0f - 0.018f + 0.030f * rim * (-dy2 / pr2), 0.0f);
                    }
            }
        }
    }
}

/* ---- wear ------------------------------------------------------
 * Thirty years of being touched.  Scratches on plastic SCATTER light,
 * so they read brighter than the surface around them, and they are not
 * scattered evenly: they gather along the front edges, around the
 * drive slot and on the buttons - wherever hands and diskettes went.
 * Uniform scratching over the whole case looks like film grain; it is
 * the CLUSTERING that tells the story. */
void surface_wear(canvas *c, int W, int H) {
    {
        float mmu = (float)H / 268.0f;
        /* places a hand actually goes, in normalised case coordinates */
        float hot[4][3] = {
            {0.50f, 0.86f, 1.00f}, /* the front band, most handled     */
            {0.86f, 0.86f, 0.85f}, /* around the floppy               */
            {0.22f, 0.86f, 0.55f}, /* badge / power end               */
            {0.50f, 0.06f, 0.40f}, /* the top edge, where it is lifted */
        };
        for (int n = 0; n < 230; n++) {
            /* pick a hotspot, then jitter around it */
            float pick = hash2(n, 17, 31);
            int hs = (pick < 0.42f) ? 0 : (pick < 0.68f) ? 1 : (pick < 0.88f) ? 2 : 3;
            float sx2 = (hash2(n, 3, 7) - 0.5f), sy2 = (hash2(n, 5, 11) - 0.5f);
            float spread = 0.16f + 0.22f * hash2(n, 9, 13);
            float cx2 = (hot[hs][0] + sx2 * spread) * (float)W;
            float cy2 = (hot[hs][1] + sy2 * spread * 0.55f) * (float)H;
            if (cx2 < 0 || cy2 < 0 || cx2 >= W || cy2 >= H)
                continue;
            /* mostly shallow near-horizontal drags, a few random nicks */
            float ang = (hash2(n, 21, 23) < 0.72f) ? (hash2(n, 15, 19) - 0.5f) * 0.55f
                                                   : (hash2(n, 15, 19) * 6.28318f);
            float len = (1.5f + 22.0f * hash2(n, 27, 29) * hash2(n, 28, 37)) * mmu;
            /* Scratches are SPECULAR - they scatter light back at you - so
             * they have to be added, not just multiplied.  A multiplier alone
             * gets scaled down by the ambient term in the shader until it
             * disappears, which is why the first pass was invisible. */
            float amp = hot[hs][2] * (0.10f + 0.22f * hash2(n, 33, 41));
            float ca = cosf(ang), sa = sinf(ang);
            int steps = (int)fmaxf(2.0f, len);
            for (int t2 = 0; t2 < steps; t2++) {
                float f = (float)t2 / steps;
                /* fade the ends so nothing starts or stops abruptly */
                float e = sinf(f * 3.14159f);
                int i2 = (int)(cx2 + (f - 0.5f) * len * ca);
                int j2 = (int)(cy2 + (f - 0.5f) * len * sa);
                /* a scratch is a bright scatter with a faint dark edge */
                px_shade(c, i2, j2, 1.0f + amp * e * 0.50f, amp * e * 0.13f);
                px_shade(c, i2, j2 + 1, 1.0f - amp * e * 0.20f, 0.0f);
            }
        }
        /* scuffs: broad patches where the sheen has been rubbed dull */
        for (int j = 0; j < H; j++)
            for (int i = 0; i < W; i++) {
                float s2 = vnoise((float)i * 0.0018f, (float)j * 0.0021f, 23);
                float bias = ((float)j / (float)H); /* worse low down */
                float scuff = fmaxf(0.0f, s2 - 0.58f) * bias * bias * 0.20f;
                if (scuff > 0.0005f)
                    px_shade(c, i, j, 1.0f - scuff, 0.0f);
            }
    }
}

/* ---- finishing pass: what makes it a photographed object ------------
 * Everything above draws PARTS.  This pass treats the finished case as
 * one object: a light with a POSITION (so illumination falls across the
 * panel instead of being uniform), a grazing-angle sheen, and thirty
 * years of uneven yellowing with dust settled into the recesses.  A
 * purely directional light lights every point on a flat face equally,
 * which is exactly what reads as a cartoon cutout. */
void surface_finish(canvas *c, int W, int H) {
    {
        float lx = (float)W * 0.16f, ly = -(float)H * 0.40f; /* key, above and left */
        float inv = 1.0f / (float)(W > H ? W : H);
        for (int j = 0; j < H; j++) {
            for (int i = 0; i < W; i++) {
                uint8_t *p = c->px + ((size_t)j * W + i) * 4;
                float dx = ((float)i - lx) * inv, dy = ((float)j - ly) * inv;
                float d2 = dx * dx + dy * dy;
                /* inverse-square-ish falloff across the whole panel */
                float key = 1.0f / (1.0f + 1.35f * d2);
                /* broad specular lobe - plastic is satin, not matte */
                float sheen = expf(-d2 * 2.6f) * 0.05f;
                /* grazing sheen: the case brightens toward its outer edges */
                float ex = fabsf(((float)i / (float)W) - 0.5f) * 2.0f;
                float ey = fabsf(((float)j / (float)H) - 0.5f) * 2.0f;
                float graze = powf(fmaxf(ex, ey), 3.5f) * 0.07f;
                /* uneven yellowing: large, slow, and never uniform */
                float age = vnoise((float)i * 0.0032f, (float)j * 0.0032f, 11);
                float age2 = vnoise((float)i * 0.0009f, (float)j * 0.0011f, 12);
                float yellow = (0.45f * age + 0.55f * age2);
                /* dust: settles low and in the corners */
                float low = (float)j / (float)H;
                float dust = vnoise((float)i * 0.006f, (float)j * 0.006f, 13) * low * low * 0.05f;

                float m = 0.72f + 0.26f * key;
                for (int k = 0; k < 3; k++) {
                    float v = p[k] * m + (sheen + graze) * 255.0f;
                    /* warm the reds, hold the greens, pull the blues down */
                    float tint = (k == 0)   ? 1.0f + 0.055f * yellow
                                 : (k == 1) ? 1.0f + 0.022f * yellow
                                            : 1.0f - 0.075f * yellow;
                    v *= tint;
                    v *= 1.0f - dust;
                    p[k] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
                }
            }
        }
    }
}

/* The louvres down the monitor's sides: a single column of thin horizontal
 * vent slots on each set-back side strip, the length of the monitor
 * between the two partings, the way a monitor shell breathed through its
 * flanks.  Cut with the same slot the foot vents use, so the trough and
 * the lips light the same way. */
void surface_side_louvres(canvas *c, int W, int H, const chassis_geom *G) {
    (void)H;
    float mm = G->mm, edge = G->edge;
    float y0 = G->gap_hi + G->gap_d + 3.0f * mm, y1 = G->gap_lo - 7.0f * mm;
    /* Each slot runs from a little inside the wall right out through the
     * edge of the picture - the cut goes round the corner of the shell,
     * so its outer end is never seen - with near-square ends. */
    float sh = fmaxf(2.0f, 1.6f * mm), pitch = 4.0f * mm;
    float in = edge * 0.62f, over = 3.0f * mm, sw = in + over;
    if (y1 - y0 < pitch * 3.0f || in < 3.0f)
        return;
    int n = (int)((y1 - y0 - sh) / pitch) + 1;
    float ys = y0 + (y1 - y0 - ((n - 1) * pitch + sh)) * 0.5f;
    for (int side = 0; side < 2; side++) {
        float x = side ? (float)W - in : -over;
        for (int k = 0; k < n; k++)
            louvre_slot(c, x, ys + k * pitch, sw, sh, sh * 0.18f);
    }
}
