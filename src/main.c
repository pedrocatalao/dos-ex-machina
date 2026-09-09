/* main.c — the appliance: the order things come up in, and the frame loop.
 * --windowed, --shot and the rest are hidden dev flags (SPEC §11). */
#include "app.h"
#include "log.h"
#include "splash.h"
#include "theatre.h"
#include "input.h"
#include "selftest.h"
#include "dos.h"
#include "chassis.h"
#include "corehost.h"
#include "dosbox.h"
#include "coreload.h"
#include "library.h"
#include "catalogue.h"
#include "crt.h"
#include "sound.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    app_options app;
    int selftest;
    const char *shot;    /* --shot: write this frame and exit */
    int shot_frames;     /* ...after this many frames (60 if unset) */
    const char *autocmd; /* --type: commands, ';'-separated, one per prompt */
    float ambient;       /* room light: 0 dark room .. 1 bright */
    /* --dosbox DIR: a real DOS.  DOSBox Pure boots behind the POST with
     * DIR as C: and takes the tube at the prompt.  --dosbox-core names
     * the core library; unset, it is looked for beside the program. */
    const char *dosbox, *dosbox_core;
} options;

static options parse(int argc, char **argv) {
    options o = {{0, 1600, 900, 0, NULL}, 0, NULL, 0, NULL, 0.5f, getenv("DXM_DOSBOX"),
                 getenv("DXM_DOSBOX_CORE")};
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump-audio") && i + 1 < argc)
            o.app.audio_dump = argv[++i];
        else if (!strcmp(argv[i], "--windowed"))
            o.app.windowed = 1;
        else if (!strcmp(argv[i], "--selftest")) {
            o.selftest = 1;
            o.app.windowed = 1;
        } else if (!strcmp(argv[i], "--shot") && i + 1 < argc)
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

/* The shipped look: a machine that has been used, tuned by eye on the F1
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

/* The machine's own contents: what is installed, and what could be. */
static void scan_library(void) {
    /* What DXM can run is whatever is installed, not what it was built
     * with.  Scanning here means the prompt and the navigator agree about
     * the machine's contents from the first frame. */
    lib_scan();
    dxm_log("library scanned: %d installed", lib_count());
    /* The cached catalogue first, so the navigator is populated instantly
     * and works with no network at all; then a refresh in the background. */
    cat_load_cached();
    cat_refresh_begin();
    dxm_log("catalogue: cache read, refresh started");
    for (int i = 0; i < lib_count(); i++) {
        const lib_game *lg = lib_at(i);
        dxm_log("game %-12s %s", lg->id,
                lg->ready ? "ready" : (lg->note[0] ? lg->note : "not ready"));
    }
    if (lib_count() == 0)
        dxm_log("no games installed - %sgames", lib_root());
}

/* The knobs show the live values - turned by hand, by the F1 panel, or
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

int main(int argc, char **argv) {
    options o = parse(argc, argv);
    app a;
    if (app_init(&a, &o.app) != 0)
        return 1;

    chassis_job job;
    SDL_Thread *cth = chassis_build_begin(&job, a.W, a.H);
    int quit = 0;
    if (!o.selftest)
        quit = splash_show(&a, &job);
    if (a.deterministic)
        app_fixed_step();
    theatre th;
    theatre_power_on(&th, o.selftest, a.deterministic);
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
    scan_library();
    dos_init();
    /* The real DOS boots now, unseen, so it is at its prompt long before
     * the POST is done.  If it cannot, the machine's own DOS carries on. */
    if (o.dosbox && dosbox_start(o.dosbox_core, o.dosbox, a.pref) == 0)
        dos_handover_mode();
    dxm_log("dos ready, entering the frame loop");

    Uint64 t_start = app_now_ns();
    int frame = 0, core_started = 0;
    /* the running game, by id: lib_scan rebuilds the library array in
     * place, so a pointer into it would not survive a rescan */
    char core_id[32] = "";
    input_state in;
    input_init(&in, &a);
    float last_b = -1.0f, last_c = -1.0f; /* what the knobs currently show */
    const char *autocmd = o.autocmd;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            input_result r = input_event(&in, &a, &L, &k, &e);
            if (r == INPUT_QUIT)
                quit = 1;
            else if (r == INPUT_RESIZED) {
                app_measure(&a);
                gpu_resize(a.gpu, a.W, a.H);
                L = chassis_layout(a.W, a.H);
                free(chas);
                chas = chassis_render(&L, a.W, a.H);
                gpu_set_chassis(a.gpu, chas, a.W, a.H);
                last_b = last_c = -1.0f;
            }
        }
        double t = (app_now_ns() - t_start) / 1e9;
        if (o.selftest && selftest_step(t))
            quit = 1;

        /* core lifecycle */
        /* Only return to the prompt if a core was ACTUALLY started and has
         * since exited.  DOS_RUNNING also covers the loading pause before a
         * launch, and testing the state alone aborted the launch instantly. */
        if (core_started && !corehost_running()) {
            core_started = 0;
            /* The run is over: let go of the module so the next launch opens
             * a fresh copy with its globals at their initial values
             * (PORTING.md 3.2).  corehost_stop() joins the core thread and
             * fences the audio callback first, so nothing is still inside
             * the image when it goes. */
            corehost_stop();
            corehost_use_module(NULL);
            lib_unload(lib_find(core_id));
            dos_core_exited();
        }
        const char *req = dos_launch_request();
        if (req) {
            const lib_game *lg = lib_find(req);
            const dxm_module *m = lg ? lib_module(lg) : NULL;
            if (m) {
                theatre_drive(&th, 2.2, t); /* the drive works while it loads */
                corehost_use_module(m);
                /* DXM_DATA still overrides, for working on a port without
                 * installing it first. */
                const char *dd = getenv("DXM_DATA");
                if (corehost_start(m->info, dd ? dd : lg->data) == 0) {
                    core_started = 1;
                    snprintf(core_id, sizeof core_id, "%s", lg->id);
                    lib_touch_played(lg);
                    input_capture(&in, &a, 1);
                }
            }
            if (!core_started)
                dos_core_failed();
        }
        if (dos_take_beep())
            snd_beep(240.0); /* after the RAM check */
        {
            double f = dos_take_floppy();
            if (f > 0.0)
                theatre_drive(&th, f, t);
        }
        /* Once per frame.  It used to be twice while a --type command was
         * pending, which took the first step of the memory count a frame
         * early in exactly those runs; the readme golden frame was
         * re-blessed when this became one call. */
        dos_state st = dos_update(t);
        /* --type takes a ';'-separated list, typed one per return to the
         * prompt - so a sequence like "CD GAMES;DIR" can be driven. */
        if (autocmd && *autocmd && st == DOS_PROMPT) {
            const char *semi = strchr(autocmd, ';');
            const char *end = semi ? semi : autocmd + strlen(autocmd);
            for (const char *q = autocmd; q < end; q++)
                dos_key(*q, 0);
            dos_key('\r', 0);
            autocmd = semi ? semi + 1 : NULL;
        }
        /* The handover: the BIOS screen has cleared and DOSBox, at its
         * prompt since before the memory count, takes the tube. */
        if (st == DOS_HANDOVER && dosbox_running() && !dosbox_shown()) {
            dosbox_show();
            dxm_log("dosbox: has the tube");
        }
        if ((st == DOS_OFF || dosbox_exited()) && th.off_t0 < 0.0)
            theatre_power_off(&th, t);
        if (theatre_frame(&th, a.gpu, &L, a.W, a.H, t))
            quit = 1;

        /* pick the tube source: DOSBox once it has the tube, else the
         * running core, else the DOS text screen */
        int cw, ch, cl, held = 0, text = 0;
        const uint8_t *src = NULL;
        static uint8_t cells[DOS_ROWS * DOS_COLS * 2];
        int cur_col, cur_row, cur_on;
        if (dosbox_shown() && dosbox_text(cells, &cur_col, &cur_row, &cur_on)) {
            /* a text mode: the machine draws the cells with its own font,
             * so DOS's prompt and the machine's are the same thing */
            src = dos_render_text(cells, cur_col, cur_row, cur_on);
            cw = DOS_W;
            ch = DOS_H;
            cl = DOS_H;
            text = 1;
        } else if (dosbox_shown())
            held = (src = dosbox_frame(&cw, &ch, &cl)) != NULL;
        else if (corehost_running())
            src = corehost_frame(&cw, &ch, &cl);
        if (src) {
            gpu_set_tube(a.gpu, src, cw, ch);
            k.crt_lines = cl;
            k.crt_cols = cw;
            /* game art: hard pixels; a real DOS's text, even strokes */
            k.sharp_text = (text || (held && dosbox_text_mode())) ? 1.0f : 0.0f;
        }
        else {
            gpu_set_tube(a.gpu, dos_render(), DOS_W, DOS_H);
            /* one scanline per texture row, border included - the beam
             * swept the overscan at the same pitch as the 400 lines of
             * text inside it */
            k.crt_lines = DOS_H;
            k.crt_cols = DOS_W;
            k.sharp_text = 1.0f;
        } /* text: even stroke weights */

        show_knobs(a.gpu, &k, &last_b, &last_c);
        k.aperture_r = L.aperture_r; /* match the chassis hole */
        gpu_draw(a.gpu, L.tube_x / a.W, 1.0f - (L.tube_y + L.tube_h) / a.H, L.tube_w / a.W,
                 L.tube_h / a.H, &k, t);
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
