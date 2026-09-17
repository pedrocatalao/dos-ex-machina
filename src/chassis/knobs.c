/* knobs.c — the two rotary controls: drawn last over the finished plastic,
 * like the keys, and redrawn when turned. */
#include "internal.h"

/* The knobs: where they are, and the plastic under each one, kept so a
 * turn redraws only the knob and not the machine. */
static struct {
    float place[2][3]; /* centre x, y and radius, output px */
    uint8_t *bg[2];    /* the square beneath, RGBA */
    int bx[2], by[2], bs[2];
    uint8_t *patch;
    float pos[2]; /* where each is turned to, as last drawn */
    int cw, ch;   /* the case they sit on, for its finish */
    float ox, oy; /* where the canvas being drawn on sits on that case */
} K;

/* A knob is drawn after the case's finish, so it takes the finish itself -
 * the yellowing and the key light where it sits - or it reads as a part
 * from a cleaner machine than the one it is fitted to. */
static void finished(canvas *c, int i, int j, float r, float g, float b, float a) {
    float rgb[3] = {r, g, b};
    if (K.cw > 0)
        finish_rgb((float)i + K.ox, (float)j + K.oy, K.cw, K.ch, rgb);
    px_blend(c, i, j, (int)rgb[0], (int)rgb[1], (int)rgb[2], a);
}

/* The chamfer and the knurled side are the knobs' plastic (params.h) at
 * the tones in rotary(), times this: the tones were set against the case
 * colour at the plastic's old 0.74, and the finish takes some light away,
 * so this puts both back. */
#define TONE_GAIN (1.09f / 0.74f)

/* The highlight on the knob's face: how strong, where 0.16 was the old
 * domed face's full one */
#define FACE_SHEEN 0.05f

/* How light the whole knob is - its knurled body, its chamfer ring and its
 * face together, 1 as drawn.  KNOB_FACE (params.h) moves the face alone. */
#define KNOB_LEVEL 0.85f

/* Where a knob goes.  The knob itself is drawn by knobs_draw, last, so the
 * plastic saved under it is the finished case. */
void knobs_slot(int which, float cx, float cy, float r) {
    K.place[which][0] = cx;
    K.place[which][1] = cy;
    K.place[which][2] = r;
}

/* A rotary control: a short cylinder of dark plastic standing off the
 * band, knurled round its edge so fingers can turn it, with a flat face
 * carrying a white index line and the stains of the hands that turned it.
 * The knurl and the lit chamfer are what make it read as round - a plain
 * disc would be a button.  pos 0..1 runs the index through 270 degrees, from seven
 * o'clock round to five. */
