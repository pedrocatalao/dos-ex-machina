/* chassis.c — the machine, drawn from parameters (SPEC §6.1).  No raster
 * art.  Rendered once into an RGBA buffer at startup / resolution change;
 * per frame it is one texture.
 *
 * This file is the assembly: it solves the shared geometry and calls the
 * parts in order.  The primitives are in canvas.c, the case body passes in
 * surface.c, the moulded parts in parts.c and marks.c, the monitor bezel in
 * bezel.c, the knobs in knobs.c, and the layout solve in layout.c. */
#include "internal.h"
#include "gen/mark.h"           /* only the mark's dimensions are used here */
#include "gen/corner_sticker.h" /* likewise */

/* ---- speaker columns, one each side of the tube ---- */
static void speaker_columns(canvas *c, dxm_layout *L, int W, int H, const chassis_geom *G,
                            int *knobs_placed) {
    float mm = G->mm, inset = G->inset, edge = G->edge, hous = G->hous;
    float gap_d = G->gap_d, gap_lo = G->gap_lo, gap_hi = G->gap_hi;
    { /* the block the columns were solved in; the braces stay so the
       * nested code reads as it did */
        float gl2 = L->tube_x - hous - (edge + inset * 0.45f); /* space per side */
        float gw = gl2 - inset * 0.35f;
        if (gw > inset * 0.5f) {
            float gh = L->tube_h * 0.5f;
            float gy = L->tube_y + (L->tube_h - gh) * 0.5f; /* centred on the tube */
            /* Each grille sits in a raised moulded POD - a tall rounded
             * pad standing a fraction proud of the flank, with the holes
             * punched through its middle.  This is how the real cases did
             * it: one moulded feature carrying the speaker, rather than
             * decoration applied around it.  It also gives the space above
             * and below the holes something to be - plateau face - instead
             * of leaving it bare or striping it. */
            float pw = gw * 0.88f, ph = L->tube_h * 0.94f;
            float py = L->tube_y + (L->tube_h - ph) * 0.5f;
            float prad = fminf(pw, ph) * 0.11f;
            float pxs[2] = {edge + inset * 0.45f + (gw - pw) * 0.5f,
                            (float)W - edge - inset * 0.45f - gw + (gw - pw) * 0.5f};
            for (int s2 = 0; s2 < 2; s2++) {
                float px0 = pxs[s2];
                /* the proud face catches marginally more of the key light */
                for (int j2 = (int)py; j2 < (int)(py + ph); j2++)
                    for (int i2 = (int)px0; i2 < (int)(px0 + pw); i2++)
                        if (rr_sd((float)i2, (float)j2, px0 + pw * 0.5f, py + ph * 0.5f, pw * 0.5f,
                                  ph * 0.5f, prad) < 0.0f)
                            px_shade(c, i2, j2, 1.022f, 0.0f);
                housing_edge(c, px0, py, pw, ph, prad, fmaxf(1.5f, (float)W * 0.0022f),
                             fmaxf(2.0f, (float)W * 0.0030f), 1, 0.85f);
                /* moulding bosses tucked into the pod corners */
                /* A real ejector boss is almost invisible EXCEPT at its
                 * rim - the flat top sits flush with the face around it.
                 * Darkening the whole disc instead read as a stamped spot,
                 * the more so because it came out the same radius as the
                 * pod corner it sat in. */
                {
                    float br = 1.6f * (float)H / 268.0f, in2 = prad * 0.86f;
                    float bp[4][2] = {
                        {in2, in2}, {pw - in2, in2}, {in2, ph - in2}, {pw - in2, ph - in2}};
                    for (int k = 0; k < 4; k++) {
                        float bx = px0 + bp[k][0], by = py + bp[k][1];
                        for (int j2 = (int)(by - br - 1); j2 <= (int)(by + br + 1); j2++)
                            for (int i2 = (int)(bx - br - 1); i2 <= (int)(bx + br + 1); i2++) {
                                float dx2 = i2 - bx, dy2 = j2 - by;
                                float dd = sqrtf(dx2 * dx2 + dy2 * dy2);
                                if (dd > br)
                                    continue;
                                float rim = 1.0f - fabsf(dd - br * 0.88f) / (br * 0.14f);
                                if (rim < 0.0f)
                                    rim = 0.0f;
                                px_shade(c, i2, j2, 1.0f - 0.005f + 0.030f * rim * (-dy2 / br),
                                         0.0f);
                            }
                    }
                }
            }
            grille_panel(c, edge + inset * 0.45f, gy, gw, gh, H * 0.019f);
            grille_panel(c, (float)W - edge - inset * 0.45f - gw, gy, gw, gh, H * 0.019f);
            /* The MULTIMEDIA sticker, on the RIGHT pod under the holes,
             * centred between them and the foot of the pod.  A FIXED
             * physical size, like every sticker: sized off the pod it was
             * 4 mm wide on a 4:3 screen and 59 mm on an ultrawide, because
             * the pods are whatever is left beside the tube.  A real sticker
             * is one size; when the pod cannot hold it, it is not there. */
            {
                float y0 = gy + gh * 0.94f, y1 = py + ph; /* holes end .. pod ends */
                multimedia_sticker(c, pxs[1] + pw * 0.5f, (y0 + y1) * 0.5f, 24.0f * mm, 2.0f * mm,
                                   pw * 0.80f, (y1 - y0) / 1.30f);
            }
            /* The monitor's two knobs, brightness and contrast, in the
             * strip under the right pod, above the parting to the base -
             * the picture's controls on the picture's half of the case.
             * Only their places are fixed here; the knobs themselves go on
             * LAST, after the wear and the yellowing, so the plastic saved
             * under each is the plastic around it.  A screen too short or
             * too narrow for them here gets them on the band instead. */
            {
                float kr = 3.9f * mm;
                float top = py + ph, strip = gap_lo - top;
                if (strip >= kr * 2.0f + 6.6f * mm && pw >= kr * 5.9f) {
                    float cx2 = pxs[1] + pw * 0.5f;
                    float ky = top + kr + 5.2f * mm;
                    float kx0 = cx2 - kr * 1.75f, kx1 = cx2 + kr * 1.75f;
                    knob_icons(c, kx0, ky + kr + 3.0f * mm, kx1, ky + kr + 3.0f * mm, 1.4f * mm);
                    knobs_slot(0, kx0, ky, kr);
                    knobs_slot(1, kx1, ky, kr);
                    *knobs_placed = 1;
                }
            }
            /* The dotted mark, engraved above the LEFT pod and centred on
             * it, halfway between the top of the display and the top of the
             * pod - and clear of the top parting and the pod both.  A fixed
             * physical size; a strip too short or a pod too narrow for it
             * goes without. */
            {
                float ew = 11.0f * mm, eh = ew * (float)CORNER_STICKER_HT / (float)CORNER_STICKER_W;
                float cy = py * 0.5f;
                float room = fminf(cy - (gap_hi + gap_d + 1.0f * mm), py - 1.0f * mm - cy);
                if (eh <= room * 2.0f && ew <= pw * 0.9f)
                    corner_engraving(c, pxs[0] + pw * 0.5f, cy, ew);
            }
        }
    }
}

