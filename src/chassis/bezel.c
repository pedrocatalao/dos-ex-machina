/* bezel.c — the monitor's moulding around the glass: the aperture cut
 * in the same warped space the shader samples in, the dished band, and
 * the alpha mask that tells the shader how much each pixel faces the
 * tube. */
#include "internal.h"
#include "crt.h"

/* THE BEZEL.  Proportions measured off all-in-one-pc-dos.png:
 *
 *   bezel band  ~4.4% of picture height   (at the edge midpoints)
 *   OUTER corner ~3.1%                    (housing meets flat case)
 *   INNER corner ~4.8%                    (moulding meets the glass)
 *
 * The inner radius is LARGER than the outer one, so the band is thicker at
 * the corners than along the edges.  That is not a mistake — it is what a
 * tube in a housing actually looks like, and getting it backwards is what
 * made the previous bezel read as a sticker.
 *
 * The aperture is a rounded box evaluated in the SAME warped space the
 * shader samples in (crt.h), so the opening follows the glass curvature and
 * carries a realistic corner radius at the same time. */

/* signed distance to the aperture, in pixels; <=0 is glass */
static float aperture_sd(const dxm_layout *L, float px, float py, float rin) {
    return warped_rr_sd(L, px, py, rin, 0.0f, DXM_WARP);
}

/* the shoulder: same construction, gentler curvature */
static float shoulder_sd(const dxm_layout *L, float px, float py, float rmid, float bz) {
    return warped_rr_sd(L, px, py, rmid, bz, DXM_WARP * BEZEL_R_MID_WARP);
}

