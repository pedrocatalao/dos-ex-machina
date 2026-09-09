/* app.c — see app.h. */
#include "app.h"
#include "log.h"
#include "sound.h"
#include "corehost.h"
#include "dosbox.h"
#include "version.h"
#include "gen/splash.h"
#include "gen/icon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the clock ---------------------------------------------------------- */
static int g_fixed_step;
static Uint64 g_vclock;
Uint64 app_now_ns(void) {
    return g_fixed_step ? g_vclock : SDL_GetTicksNS();
}
void app_fixed_step(void) {
    g_vclock = SDL_GetTicksNS();
    g_fixed_step = 1;
}
void app_frame_done(void) {
    if (g_fixed_step)
        g_vclock += 1000000000ull / 60;
}

/* ---- audio ---------------------------------------------------------------- */
static FILE *g_audio_dump; /* --dump-audio, dev verification */
static void SDLCALL audio_cb(void *ud, SDL_AudioStream *st, int add, int total) {
    (void)ud;
    (void)total;
    if (add <= 0)
        return;
    static int16_t buf[4096];
    int frames = add / 4;
    if (frames > 2048)
        frames = 2048;
    if (dosbox_running())
        dosbox_audio(buf, frames);
    else
        corehost_audio(buf, frames);
    snd_mix(buf, frames);
    if (g_audio_dump) {
        fwrite(buf, 4, (size_t)frames, g_audio_dump);
    }
    SDL_PutAudioStreamData(st, buf, frames * 4);
}

static void gpu_log_cb(const char *m) {
    dxm_log("%s", m);
}

/* Say so where it will be seen.  A person who double-clicked the program
 * has no stderr; a message box they have. */
