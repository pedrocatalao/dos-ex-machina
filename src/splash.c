/* splash.c — see splash.h. */
#include "splash.h"
#include "log.h"

static int SDLCALL chassis_worker(void *ud) {
    chassis_job *j = (chassis_job *)ud;
    Uint64 t0 = SDL_GetTicksNS();
    dxm_log("chassis worker: start, %dx%d", j->W, j->H);
    j->L = chassis_layout(j->W, j->H);
    dxm_log("chassis worker: layout done");
    j->px = chassis_render(&j->L, j->W, j->H);
    j->ms = (SDL_GetTicksNS() - t0) / 1000000;
    dxm_log("chassis worker: render %s in %llu ms", j->px ? "done" : "FAILED - no pixels",
            (unsigned long long)j->ms);
    j->done = 1;
    return 0;
}

SDL_Thread *chassis_build_begin(chassis_job *job, int W, int H) {
    memset(job, 0, sizeof *job);
    job->W = W;
    job->H = H;
    SDL_Thread *th = SDL_CreateThread(chassis_worker, "chassis", job);
    if (!th) {
        dxm_log("no worker thread (%s) - drawing inline", SDL_GetError());
        chassis_worker(job);
    }
    return th;
}

void chassis_build_join(SDL_Thread *th, chassis_job *job) {
    if (th)
        SDL_WaitThread(th, NULL);
    dxm_log("chassis %dx%d joined, %llu ms%s", job->W, job->H, (unsigned long long)job->ms,
            job->px ? "" : "  (FAILED - no pixels)");
}

int splash_show(app *a, const chassis_job *job) {
    /* The hold has a floor so a quick build does not flash the mark up
     * and away. */
    const double FADE_IN = 0.30, HOLD_MIN = 0.95, FADE_OUT = 0.45;
    int quit = 0;
    Uint64 s0 = SDL_GetTicksNS();
    int frames = 0;
    double beat = 0;
    for (;;) {
        SDL_Event se;
        while (SDL_PollEvent(&se))
            if (se.type == SDL_EVENT_QUIT)
                quit = 1;
        double e = (SDL_GetTicksNS() - s0) / 1e9;
        float al = (float)(e < FADE_IN ? e / FADE_IN : 1.0);
        gpu_draw_splash(a->gpu, al * al * (3.0f - 2.0f * al)); /* ease, no linear ramp */
        SDL_GL_SwapWindow(a->win);
        if (++frames == 1)
            dxm_log("splash: first frame on screen");
        /* a heartbeat, so a log from a machine that never leaves the
         * splash shows whether the swaps come back and the worker ends */
        if (e - beat >= 2.0) {
            beat = e;
            dxm_log("splash: %.1f s, %d frames, worker %s", e, frames,
                    job->done ? "done" : "still running");
        }
        if (quit)
            break;
        if (e >= HOLD_MIN && job->done)
            break;
    }
    dxm_log("splash: over after %.2f s, %d frames", (SDL_GetTicksNS() - s0) / 1e9, frames);
    Uint64 f0 = SDL_GetTicksNS();
    while (!quit) {
        SDL_Event se;
        while (SDL_PollEvent(&se))
            if (se.type == SDL_EVENT_QUIT)
                quit = 1;
        double e = (SDL_GetTicksNS() - f0) / 1e9;
        if (e >= FADE_OUT)
            break;
        float al = (float)(1.0 - e / FADE_OUT);
        gpu_draw_splash(a->gpu, al * al * (3.0f - 2.0f * al));
        SDL_GL_SwapWindow(a->win);
    }
    return quit;
}