static void rotary(canvas *c, float cx, float cy, float r, float pos) {
    const float PI = 3.14159265f;
    float ang = (-135.0f + 270.0f * pos) * PI / 180.0f; /* 0 = twelve o'clock */
    /* Seen a little from above, the way the eject button is: the face is
     * an ellipse, squashed by the viewing angle, and above it shows a band
     * of the cylinder's side - the top of the knob, foreshortened - which
     * faces the key light and is the brightest thing here.  Below the
     * face the side is hidden and a contact shadow takes over.  It is the
     * same knob as before; only the camera has moved. */
    /* The tones of the chamfer and the knurled side, as multipliers on the
     * knobs' plastic; the face is the plastic itself.  Each is its own
     * number so one can be tuned without moving the others. */
    const float TONE_RING = 1.41f; /* the chamfer round the face */
    const float TONE_GRIP = 1.20f; /* the knurled side: a touch above, lit */
    const float SQ = 0.98f;        /* ellipse: vertical/horizontal */
    float ry = r * SQ;             /* face half-height */
    float hs = r * 0.20f;          /* visible side, foreshortened */
    float off = r * 0.10f;         /* face centre sits a touch low */
    int saved = canvas_grain;
    canvas_grain = 0;
    int lim = (int)(r * 2.3f + 3.0f);
    /* The knob sits in a WELL, built exactly the way the power LED's is,
     * since that is what reads as recessed: a dark ring hugging the base,
     * a soft shadow on the plastic above the hole where the lip throws it,
     * and a lit chamfer lip on the plastic below, all of it reaching well
     * out into the case.  Drawn first; the knob body covers the middle. */
    {
        float bcy = cy + off - hs; /* the base, seen from above */
        float reach = r * 1.32f;
        for (int j2 = (int)(bcy - reach) - 1; j2 <= (int)(bcy + reach) + 1; j2++)
            for (int i2 = (int)(cx - reach) - 1; i2 <= (int)(cx + reach) + 1; i2++) {
                float dx = (float)i2 + 0.5f - cx, dy = (float)j2 + 0.5f - bcy;
                float d = sqrtf((dx / r) * (dx / r) + (dy / ry) * (dy / ry)); /* 1 at the base */
                if (d <= 0.94f || d > 1.30f)
                    continue;
                float up = -dy / fmaxf(d * ry, 1e-3f); /* +1 straight above */
                if (d <= 1.10f) {                      /* the ring itself, hard-edged */
                    float a = 1.0f - fmaxf(0.0f, (d - 1.04f) / 0.06f);
                    a *= fminf(1.0f, (d - 0.94f) / 0.04f);
                    float top = (dy < 0.0f) ? 1.0f : 0.66f;
                    px_blend(c, i2, j2, 26, 22, 14, a * 0.85f * top);
                } else {
                    float t = (d - 1.10f) / 0.20f; /* 0 at ring, 1 outside */
                    if (up > 0.15f)                /* shadow above the hole */
                        px_shade(c, i2, j2, 1.0f - 0.20f * (1.0f - t) * up, 0.0f);
                    /* no lit lip below: the knob's own shadow falls there, and
                     * a highlight under a shadow reads as two light sources */
                }
            }
    }
    for (int j2 = (int)cy - lim; j2 <= (int)cy + lim; j2++)
        for (int i2 = (int)cx - lim; i2 <= (int)cx + lim; i2++) {
            float dx = (float)i2 + 0.5f - cx, dy = (float)j2 + 0.5f - (cy + off);
            float ux = dx / r;
            if (fabsf(ux) > 1.8f)
                continue; /* the shadow reaches this far */
            float yr = (fabsf(ux) < 1.0f) ? ry * sqrtf(1.0f - ux * ux) : 0.0f; /* rim height here */
            float rho = sqrtf(ux * ux + (dy / ry) * (dy / ry)); /* 1 on the face rim */
            float base, cov;

            if (rho > 1.0f) {
                /* The shadow the knob throws: a copy of itself dropped a
                 * quarter of a radius, with a short gaussian skirt - tight
                 * to the knob, as a hard light throws it, and fading in
                 * across the knob's equator rather than starting there. */
                float sx = ux * 1.25f, sy = (dy - r * 0.28f) / ry; /* narrower than tall */
                float d2 = sqrtf(sx * sx + sy * sy) - 1.0f;        /* radii outside it */
                if (d2 < 0.8f) {
                    float f = (d2 <= 0.0f) ? 1.0f : expf(-d2 * d2 * 10.0f);
                    float v = (dy / r + 0.25f) / 0.75f;
                    if (v < 0.0f)
                        v = 0.0f;
                    if (v > 1.0f)
                        v = 1.0f;
                    f *= v * v * (3.0f - 2.0f * v); /* eases in over the sides */
                    if (f > 0.003f)
                        px_shade(c, i2, j2, 1.0f - 0.16f * f, 0.0f);
                }
                if (dy > 0.0f)
                    continue; /* below: only the shadow */
            }
            if (rho > 1.0f) {
                /* above the face: the cylinder's side, if this pixel is within
                 * its foreshortened height; lit by how much it faces up */
                float above = -dy - yr; /* distance above the rim */
                if (above > hs + 1.0f || fabsf(ux) >= 1.0f)
                    continue;
                cov = fminf(1.0f, hs + 1.0f - above);
                float up = sqrtf(1.0f - ux * ux); /* the normal's upward part */
                float th = atan2f(ux, up);        /* angle round the axis */
                float ridge = sinf(th * 28.0f);
                float lit = 0.5f + 0.5f * ridge * (-sinf(th + 0.6f));
                base = (0.45f + 0.28f * lit) * (0.66f + 0.40f * up);
                /* darkest right behind the face, where the chamfer overhangs
                 * the side - a crease, not a bright ledge - and falling off
                 * again at the far edge into the panel */
                base *= 0.76f + 0.24f * fminf(1.0f, above / (hs * 0.5f));
                base *= 1.0f - 0.28f * fmaxf(0.0f, (above - hs * 0.55f) / (hs * 0.45f));
                float f = TONE_GRIP * TONE_GAIN * KNOB_LEVEL * base;
                finished(c, i2, j2, CONTROL_R * f, CONTROL_G * f, CONTROL_B * f, cov);
                continue;
            }
            /* the face */
            cov = fminf(1.0f, (1.0f - rho) * ry + 0.5f);
            float up = -dy / fmaxf(ry * rho, 1e-3f); /* +1 at the top of the rim */
            float th = atan2f(ux, -dy / ry);
            if (rho > 0.85f) {
                /* the edge of the face: a rounded chamfer, bright along the top
                 * and shaded along the bottom, carrying a faint trace of the
                 * knurl.  Not a dark ring - the knurl proper is the side, seen
                 * above the face, and from here the face's edge is just the
                 * moulding turning away. */
                float ridge = sinf(th * 28.0f);
                float k = (rho - 0.85f) / 0.15f; /* 0 inner .. 1 rim */
                /* darker than the face it surrounds: this is the moulding
                 * turning away from the viewer toward the grip */
                /* the top half catches a little more light; the bottom half
                 * is as it was */
                const float RING_TOP_LIFT = 0.05f;
                base = (0.54f + 0.08f * up + RING_TOP_LIFT * fmaxf(0.0f, up)) * (1.0f - 0.30f * k) +
                       0.02f * ridge * k;
                float f = TONE_RING * TONE_GAIN * KNOB_LEVEL * base;
                finished(c, i2, j2, CONTROL_R * f, CONTROL_G * f, CONTROL_B * f, cov);
                /* a hard, narrow catch of the key light along the top of
                 * the rim, as the pushbuttons' rims have */
                float catch = powf(fmaxf(0.0f, up), 6.0f) * k * 0.12f;
                if (catch > 0.002f)
                    px_shade(c, i2, j2, 1.0f, catch * cov);
            } else {
                /* the face: flat, as the MOUSE key's face is - one colour with
                 * the moulding's grain in it and no light running off it - but
                 * in the knob's own warmer plastic, the one its chamfer and its
                 * knurled side are */
                int saved_grain = canvas_grain;
                canvas_grain = 1;
                float g = 1.0f + plastic_tex(i2 + (int)K.ox, j2 + (int)K.oy) * 0.25f;
                canvas_grain = saved_grain;
                /* light stains: where fingers have turned it for thirty years,
                 * soft warm blotches over a fine mottle, lighter than the
                 * MOUSE key's - taking more blue out than red, as the case's
                 * grime does.  They are the knob's own, so they are laid out
                 * on the face itself and turn with it, in millimetres so they
                 * look the same at any size, and each knob's are different,
                 * offset by where it sits on the case. */
                float mr = 1.0f, mg = 1.0f, mb = 1.0f;
                {
                    float mmu = (float)(K.ch > 0 ? K.ch : 268) / 268.0f;
                    float ex = ux * r, ey = (dy / ry) * r; /* on the face, unsquashed */
                    float ca = cosf(ang), sa = sinf(ang);
                    float u = (ex * ca + ey * sa + cx + K.ox) / mmu;
                    float v = (-ex * sa + ey * ca + cy + K.oy) / mmu;
                    float blot = vnoise(u * 0.55f, v * 0.6f, 61) * 0.6f +
                                 vnoise(u * 1.4f, v * 1.3f, 63) * 0.4f;
                    float d = fmaxf(0.0f, blot - 0.45f) / 0.55f;
                    d = d * d * 0.16f + (vnoise(u * 2.6f, v * 2.6f, 67) - 0.5f) * 0.03f;
                    mr = 1.0f - d * 0.70f;
                    mg = 1.0f - d;
                    mb = 1.0f - d * 1.40f;
                }
                finished(c, i2, j2, CONTROL_R * KNOB_FACE * KNOB_LEVEL * g * mr,
                         CONTROL_G * KNOB_FACE * KNOB_LEVEL * g * mg,
                         CONTROL_B * KNOB_FACE * KNOB_LEVEL * g * mb, cov);
                /* a faint sheen toward the upper left, where the room's light
                 * catches the moulding: the highlight the domed face had, at a
                 * fraction of its strength, so the face still reads as flat */
                {
                    float hx = (ux + 0.30f) / 0.34f, hy = (dy / ry + 0.36f) / 0.34f;
                    float sheen = expf(-(hx * hx + hy * hy)) * FACE_SHEEN;
                    if (sheen > 0.002f)
                        px_shade(c, i2, j2, 1.0f, sheen * cov);
                }
            }
            /* The index: a groove cut across the face and filled with an
             * off-white that has seen thirty years of thumbs.  Engraved, so it
             * is lit as a cut: of its two walls the one facing the light
             * catches it and the one turned away is in shade, and that shaded
             * wall throws a little shadow over the paint beside it.  Built as
             * the distance to a rounded stroke, so the ends are walls too. */
            {
                float px = sinf(ang), py = -cosf(ang); /* along the stroke */
                float ex = ux * r, ey = (dy / ry) * r; /* on the face, unsquashed */
                float a0 = r * 0.20f, a1 = r * 0.66f;
                float along = fminf(a1, fmaxf(a0, ex * px + ey * py));
                float qx = ex - px * along, qy = ey - py * along; /* from the stroke's spine */
                float dist = sqrtf(qx * qx + qy * qy);
                float hw = r * 0.075f + 0.5f;      /* the paint, either side of the spine */
                float ww = fmaxf(1.2f, r * 0.07f); /* the groove's walls, beyond it */
                float nx = dist > 1e-4f ? qx / dist : 0.0f, ny = dist > 1e-4f ? qy / dist : 0.0f;
                /* how much this side of the cut faces the light: the wall's
                 * normal points back into the groove, -n */
                float toward = nx * LIGHT_X + ny * LIGHT_Y;
                if (dist < hw + 0.5f) {
                    /* the paint, darker in the shade of the wall nearest the
                     * light, which stands over it */
                    float a = fminf(1.0f, hw + 0.5f - dist) * 0.86f;
                    float shade = 1.0f - 0.30f * fmaxf(0.0f, toward) * (dist / hw);
                    finished(c, i2, j2, 214.0f * shade, 208.0f * shade, 192.0f * shade, a);
                } else if (dist < hw + 0.5f + ww) {
                    /* a wall: lit if it faces the light, in shade if not,
                     * strongest at the paint's edge where the cut is deepest */
                    float prof = 1.0f - (dist - hw - 0.5f) / ww;
                    float lam = -toward;
                    px_shade(c, i2, j2, 1.0f + (lam > 0.0f ? 0.30f : 0.42f) * lam * prof,
                             lam > 0.0f ? 0.05f * lam * prof : 0.0f);
                }
            }
        }
    canvas_grain = saved;
}