/* ---- bottom band: power | turbo module | mark | vents | floppy ---- */
static void bottom_band(canvas *c, dxm_layout *L, int W, int H, const chassis_geom *G,
                        int *knobs_placed) {
    float mm = G->mm, inset = G->inset, edge = G->edge, hous = G->hous;
    float gap_d = G->gap_d, gap_lo = G->gap_lo;
    float band_y = L->tube_y + L->tube_h + hous + inset * 0.22f;
    float band_h = (float)H - inset * 0.55f - band_y;
    if (band_h > inset * 0.8f) {
        /* The two mouldings part here.  It runs the FULL width, across the
         * turned-away side strips as well: the parting between two parts
         * goes right round the case, and stopping it at the front face made
         * it read as a groove cut into one moulding instead. */
        /* The parting runs between the two walls, not across the sides:
         * out there the base's top surface meets the monitor's set-back
         * side directly, as the ledge, and a groove crossing that would
         * cut through geometry that has no groove in it. */
        panel_gap(c, edge, gap_lo, (float)W - 2.0f * edge, gap_d);
        surface_monitor_recess(c, W, H, G);
        float mid = band_y + band_h * 0.46f;

        /* power button + status LED at the left end of the band, with the
         * same space either side of it - three quarters of a cap's width -
         * between the case's left edge and the turbo module: the display's
         * glass and its three keys in one well */
        float pw = 16.0f * mm;   /* a 16mm power cap */
        float pgap = pw * 0.75f; /* the space either side of it */
        float px0 = edge + pgap;
        float pmid = mid + 1.5f * mm;
        power_button(c, px0, pw, pmid, mm, band_h, L);
        float sx = px0 + pw + pgap;
        turbo_module(c, sx, pw, pmid, mm, L->seg, L->btn, L->mode_led);
        float mod_r = L->btn[2][0] + L->btn[2][2] + 0.7f * mm; /* the module's right edge */
        float left_r = mod_r; /* the right edge of everything at the left end */

        /* the knobs' fallback: on the band beside the module, for a
         * screen whose pods have no room under them */
        if (!*knobs_placed) {
            float kr = 3.9f * mm;
            float kx = mod_r + inset * 0.85f + kr;
            float ky = mid;
            knob_icons(c, kx, ky + kr + 3.0f * mm, kx + kr * 3.0f, ky + kr + 3.0f * mm, 1.4f * mm);
            /* only their places for now: the knobs themselves go on LAST,
             * after the wear and the yellowing, so the plastic saved under
             * each one is the plastic around it - otherwise a turned knob
             * came back on a square of cleaner case */
            knobs_slot(0, kx, ky, kr);
            knobs_slot(1, kx + kr * 3.0f, ky, kr);
            *knobs_placed = 1;
            left_r = kx + kr * 4.0f;
        }

        /* floppy drive: a real 3.5" face is 101.6 x 25.4 mm, centred
         * vertically between the divider ridge and the case bottom */
        float fh = 25.4f * mm, fw2 = 101.6f * mm;
        float fx = (float)W - edge - inset * 0.65f - fw2;
        float fmid = ((band_y - inset * 0.26f) + (float)H) * 0.5f;

        /* The maker's mark: the DXM wordmark engraved on the centre line of
         * the base, level with the controls, with its colour laid in the
         * cut.  A fixed physical size, drawn only where it clears the
         * controls on its left and the drive on its right. */
        {
            float mw = 17.0f * mm, cx = (float)W * 0.5f;
            if (cx - mw * 0.5f > left_r + inset * 0.3f && cx + mw * 0.5f < fx - inset * 0.3f)
                engrave_mark(c, cx, mid + 1.0f * mm, mw, 0.75f);
        }

        /* Vent cuts along the foot of the band, confined to the middle
         * fifth of the case, under the mark.  A run all the way across read
         * as a decorative band; a short group on the centre line reads as
         * what it is - ducting put where the airflow is. */
        {
            float vy0 = pmid + pw * 0.39f + fmaxf(4.0f, band_h * 0.09f) + 2.7f * mm + inset * 0.10f;
            float vy1 = (float)H - inset * 0.28f;
            float vx0 = (float)W * 0.40f, vx1 = (float)W * 0.60f;
            /* the run hangs from its BOTTOM edge: shortening it from the top
             * keeps it sitting on the foot of the case, which is where a cut
             * that vents the floor of the machine belongs */
            float avail = vy1 - vy0, vspan = vx1 - vx0;
            float vh = avail * 0.86f;
            vy0 = vy1 - vh;
            /* the test is on the room AVAILABLE, not on the shortened run -
             * testing the latter is what silently deleted the whole row */
            if (avail > 1.8f * mm && vspan > 8.0f * mm) {
                float vw = fmaxf(2.0f, 1.15f * mm);
                float pitch = vw * 2.7f;
                int n = (int)((vspan + pitch - vw) / pitch);
                float x2 = vx0 + (vspan - ((n - 1) * pitch + vw)) * 0.5f;
                for (int k = 0; k < n; k++)
                    vent_slot(c, x2 + k * pitch, vy0, vw, vh);
            }
        }

        floppy_drive(c, fx, fmid - fh * 0.5f, fw2, fh, L->fdd_led);
    }
}

