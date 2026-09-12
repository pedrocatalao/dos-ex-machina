/* main.c — the appliance: the order things come up in, and the frame loop.
 * --windowed, --shot and the rest are hidden dev flags. */
#include "app.h"
#include "log.h"
#include "splash.h"
#include "theatre.h"
#include "input.h"
#include "dos.h"
#include "chassis.h"
#include "dosbox.h"
#include "setup.h"
#include "crt.h"
#include "sound.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    app_options app;
    const char *shot;    /* --shot: write this frame and exit */
    int shot_frames;     /* ...after this many frames (60 if unset) */
    const char *autocmd; /* --type: commands, ';'-separated, one a second */
    float ambient;       /* room light: 0 dark room .. 1 bright */
    /* --dosbox DIR mounts DIR as C: instead of the machine's own drive;
     * --dosbox-core names the core library instead of the one beside the
     * program.  Both also read from the environment. */
    const char *dosbox, *dosbox_core;
    const char *keyboard; /* --keyboard: the DOS layout, instead of the guess */
    int setup;            /* --setup: open SETUP at once, for shots and tests */
} options;

static options parse(int argc, char **argv) {
    options o = {{0, 1600, 900, 0, NULL},
                 NULL,
                 0,
                 NULL,
                 0.5f,
                 getenv("DXM_DOSBOX"),
                 getenv("DXM_DOSBOX_CORE"),
                 getenv("DXM_KEYBOARD"),
                 0};
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump-audio") && i + 1 < argc)
            o.app.audio_dump = argv[++i];
        else if (!strcmp(argv[i], "--windowed"))
            o.app.windowed = 1;
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc)
            o.shot = argv[++i]; /* honours fullscreen */
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            o.shot_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--type") && i + 1 < argc)
            o.autocmd = argv[++i];
        else if (!strcmp(argv[i], "--deterministic"))
            o.app.deterministic = 1;
        else if (!strcmp(argv[i], "--size") && i + 1 < argc)
            sscanf(argv[++i], "%dx%d", &o.app.win_w, &o.app.win_h);
        else if (!strcmp(argv[i], "--dosbox") && i + 1 < argc)
            o.dosbox = argv[++i];
        else if (!strcmp(argv[i], "--dosbox-core") && i + 1 < argc)
            o.dosbox_core = argv[++i];
        else if (!strcmp(argv[i], "--keyboard") && i + 1 < argc)
            o.keyboard = argv[++i];
        else if (!strcmp(argv[i], "--setup"))
            o.setup = 1;
        else if (!strcmp(argv[i], "--ambient") && i + 1 < argc) {
            o.ambient = (float)atof(argv[++i]);
            if (o.ambient < 0)
                o.ambient = 0;
            if (o.ambient > 1)
                o.ambient = 1;
        }
    }
    return o;
}

/* The shipped look: a machine that has been used, tuned by eye on the
 * panel and copied from its crt.cfg.  The imperfections are all small -
 * h-sync and RGB shift in particular are kept low, since past a point they
 * put every character at a different sub-pixel phase and the same glyph
 * reads thin-edged on one side here and the other there.  The panel's own
 * file overrides all of this once it exists. */
static gpu_knobs shipped_knobs(float ambient) {
    /* brightness, contrast, bloom, burn_in, noise, jitter, glow_line,
     * ambient, flicker, hsync, rgb_shift, chassis_glow, persistence,
     * scan, vgrid, sharp_text, warp, margin, overscan, aperture_r,
     * crt_lines, crt_cols */
    gpu_knobs k = {0.219f,   0.710f, 0.190f, 0.044f, 0.190f, 0.029f, 0.087f, ambient,
                   0.229f,   0.029f, 0.048f, 0.646f, 0.490f, 0.414f, 0.077f, 1.0f,
                   DXM_WARP, 0.0f,   1.0f,   0.0f,   400,    DOS_W};
    return k;
}

/* The knobs show the live values - turned by hand, by the panel, or
 * loaded from the file - and only a knob that moved is redrawn. */
static void show_knobs(gpu *g, const gpu_knobs *k, float *last_b, float *last_c) {
    if (k->brightness != *last_b) {
        int px, py, pw, ph;
        const uint8_t *p = chassis_knob_set(0, k->brightness, &px, &py, &pw, &ph);
        if (p)
            gpu_patch_chassis(g, px, py, pw, ph, p);
        *last_b = k->brightness;
    }
    if (k->contrast != *last_c) {
        int px, py, pw, ph;
        const uint8_t *p = chassis_knob_set(1, (k->contrast - 0.4f) / 1.4f, &px, &py, &pw, &ph);
        if (p)
            gpu_patch_chassis(g, px, py, pw, ph, p);
        *last_c = k->contrast;
    }
}

/* The machine's own C: drive: a directory in the preferences, created the
 * first time and never touched by anything but the DOS that runs on it.
 * --dosbox DIR points the machine at another one. */