/* A symbol printed on the case: its coverage field laid down in the same
 * ink as every other printed legend, the grain held off under it the way
 * the lettering does, since a silk screen sits on the plastic rather than
 * in it. */
static void paint_field(canvas *c, float x, float y, int dw, int dh, const float *cov) {
    /* the lamps' branch lines' tone: a shade paler than the lettering's
     * ink, since a symbol is a solid mark where a word is strokes, and in
     * the words' ink it came out heavier than they do */
    const int INK_R = 104, INK_G = 99, INK_B = 88;
    int saved = canvas_grain;
    canvas_grain = 0;
    for (int j = 0; j < dh; j++)
        for (int i = 0; i < dw; i++) {
            float a = cov[(size_t)j * dw + i];
            if (a > 0.004f)
                px_blend(c, (int)x + i, (int)y + j, INK_R, INK_G, INK_B, a);
        }
    canvas_grain = saved;
}

/* The monitor's own symbols, printed under each knob in the ink the rest
 * of the legends are: a sun for brightness, a half-filled disc for
 * contrast.  Distance fields, like the power mark, so they stay crisp at
 * any size. */
void knob_icons(canvas *c, float bx, float by, float cx2, float cy2, float s) {
    int dw = (int)(s * 2.6f) + 4, dh = dw;
    float *dep = calloc((size_t)dw * dh, sizeof *dep);
    if (!dep)
        return;
    float t = fmaxf(1.4f, s * 0.26f); /* the stroke: as heavy as the lettering's */
    /* sun: a ring and eight rays */
    for (int j = 0; j < dh; j++)
        for (int i = 0; i < dw; i++) {
            float x = (float)i + 0.5f - dw * 0.5f, y = (float)j + 0.5f - dh * 0.5f,
                  r = sqrtf(x * x + y * y);
            float sd = fabsf(r - s * 0.45f) - t * 0.5f;
            float ang = atan2f(y, x);
            float k = roundf(ang / (3.14159265f / 4.0f)) * (3.14159265f / 4.0f);
            float rx = cosf(k), ry = sinf(k);
            float along = x * rx + y * ry, across = fabsf(x * ry - y * rx);
            float ray = fmaxf(fmaxf(s * 0.72f - along, along - s * 1.15f), across - t * 0.5f);
            sd = fminf(sd, ray);
            float cov = 0.5f - sd;
            dep[(size_t)j * dw + i] = cov < 0 ? 0 : (cov > 1 ? 1 : cov);
        }
    paint_field(c, bx - dw * 0.5f, by - dh * 0.5f, dw, dh, dep);
    /* contrast: a ring, its right half filled */
    for (int j = 0; j < dh; j++)
        for (int i = 0; i < dw; i++) {
            float x = (float)i + 0.5f - dw * 0.5f, y = (float)j + 0.5f - dh * 0.5f,
                  r = sqrtf(x * x + y * y);
            float sd = fabsf(r - s * 0.78f) - t * 0.5f;
            float half = fmaxf(r - s * 0.78f, -x); /* inside the disc, x>0 */
            sd = fminf(sd, half);
            float cov = 0.5f - sd;
            dep[(size_t)j * dw + i] = cov < 0 ? 0 : (cov > 1 ? 1 : cov);
        }
    paint_field(c, cx2 - dw * 0.5f, cy2 - dh * 0.5f, dw, dh, dep);
    free(dep);
}