static void bezel(canvas *c, const dxm_layout *L, float bz, float rin, float rmid) {
    float m = bz * 1.20f; /* the band only - see below */
    for (int j = (int)(L->tube_y - m); j < (int)(L->tube_y + L->tube_h + m); j++)
        for (int i = (int)(L->tube_x - m); i < (int)(L->tube_x + L->tube_w + m); i++) {
            float d = aperture_sd(L, (float)i, (float)j, rin);
            if (d <= 0.0f)
                continue; /* glass: the shader owns it */
            float dout = shoulder_sd(L, (float)i, (float)j, rmid, bz);
            if (dout >= 0.0f)
                continue;                         /* past the band: flat face */
            float t = d / fmaxf(d - dout, 1e-3f); /* 0 at glass, 1 at the edge */
            float sh, spec = 0.0f;
            {
                /* The reveal is a WALL, not a dome.  Every point on it faces the
                 * light at the same angle, so its tone is essentially CONSTANT
                 * across the wall - dark all the way down on the shadowed side,
                 * light all the way down on the lit side.  Shading it as a ramp
                 * from the shoulder to the glass is what made it read as a raised
                 * rounded lip instead of a deep recess. */
                float gx = aperture_sd(L, (float)i + 1, (float)j, rin) -
                           aperture_sd(L, (float)i - 1, (float)j, rin);
                float gy = aperture_sd(L, (float)i, (float)j + 1, rin) -
                           aperture_sd(L, (float)i, (float)j - 1, rin);
                float gl = sqrtf(gx * gx + gy * gy);
                float lam = 0.0f;
                if (gl > 1e-4f) {
                    /* the wall climbs away from the glass, so it faces back
                     * toward the aperture: normal = -gradient */
                    float nx = -gx / gl, ny = -gy / gl;
                    lam = nx * LIGHT_X + ny * LIGHT_Y;
                }
                /* Measured off the reference, per side (glass -> shoulder):
                 *   top    : reveal LIT well above face tone (~180-206), with a
                 *            dark groove right at the shoulder
                 *   bottom : face tone, plus a bright catch just off the glass
                 *   sides  : darker than the face all the way up
                 * i.e. overhead light pours INTO the dish and lands on the wall
                 * you see along the top edge; the side walls rake away from it.
                 * That asymmetry is what makes the dish read deep. */
                float niy = (gl > 1e-4f) ? -gy / gl : 0.0f; /* inward normal, y */
                float nix = (gl > 1e-4f) ? -gx / gl : 0.0f;
                float k = 1.0f - t; /* 1 at the glass, 0 at shoulder */
                /* The wall's own shading, at FULL strength across the dish. */
                float dev = lam * 0.05f;
                dev += fmaxf(niy, 0.0f) * 0.13f; /* top wall: a little bounce */
                dev -= fabsf(nix) * 0.32f;       /* side walls rake away      */
                /* (a 'groove' term used to subtract light here wherever the
                 * wall faced down - i.e. precisely the top of the dish as it
                 * met the shoulder.  That was the dark band; the shoulder is a
                 * convex edge and should catch light there, not lose it.) */
                /* ROUNDED SHOULDER: only the last stretch before the flat face
                 * rolls off, so the corner is a fillet instead of a crease.  The
                 * dish keeps its depth - fading this across the whole wall (an
                 * earlier attempt) just flattened the recess. */
                float fu = (t - FILLET_START) / (1.0f - FILLET_START);
                if (fu < 0.0f)
                    fu = 0.0f;
                if (fu > 1.0f)
                    fu = 1.0f;
                float roll = 1.0f - fu * fu * (3.0f - 2.0f * fu);
                sh = 1.00f + dev * roll;
                /* contact shadow at the glass: narrow on the bottom (the ref
                 * jumps from 102 to 155 in one step there), fuller elsewhere */
                float cs = 0.30f * (1.0f - 0.62f * fmaxf(-niy, 0.0f));
                float kc = k * k * k * k; /* NARROW: the ref jumps from
                                                contact-dark to lit in one
                                                sample, not a long ramp */
                sh *= 1.0f - cs * kc;
                /* bottom: bright catch just off the glass (fillet facing up) */
                spec += fmaxf(-niy, 0.0f) * k * k * k * 0.30f;
            }
            float n = plastic_tex(i, j);
            float a = (d < 1.3f) ? d / 1.3f : 1.0f;
            px_blend(c, i, j, (int)(PLASTIC_R * (sh + n) + spec * 255.0f),
                     (int)(PLASTIC_G * (sh + n) + spec * 255.0f),
                     (int)(PLASTIC_B * (sh + n) + spec * 255.0f), a);
            /* The glass sits below the moulding, so there IS a contact line -
             * but it should be exactly that.  At 0.13 of the bezel it was ~10px
             * of near-black, which together with the contact-shadow term above
             * read as a dark blurry band rather than an edge. */
            float rw = fmaxf(1.5f, bz * 0.045f);
            if (d < rw)
                px_blend(c, i, j, 14, 14, 13, 0.50f * (1.0f - d / rw));
        }
    /* ---- the reveal SHOULDER ----
     * Where the dished reveal rolls back up onto the flat moulding face.
     * It is a convex fillet, so with the light above it takes a hard catch
     * along the top and drops into shadow along the bottom.  This is the
     * edge that gives the bezel its depth; putting the highlight out at the
     * chassis boundary instead was simply the wrong edge. */
    float fw = fmaxf(2.0f, bz * 0.30f); /* roll width: wider reads rounder, and
                                        past about half the band the moulding
                                        stops looking moulded and starts
                                        looking inflated */
    for (int j = (int)(L->tube_y - m); j < (int)(L->tube_y + L->tube_h + m); j++)
        for (int i = (int)(L->tube_x - m); i < (int)(L->tube_x + L->tube_w + m); i++) {
            if (aperture_sd(L, (float)i, (float)j, rin) <= 0.0f)
                continue;
            /* same shoulder the band is cut against, or the highlight sits
             * off the edge it is lighting */
            float dq = shoulder_sd(L, (float)i, (float)j, rmid, bz);
            /* A rounded edge STRADDLES the boundary.  Clamping this to dq<=0
             * meant the profile peaked exactly at the shoulder and stopped
             * dead - which on the side where that peak is a darkening showed
             * as a crisp line against the flat face. */
            float outw = fw * 0.55f;
            if (dq < -fw || dq > outw)
                continue;
            float gx = shoulder_sd(L, (float)i + 1, (float)j, rmid, bz) -
                       shoulder_sd(L, (float)i - 1, (float)j, rmid, bz);
            float gy = shoulder_sd(L, (float)i, (float)j + 1, rmid, bz) -
                       shoulder_sd(L, (float)i, (float)j - 1, rmid, bz);
            float gl = sqrtf(gx * gx + gy * gy);
            if (gl < 1e-4f)
                continue;
            /* A recessed shoulder catches the light on the side FACING the
             * light source - the BOTTOM shoulder, whose fillet tilts up toward
             * it.  The top shoulder shades itself.  So the shine follows the
             * inward normal, not the outward one. */
            float nx = -gx / gl, ny = -gy / gl; /* inward, toward the glass */
            float lam = nx * LIGHT_X + ny * LIGHT_Y;
            float prof = (dq < 0.0f) ? (1.0f + dq / fw)    /* into the dish  */
                                     : (1.0f - dq / outw); /* onto the face  */
            prof *= prof;
            /* Only ever ADD light.  The signed term darkened the shoulder on
             * the side facing away from the light, which showed as a shadow
             * band along the top of the transition - a fillet that catches a
             * highlight on one side should not carve a shadow on the other. */
            /* A convex fillet presents every angle to the light, so it picks
             * up a catch the whole way round - more on the side facing the
             * source, but never nothing. */
            float sp = fmaxf(lam, 0.0f);
            px_shade(c, i, j, 1.0f + (0.055f + sp * 0.10f) * prof, powf(sp, 9.0f) * prof * 0.10f);
        }
}