static void fail(SDL_Window *win, const char *msg) {
    dxm_log("%s", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DOS ex Machina", msg, win);
}

void app_measure(app *a) {
    /* HiDPI: size from the DRAWABLE, never the window (SPEC §6.3) */
    SDL_GetWindowSizeInPixels(a->win, &a->W, &a->H);
    /* mouse arrives in WINDOW units; the panel works in drawable px */
    int ww, wh;
    SDL_GetWindowSize(a->win, &ww, &wh);
    a->win_wf = (float)(ww > 0 ? ww : a->W);
    a->win_hf = (float)(wh > 0 ? wh : a->H);
}

int app_init(app *a, const app_options *o) {
    memset(a, 0, sizeof *a);
    a->windowed = o->windowed;
    a->deterministic = o->deterministic;
    if (o->audio_dump)
        g_audio_dump = fopen(o->audio_dump, "wb");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    a->pref = SDL_GetPrefPath("DOSexMachina", "dxm");
    log_open(a->pref);
    {
        int v = SDL_GetVersion();
        dxm_log("DOS ex Machina " DXM_VERSION " on %s, SDL %d.%d.%d", SDL_GetPlatform(),
                SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_WindowFlags wflags = SDL_WINDOW_OPENGL;
    if (!o->deterministic)
        wflags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!o->windowed)
        wflags |= SDL_WINDOW_FULLSCREEN;
    a->win = SDL_CreateWindow("DOS ex Machina", o->win_w, o->win_h, wflags);
    if (!a->win) {
        dxm_log("window: %s", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DOS ex Machina", SDL_GetError(), NULL);
        return -1;
    }
    /* The window/taskbar icon.  Windows also carries one as a resource for
     * Explorer to show on the .exe, and macOS gets it from the bundle - this
     * is what Linux has, and it costs nothing on the others. */
    { /* SDL_CreateSurfaceFrom wants writable pixels and the icon is
       * const data, so it gets a copy for the moment SetWindowIcon needs */
        size_t icn = (size_t)DXM_ICON_W * DXM_ICON_HT * 4;
        void *icpx = malloc(icn);
        if (icpx) {
            memcpy(icpx, dxm_icon, icn);
            SDL_Surface *ic = SDL_CreateSurfaceFrom(DXM_ICON_W, DXM_ICON_HT, SDL_PIXELFORMAT_RGBA32,
                                                    icpx, DXM_ICON_W * 4);
            if (ic) {
                SDL_SetWindowIcon(a->win, ic);
                SDL_DestroySurface(ic);
            }
            free(icpx);
        }
    }

    a->ctx = SDL_GL_CreateContext(a->win);
    if (!a->ctx) {
        /* The most likely failure on a machine that cannot run DXM at all:
         * no OpenGL 3.3 core context to be had.  Say so on screen. */
        char msg[600];
        snprintf(msg, sizeof msg,
                 "Could not create an OpenGL 3.3 core context, "
                 "which DOS ex Machina needs.\n\n%s",
                 SDL_GetError());
        fail(a->win, msg);
        return -1;
    }
    SDL_GL_SetSwapInterval(o->deterministic ? 0 : 1);
    /* Typed characters come from SDL's text input, not from key-down: the
     * keycode of Shift+2 is still '2', and only the text event knows what
     * the keyboard layout made of it.  Control keys stay on key-down. */
    SDL_StartTextInput(a->win);

    if (!o->windowed) {
        SDL_SetWindowFullscreenMode(a->win, NULL); /* NULL = desktop mode */
        SDL_SetWindowFullscreen(a->win, true);
        SDL_SyncWindow(a->win); /* the transition is ASYNC on
                                 * macOS; measuring before it
                                 * settles lays the machine
                                 * out for the wrong size */
    }
    app_measure(a);
    dxm_log("window %dx%d px (%.0fx%.0f units), %s", a->W, a->H, (double)a->win_wf,
            (double)a->win_hf, o->windowed ? "windowed" : "fullscreen");

    gpu_set_log(gpu_log_cb);
    a->gpu = gpu_create(a->W, a->H);
    if (!a->gpu) {
        char msg[1400];
        snprintf(msg, sizeof msg,
                 "This computer's OpenGL driver does not provide OpenGL 3.3 "
                 "core, which DOS ex Machina needs.\n\nDriver: %s\nMissing: %s",
                 gpu_describe(), gpu_missing());
        fail(a->win, msg);
        return -1;
    }
    dxm_log("GL: %s", gpu_describe());

    /* The machine is switched on before it is drawn: the splash and the
     * sound of it running come up first, and the chassis is built behind
     * them. */
    {
        uint8_t *spl = dxm_splash_rgba();
        if (spl) {
            gpu_set_splash(a->gpu, spl, DXM_SPLASH_W, DXM_SPLASH_HT);
            free(spl);
        }
    }

    /* The core renders at 44100 Hz (skyroads audio.c SAMPLE_RATE).  The
     * stream must be opened at the CORE's rate - SDL3 resamples to whatever
     * the hardware wants.  Opening at 48000 played everything 8.8%% fast. */
    SDL_AudioSpec as = {SDL_AUDIO_S16, 2, 44100};
    a->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &as, audio_cb, NULL);
    if (a->audio)
        SDL_ResumeAudioStreamDevice(a->audio);
    if (a->audio)
        dxm_log("audio stream open");
    else
        dxm_log("audio: no device (%s) - silent", SDL_GetError());
    snd_init(44100);
    snd_power(1); /* fans and spindle spin up, immediately */
    return 0;
}

static void write_bmp(const char *path, const uint8_t *rgb, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    int row = (w * 3 + 3) & ~3, sz = 54 + row * h;
    uint8_t hd[54] = {0};
    hd[0] = 'B';
    hd[1] = 'M';
    memcpy(hd + 2, &sz, 4);
    int off = 54;
    memcpy(hd + 10, &off, 4);
    int ih = 40;
    memcpy(hd + 14, &ih, 4);
    memcpy(hd + 18, &w, 4);
    memcpy(hd + 22, &h, 4);
    hd[26] = 1;
    hd[28] = 24;
    fwrite(hd, 1, 54, f);
    uint8_t pad[3] = {0};
    for (int y = 0; y < h; y++) { /* GL readback is bottom-up */
        for (int x = 0; x < w; x++) {
            const uint8_t *p = rgb + ((size_t)y * w + x) * 3;
            uint8_t bgr[3] = {p[2], p[1], p[0]};
            fwrite(bgr, 1, 3, f);
        }
        fwrite(pad, 1, row - w * 3, f);
    }
    fclose(f);
}
void app_screenshot(const app *a, const char *path) {
    int rw, rh;
    uint8_t *px = gpu_readback(a->gpu, &rw, &rh);
    if (px) {
        write_bmp(path, px, rw, rh);
        free(px);
        fprintf(stderr, "wrote %s (%dx%d)\n", path, rw, rh);
    } else
        fprintf(stderr, "readback failed\n");
}

void app_shutdown(app *a) {
    corehost_stop();
    if (g_audio_dump) {
        fclose(g_audio_dump);
        g_audio_dump = NULL;
    }
    log_close();
    SDL_Quit();
    (void)a;
}