/* The square a knob's drawing reaches: the side band above, the shadow
 * ring below.  Smaller, and a redraw would clip them with straight edges. */
static void knob_square(int which, int *bx, int *by, int *bs) {
    float cx = K.place[which][0], cy = K.place[which][1], r = K.place[which][2];
    *bs = (int)(2.0f * (r * 2.35f + 4.0f)) + 2;
    *bx = (int)(cx - (float)*bs * 0.5f);
    *by = (int)(cy - (float)*bs * 0.5f);
}

/* Draw both knobs where they reach canvas c, which sits at (ox, oy) on the
 * case.  The two stand close enough that one's square takes in the edge of
 * the other, so a redraw of either puts back both, each as it is turned. */
static void knobs_onto(canvas *c, float ox, float oy) {
    int px0 = (int)ox, py0 = (int)oy;
    for (int k = 0; k < 2; k++) {
        int bx, by, bs;
        knob_square(k, &bx, &by, &bs);
        if (bx >= px0 + c->w || bx + bs <= px0 || by >= py0 + c->h || by + bs <= py0)
            continue;
        K.ox = ox;
        K.oy = oy;
        rotary(c, K.place[k][0] - ox, K.place[k][1] - oy, K.place[k][2], K.pos[k]);
    }
}

void knobs_draw(canvas *c) {
    K.cw = c->w;
    K.ch = c->h;
    /* first the plastic under both, with neither knob on it, and the band's
     * facing alpha with it - a moulded control does not catch the tube's
     * light as if it were the reveal dish */
    for (int k = 0; k < 2; k++) {
        int bx, by, bs;
        knob_square(k, &bx, &by, &bs);
        free(K.bg[k]);
        K.bg[k] = malloc((size_t)bs * bs * 4);
        K.bx[k] = bx;
        K.by[k] = by;
        K.bs[k] = bs;
        K.pos[k] = 0.5f;
        if (!K.bg[k])
            continue;
        for (int j = 0; j < bs; j++)
            for (int i = 0; i < bs; i++) {
                int x = bx + i, y = by + j;
                uint8_t *d = K.bg[k] + ((size_t)j * bs + i) * 4;
                if (x < 0 || y < 0 || x >= c->w || y >= c->h)
                    memset(d, 0, 4);
                else
                    memcpy(d, c->px + ((size_t)y * c->w + x) * 4, 4);
            }
    }
    /* then both knobs over it, the alpha under them left as the case's */
    knobs_onto(c, 0.0f, 0.0f);
    for (int k = 0; k < 2; k++)
        for (int j = 0; j < K.bs[k] && K.bg[k]; j++)
            for (int i = 0; i < K.bs[k]; i++) {
                int x = K.bx[k] + i, y = K.by[k] + j;
                if (x >= 0 && y >= 0 && x < c->w && y < c->h)
                    c->px[((size_t)y * c->w + x) * 4 + 3] =
                        K.bg[k][((size_t)j * K.bs[k] + i) * 4 + 3];
            }
}

const uint8_t *chassis_knob_set(int which, float pos, int *x, int *y, int *w, int *h) {
    if (which < 0 || which > 1 || !K.bg[which])
        return NULL;
    int bs = K.bs[which];
    K.patch = realloc(K.patch, (size_t)bs * bs * 4);
    if (!K.patch)
        return NULL;
    K.pos[which] = pos < 0.0f ? 0.0f : pos > 1.0f ? 1.0f : pos;
    /* the plastic, with no knob in it, and both knobs back over it */
    memcpy(K.patch, K.bg[which], (size_t)bs * bs * 4);
    canvas P;
    P.w = bs;
    P.h = bs;
    P.px = K.patch;
    knobs_onto(&P, (float)K.bx[which], (float)K.by[which]);
    for (size_t n = 0; n < (size_t)bs * bs; n++)
        K.patch[n * 4 + 3] = K.bg[which][n * 4 + 3];
    *x = K.bx[which];
    *y = K.by[which];
    *w = bs;
    *h = bs;
    return K.patch;
}

void knobs_layout(dxm_layout *L) {
    for (int k = 0; k < 2; k++)
        for (int m = 0; m < 3; m++)
            L->knob[k][m] = K.place[k][m];
}