/* bezel band -> aperture -> glass.  The housing face itself is NOT
 * repainted: it is flush with the case, and giving it its own plate with
 * its own shade ramp left a visible tone step against the surrounding
 * plastic.  The base coat IS the housing face. */
void bezel_cut(canvas *c, dxm_layout *L, const chassis_geom *G) {
    float bz = G->bz;
    float hous = G->hous;
    float rin = L->tube_h * BEZEL_R_IN;
    L->aperture_r = rin; /* the shader cuts its glass to the same shape */
    bezel(c, L, bz, rin, L->tube_h * BEZEL_R_MID);
    for (int j = (int)(L->tube_y - hous); j < (int)(L->tube_y + L->tube_h + hous); j++)
        for (int i = (int)(L->tube_x - hous); i < (int)(L->tube_x + L->tube_w + hous); i++)
            if (aperture_sd(L, (float)i, (float)j, rin) <= 0.0f)
                px_set(c, i, j, 5, 6, 5);
}

/* Encode how strongly each pixel FACES THE TUBE into the alpha channel,
 * which is otherwise unused.  The reveal dish is angled toward the glass
 * and so catches far more of the screen's light than the flat front of
 * the case; the shader has no other way to tell them apart. */
void bezel_facing_alpha(canvas *c, const dxm_layout *L, int W, int H, const chassis_geom *G) {
    float bz = G->bz;
    {
        float rin2 = L->tube_h * BEZEL_R_IN, rmid2 = L->tube_h * BEZEL_R_MID;
        float reach = bz * 5.0f; /* how far the falloff carries */
        for (int j = 0; j < H; j++)
            for (int i = 0; i < W; i++) {
                uint8_t *p = c->px + ((size_t)j * W + i) * 4;
                /* EXACTLY the test bezel() uses to cut the dish: inside the
                 * shoulder and outside the aperture.  A uniform offset from the
                 * aperture is a different curve - the shoulder has its own
                 * radius and only part of the barrel - so the mask drifted off
                 * the moulding at the corners. */
                float din = aperture_sd(L, (float)i, (float)j, rin2);
                float dout = shoulder_sd(L, (float)i, (float)j, rmid2, bz);
                float f;
                if (din <= 0.0f)
                    f = 0.0f; /* glass          */
                else if (dout < 0.0f) {
                    /* Across the dish itself the light FADES OUTWARD: the wall
                     * is brightest where it meets the glass and turns away as it
                     * climbs to the shoulder.  A flat mask lit the whole dish
                     * evenly, which read as far too hot. */
                    float t = din / fmaxf(din - dout, 1e-3f); /* 0 glass, 1 edge */
                    float e = 1.0f - t;
                    f = 0.30f + 0.70f * e * e;
                } else {
                    float t = dout / reach; /* onto the face  */
                    f = (t >= 1.0f) ? 0.0f : (1.0f - t) * (1.0f - t) * 0.30f;
                }
                p[3] = (uint8_t)(f * 255.0f);
            }
    }
}
