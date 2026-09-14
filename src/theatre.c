/* theatre.c — see theatre.h. */
#include "theatre.h"
#include "sound.h"
#include "log.h"
#include "segdisp.h"
#include <math.h>
#include <string.h>

/* The machine comes up out of the same black the splash left behind, so
 * the two read as one continuous power-on rather than a cut. */
static const double MACH_FADE = 0.70;
static const double WARM = 1.6;    /* the tube's warm-up, seconds */
static const double OFF_END = 1.1; /* power-off, from the switch to the end */

void theatre_power_on(theatre *th, int deterministic) {
    th->fade0 = app_now_ns();
    th->off_t0 = -1.0;
    th->drive_until = 0.0;
    th->pwr = 0.0f;
    th->deterministic = deterministic;
    th->fps = -1;
    th->mhz_stop = SEG_MHZ_DEFAULT;
    th->mode = 0;
    memset(th->seg_lvl, 0, sizeof th->seg_lvl);
    th->leg_lvl[0] = th->leg_lvl[1] = 0.0f;
    th->seg_t = -1.0;
    snd_relay();
    snd_degauss();
}

void theatre_power_off(theatre *th, double t) {
    th->off_t0 = t;
    snd_power(0);
    snd_relay();
    dxm_log("power off");
}

void theatre_fps(theatre *th, int fps) {
    th->fps = fps > 999 ? 999 : fps;
}

static const int mhz_stops[SEG_MHZ_NSTOPS] = SEG_MHZ_STOPS;

static const int mhz_cycles[SEG_MHZ_NSTOPS] = SEG_MHZ_CYCLES;

int theatre_mhz(const theatre *th) {
    return mhz_stops[th->mhz_stop];
}

int theatre_cycles(const theatre *th) {
    return mhz_cycles[th->mhz_stop];
}

void theatre_button(theatre *th, int which) {
    if (which == 0)
        th->mode ^= 1;
    else if (th->mode == 0) {
        int n = th->mhz_stop + (which == 2 ? 1 : -1);
        if (n >= 0 && n < SEG_MHZ_NSTOPS)
            th->mhz_stop = n;
    }
    dxm_log("display: %s, %d MHz", th->mode ? "fps" : "mhz", mhz_stops[th->mhz_stop]);
}

/* the three digits of v, leading zeros blank, as on the real displays */
static void seg_figures(int v, int mask[3]) {
    static const int figures[10] = SEG_FIGURES;
    mask[0] = mask[1] = mask[2] = 0;
    if (v < 0)
        return;
    mask[2] = figures[v % 10];
    if (v >= 10)
        mask[1] = figures[(v / 10) % 10];
    if (v >= 100)
        mask[0] = figures[(v / 100) % 10];
}

void theatre_drive(theatre *th, double seconds, double t) {
    snd_floppy(seconds);
    if (t + seconds > th->drive_until)
        th->drive_until = t + seconds;
}

