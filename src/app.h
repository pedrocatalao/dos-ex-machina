/* app.h — the appliance's window, GL context, GPU pipeline, audio device
 * and clock: everything that exists before the machine is drawn and after
 * it is switched off.  One window, always fullscreen, no chrome; the dev
 * flags are the exceptions (SPEC §11). */
#ifndef DXM_APP_H
#define DXM_APP_H
#include <SDL3/SDL.h>
#include "gpu.h"

typedef struct {
    int windowed;     /* a window instead of the display */
    int win_w, win_h; /* its size, when windowed */
    /* --deterministic: the same frame every run.  A fixed-step clock, no
     * HiDPI scaling (the drawable is exactly --size), the shipped CRT
     * defaults rather than the user's crt.cfg, no vsync wait. */
    int deterministic;
    const char *audio_dump; /* --dump-audio: raw s16 stereo to this file */
} app_options;

typedef struct {
    SDL_Window *win;
    SDL_GLContext ctx;
    gpu *gpu;
    SDL_AudioStream *audio; /* NULL when there is no device: silent */
    int W, H;               /* the drawable, in pixels (SPEC §6.3) */
    float win_wf, win_hf;   /* the window, in its own units - mouse coordinates */
    const char *pref;       /* the preferences directory, trailing separator */
    int windowed, deterministic;
} app;

/* Bring everything up.  On failure the reason has already been shown in a
 * message box and logged; the caller only has to exit. */
int app_init(app *a, const app_options *o);
/* Re-read the drawable and window sizes, after the window changed. */
void app_measure(app *a);
/* The current frame, as a bottom-up 24-bit BMP. */
void app_screenshot(const app *a, const char *path);
void app_shutdown(app *a);

/* The machine's clock.  Normally the wall clock; under --deterministic a
 * counter that advances exactly one sixtieth of a second per frame, so a
 * given frame number is the same picture on every run - which is what the
 * golden-frame test compares against. */
Uint64 app_now_ns(void);
void app_fixed_step(void); /* switch to the counter, from now */
void app_frame_done(void); /* one frame has been shown */
#endif