uint8_t *chassis_render(dxm_layout *L, int W, int H) {
    canvas C;
    C.w = W;
    C.h = H;
    C.px = calloc((size_t)W * H, 4);
    if (!C.px)
        return NULL;
    canvas *c = &C;
    canvas_lbl = fmaxf(1.0f, (float)H / 760.0f);
    float inset = H * 0.052f, edge = W * 0.024f;
    /* ONE physical scale for everything that has a real-world size - the
     * drives, the LEDs, the stickers.  Tied to the DISPLAY height, not to
     * any band or pod, so a wide screen gets more case around the same
     * objects rather than bigger objects. */
    float mm = H / 268.0f;
    float bz = L->tube_h * BEZEL_BAND; /* measured off the reference */
    float hous = L->tube_h * BEZEL_HOUSING;
    /* the two partings, and where the upper one sits - the top band is the
     * case above it, so both have to be solved from the same numbers */
    float gap_d = fmaxf(3.0f, 2.0f * (float)H / 268.0f);
    float gap_lo = L->tube_y + L->tube_h + hous + inset * 0.22f - inset * 0.30f;
    float gap_hi = L->tube_y - (gap_lo - (L->tube_y + L->tube_h)) - gap_d;
    int knobs_placed = 0; /* under the right pod, or on the band */
    chassis_geom G = {mm, inset, edge, bz, hous, gap_d, gap_lo, gap_hi};

    surface_base(c, W, H);
    surface_top_roll(c, W, &G);
    surface_edges(c, W, H, &G);
    bezel_cut(c, L, &G);
    speaker_columns(c, L, W, H, &G, &knobs_placed);
    bottom_band(c, L, W, H, &G, &knobs_placed);
    surface_side_louvres(c, W, H, &G);
    surface_moulding_traces(c, W, H, &G);
    surface_wear(c, W, H);
    surface_finish(c, W, H);
    bezel_facing_alpha(c, L, W, H, &G);
    /* the knobs, over the finished case */
    knobs_draw(c);
    knobs_layout(L);
    return C.px;
}