int theatre_frame(theatre *th, gpu *g, const dxm_layout *L, int W, int H, double t) {
    int done = 0;
    /* the tube's state this frame: warming up, steady, or dying */
    {
        float rh = 1.0f, rv = 1.0f, gain = 1.0f;
        double fe = (app_now_ns() - th->fade0) / 1e9;
        if (th->off_t0 >= 0.0) {
            double o = t - th->off_t0;
            if (o < 0.10) { /* vertical collapse */
                float p = (float)(o / 0.10);
                p = p * p;
                rv = 1.0f - 0.985f * p;
                gain = 1.0f + 1.4f * p;
            } else if (o < 0.18) { /* the line shrinks to a dot */
                float p = (float)((o - 0.10) / 0.08);
                rv = 0.015f;
                rh = 1.0f - 0.98f * p;
                gain = 2.4f - 0.6f * p;
            } else { /* the dot fades */
                float p = (float)((o - 0.18) / 0.45);
                if (p > 1.0f)
                    p = 1.0f;
                rv = 0.015f;
                rh = 0.02f;
                gain = 1.8f * (1.0f - p) * (1.0f - p);
            }
            if (o >= OFF_END)
                done = 1;
        } else if (fe < WARM) {
            /* the raster opens quickly and then creeps the last of the
             * way, the way a cold tube settles: a cubic ease-OUT, all
             * the speed at the start and none at the end */
            float u = 1.0f - (float)(fe / WARM);
            float p = 1.0f - u * u * u;
            rh = 0.96f + 0.04f * p;
            rv = 0.90f + 0.10f * p;
            gain = p * p;
        }
        gpu_set_tube_power(g, rh, rv, gain);
    }

    /* GL's origin is bottom-left; chassis_render draws top-down. */
    /* The activity LED follows the drive, with a little flicker so it
     * reads as head movement rather than a steady lamp.  Its level comes
     * from the sound's own envelope, which the audio thread advances in
     * real time; under the fixed-step clock that would make the LED the
     * one thing in the frame that differs between runs, so there it is
     * simply on while the drive was asked to run. */
    {
        float lv = th->deterministic ? (t < th->drive_until ? 1.0f : 0.0f) : snd_floppy_level();
        float fl = 0.72f + 0.28f * (float)sin(t * 47.0) * (float)sin(t * 23.0);
        gpu_set_led(g, 0, L->fdd_led[0] / W, 1.0f - (L->fdd_led[1] + L->fdd_led[3]) / H,
                    L->fdd_led[2] / W, L->fdd_led[3] / H, lv * fl, 0.16f, 1.0f, 0.22f, 0, 2.0f);
        /* power: steady, and it comes up with the machine */
        th->pwr +=
            ((th->off_t0 >= 0.0 ? 0.0f : 1.0f) - th->pwr) * (th->off_t0 >= 0.0 ? 0.25f : 0.02f);
        gpu_set_led(g, 1, L->pwr_led[0] / W, 1.0f - (L->pwr_led[1] + L->pwr_led[3]) / H,
                    L->pwr_led[2] / W, L->pwr_led[3] / H, th->pwr, 0.20f, 1.0f, 0.26f, 1,
                    1.0f - L->pwr_shelf / H);
        /* the turbo display: lit like the power LED, and it comes up and
         * goes down with it; the clock or the frame rate, with the legend
         * to say which */
        /* An LED does not snap: each segment eases toward on or off over a
         * fifth of a second, so a change of speed or of mode crossfades
         * rather than cuts.  The mode lights crossfade the same way. */
        int mask[3];
        seg_figures(th->mode ? th->fps : mhz_stops[th->mhz_stop], mask);
        float dt = th->seg_t < 0.0 ? 1.0f : (float)(t - th->seg_t);
        th->seg_t = t;
        float k = 1.0f - expf(-dt / 0.22f);
        for (int i = 0; i < 21; i++) {
            float want = (mask[i / 7] >> (i % 7)) & 1 ? 1.0f : 0.0f;
            th->seg_lvl[i] += (want - th->seg_lvl[i]) * k;
        }
        for (int i = 0; i < 2; i++) {
            float want = (th->mode ? 0 : 1) == i ? 1.0f : 0.0f;
            th->leg_lvl[i] += (want - th->leg_lvl[i]) * k;
        }
        gpu_set_segdisp(g, L->seg[0] / W, 1.0f - (L->seg[1] + L->seg[3]) / H, L->seg[2] / W,
                        L->seg[3] / H, th->seg_lvl, th->pwr);
        /* the mode lights: red, against FPS or MHz, and dark with the machine */
        for (int i = 0; i < 2; i++)
            gpu_set_led(g, 2 + i, L->mode_led[i][0] / W,
                        1.0f - (L->mode_led[i][1] + L->mode_led[i][3]) / H, L->mode_led[i][2] / W,
                        L->mode_led[i][3] / H, th->leg_lvl[i] * th->pwr, 1.0f, 0.16f, 0.06f, 1,
                        2.0f);
    }
    return done;
}

void theatre_room(const theatre *th, gpu *g, double t) {
    {
        double fe = (app_now_ns() - th->fade0) / 1e9;
        if (fe < MACH_FADE) {
            float a = (float)(1.0 - fe / MACH_FADE);
            gpu_draw_fade(g, a * a * (3.0f - 2.0f * a));
        }
    }
    if (th->off_t0 >= 0.0) {
        /* the room goes dark after the tube has, not with it */
        double o = t - th->off_t0, f0 = 0.6;
        if (o > f0) {
            float a = (float)((o - f0) / (OFF_END - f0));
            if (a > 1.0f)
                a = 1.0f;
            gpu_draw_fade(g, a * a * (3.0f - 2.0f * a));
        }
    }
}