static const char *c_drive(const options *o, const app *a) {
    static char path[1200];
    if (o->dosbox)
        return o->dosbox;
    snprintf(path, sizeof path, "%sC", a->pref ? a->pref : "./");
    SDL_CreateDirectory(path);
    return path;
}

int main(int argc, char **argv) {
    options o = parse(argc, argv);
    app a;
    if (app_init(&a, &o.app) != 0)
        return 1;

    /* The machine takes the mouse before anything is on screen: the splash
     * is the machine warming up, and a desktop arrow sitting over it is the
     * one thing in the way.  Ctrl+F10 hands it back. */
    input_state in;
    input_init(&in, &a);

    chassis_job job;
    SDL_Thread *cth = chassis_build_begin(&job, a.W, a.H);
    int quit = splash_show(&a, &job);
    if (a.deterministic)
        app_fixed_step();
    theatre th;
    theatre_power_on(&th, a.deterministic);
    chassis_build_join(cth, &job);
    dxm_layout L = job.L;
    uint8_t *chas = job.px;
    if (!chas) {
        /* Out of memory for the case itself - W*H*4 bytes.  Nothing sensible
         * can be drawn without it. */
        char msg[200];
        snprintf(msg, sizeof msg, "Could not allocate the %dx%d chassis image.", a.W, a.H);
        dxm_log("%s", msg);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DOS ex Machina", msg, a.win);
        return 1;
    }
    gpu_set_chassis(a.gpu, chas, a.W, a.H);
    dxm_log("chassis uploaded");

    gpu_knobs k = shipped_knobs(o.ambient);
    ui_init(&k);
    static char cfgpath[1024];
    snprintf(cfgpath, sizeof cfgpath, "%scrt.cfg", a.pref ? a.pref : "./");
    if (!a.deterministic)
        ui_load(cfgpath);
    setup_bind(&k, cfgpath); /* what SETUP is allowed to change, and where it keeps it */
    dos_init(theatre_mhz(&th), a.deterministic);
    /* The DOS boots now, unseen, so it is at its prompt long before the
     * POST is done.  Without it there is no machine: say so and stop. */
    dosbox_set_cycles(theatre_cycles(&th)); /* the clock the display shows */
    dosbox_set_mhz(theatre_mhz(&th));       /* the same, as the BIOS screen prints it */
    if (o.keyboard)
        dosbox_set_layout(o.keyboard); /* else the host's own, guessed */
    if (dosbox_start(o.dosbox_core, c_drive(&o, &a), a.pref) != 0) {
        const char *msg = "DOS ex Machina could not start its DOS.\n\n"
                          "The DOSBox core (dosbox_pure_libretro) was not found beside the "
                          "program, or could not be loaded. dxm.log in the preferences "
                          "directory says which.";
        dxm_log("no DOS: giving up");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DOS ex Machina", msg, a.win);
        app_shutdown(&a);
        return 1;
    }
    if (o.setup)
        setup_open();
    dxm_log("dos booting, entering the frame loop");

    Uint64 t_start = app_now_ns();
    int frame = 0;
    /* the turbo display's reading: frames counted over half a second of
     * the machine's clock, so under --deterministic it reads a steady 60 */
    Uint64 fps_t0 = t_start;
    int fps_n = 0;
    float last_b = -1.0f, last_c = -1.0f; /* what the knobs currently show */
    const char *autocmd = o.autocmd;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            input_result r = input_event(&in, &a, &L, &k, &e);
            if (r == INPUT_QUIT)
                quit = 1;
            else if (r == INPUT_BUTTON) {
                theatre_button(&th, in.button);
                dosbox_set_cycles(theatre_cycles(&th));
                dosbox_set_mhz(theatre_mhz(&th));
            } else if (r == INPUT_RESIZED) {
                app_measure(&a);
                gpu_resize(a.gpu, a.W, a.H);
                L = chassis_layout(a.W, a.H);
                free(chas);
                chas = chassis_render(&L, a.W, a.H);
                gpu_set_chassis(a.gpu, chas, a.W, a.H);
                last_b = last_c = -1.0f;
            }
        }
        /* SETUP stops the machine's clock: a BIOS setup halted the boot, so
         * the POST waits where it stood and the theatre holds its pose. */
        double raw = (app_now_ns() - t_start) / 1e9;
        static double held_at = -1.0, held_for = 0.0;
        if (setup_visible() && held_at < 0.0)
            held_at = raw;
        else if (!setup_visible() && held_at >= 0.0) {
            held_for += raw - held_at;
            held_at = -1.0;
        }
        double t = (held_at >= 0.0 ? held_at : raw) - held_for;

        if (dos_take_beep())
            snd_beep(240.0); /* after the RAM check */
        {
            double f = dos_take_floppy();
            if (f <= 0.0)
                f = dosbox_take_floppy(); /* the core's BIOS screen asking */
            if (f > 0.0)
                theatre_drive(&th, f, t);
        }
        dos_state st = dos_update(t);
        /* --type takes a ';'-separated list, typed one a second once the
         * DOS has the tube - so a sequence like "CD GAMES;DIR" can be
         * driven. */
        static double type_at = -1.0;
        if (autocmd && dosbox_shown()) {
            if (type_at < 0.0)
                type_at = t + 1.0;
            if (t >= type_at) {
                const char *semi = strchr(autocmd, ';');
                const char *end = semi ? semi : autocmd + strlen(autocmd);
                char line[256];
                snprintf(line, sizeof line, "%.*s\r", (int)(end - autocmd), autocmd);
                dosbox_type(line);
                autocmd = semi ? semi + 1 : NULL;
                type_at = t + 1.0;
            }
        }
        /* The handover: the BIOS screen has cleared and DOSBox, at its
         * prompt since before the memory count, takes the tube. */
        if (st == DOS_HANDOVER && dosbox_running() && !dosbox_shown()) {
            dosbox_show();
            dxm_log("dosbox: has the tube");
        }
        if (dosbox_exited() && th.off_t0 < 0.0)
            theatre_power_off(&th, t);
        if (theatre_frame(&th, a.gpu, &L, a.W, a.H, t))
            quit = 1;

        /* the tube's source: DOSBox once it has the tube, else the BIOS
         * screen */
        int cw, ch, cl, held = 0;
        const uint8_t *src = NULL;
        if (dosbox_shown()) {
            held = (src = dosbox_frame(&cw, &ch, &cl)) != NULL;
            /* A change of picture size is a program starting or ending, and
             * on this machine a program starting means the drive reads it.
             * The first size seen is the prompt's own, and a drive already
             * running is not restarted. */
            static int last_w = -1, last_h = -1;
            if (held && (cw != last_w || ch != last_h)) {
                if (last_w >= 0 && t >= th.drive_until)
                    theatre_drive(&th, 2.2, t);
                last_w = cw;
                last_h = ch;
            }
        }
        if (setup_visible()) {
            /* the machine's own program has the tube, in its own mode */
            int sw, sh;
            const uint8_t *px = setup_render(&sw, &sh);
            gpu_set_tube(a.gpu, px, sw, sh);
            k.crt_lines = sh; /* a 400-line mode, like the text screen's */
            k.crt_cols = sw;
            k.sharp_text = 0.0f; /* graphics, not a character generator */
        } else if (src) {
            gpu_set_tube(a.gpu, src, cw, ch);
            k.crt_lines = cl;
            k.crt_cols = cw;
            /* game art: hard pixels; the DOS's text, even strokes */
            k.sharp_text = dosbox_text_mode() ? 1.0f : 0.0f;
        } else {
            gpu_set_tube(a.gpu, dos_render(), DOS_W, DOS_H);
            /* one scanline per texture row, border included - the beam
             * swept the overscan at the same pitch as the 400 lines of
             * text inside it */
            k.crt_lines = DOS_H;
            k.crt_cols = DOS_W;
            k.sharp_text = 1.0f;
        }

        show_knobs(a.gpu, &k, &last_b, &last_c);
        k.aperture_r = L.aperture_r; /* match the chassis hole */
        /* the wall clock, not the machine's: SETUP stops the boot, not the
         * tube - its noise, flicker and jitter are the glass's own */
        gpu_draw(a.gpu, L.tube_x / a.W, 1.0f - (L.tube_y + L.tube_h) / a.H, L.tube_w / a.W,
                 L.tube_h / a.H, &k, raw);
        if (held)
            dosbox_frame_done();
        {
            int ow, oh;
            const uint8_t *ov = ui_render(a.W, a.H, &ow, &oh);
            if (ov) {
                gpu_set_overlay(a.gpu, ov, ow, oh);
                gpu_draw_overlay(a.gpu);
            }
        }
        theatre_room(&th, a.gpu, t);
        input_mouse_sync(&in, &a);
        SDL_GL_SwapWindow(a.win);
        frame++;
        app_frame_done();
        fps_n++;
        {
            Uint64 now = app_now_ns();
            if (now - fps_t0 >= 500000000ull) {
                theatre_fps(&th, (int)((double)fps_n * 1e9 / (double)(now - fps_t0) + 0.5));
                fps_n = 0;
                fps_t0 = now;
            }
        }
        if (frame == 1)
            dxm_log("first machine frame on screen");
        if (o.shot && frame >= (o.shot_frames ? o.shot_frames : 60)) {
            app_screenshot(&a, o.shot);
            quit = 1;
        }
    }
    if (!a.deterministic)
        ui_save(cfgpath);
    dosbox_stop();
    app_shutdown(&a);
    return 0;
}
