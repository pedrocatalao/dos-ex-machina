/* install.c — downloading a game, on a worker thread.
 *
 * The machine has to stay alive while this runs: the fan keeps turning, the
 * navigator keeps drawing, and the progress bar is only honest if the frame
 * loop is still running.  So the work happens on a thread and the UI reads a
 * status block, rather than the download blocking the world.
 *
 * Order matters.  The module is fetched and VERIFIED before anything else,
 * because it is the part DXM will execute.  The data comes second, and only
 * once both are in place is the directory a game - up to that point it is a
 * half-finished install that the library will report as such. */
#include "install.h"
#include "catalog.h"
#include "library.h"
#include "net.h"
#include "sha256.h"
#include "unzip.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static inst_status  st;
static SDL_Mutex   *mu;
static SDL_Thread  *th;
static volatile int cancel;
static cat_game     job;

static void set_stage(const char *what, int step, int steps) {
    SDL_LockMutex(mu);
    snprintf(st.stage, sizeof st.stage, "%s", what);
    st.step = step; st.steps = steps; st.frac = 0.0;
    SDL_UnlockMutex(mu);
}
static void on_progress(void *ud, double got, double total) {
    (void)ud;
    SDL_LockMutex(mu);
    st.got = got; st.total = total;
    st.frac = total > 0 ? got / total : 0.0;
    SDL_UnlockMutex(mu);
}
static void fail(const char *fmt, const char *a) {
    SDL_LockMutex(mu);
    snprintf(st.err, sizeof st.err, fmt, a ? a : "");
    st.state = INST_FAILED;
    SDL_UnlockMutex(mu);
}

static int SDLCALL worker(void *ud) {
    (void)ud;
    char dir[LIB_PATH], path[LIB_PATH], data[LIB_PATH], zip[LIB_PATH];
    char err[192];
    /* the three the user waits through */
    const int STEPS = 3;

    if (lib_make_dir(job.id, dir, sizeof dir) != 0) {
        fail("cannot create the game directory%s", NULL);
        return 0;
    }

    /* ---- 1. the module ---- */
    set_stage("Downloading game", 1, STEPS);
    snprintf(path, sizeof path, "%s%cgame.dxm", dir, DXM_SEP);
    if (net_get_file(job.module.url, path, on_progress, NULL, &cancel,
                     err, sizeof err) != 0) {
        fail("%s", err);
        return 0;
    }
    set_stage("Verifying", 1, STEPS);
    if (!sha256_matches(path, job.module.sha256)) {
        remove(path);
        fail("the download does not match its checksum%s", NULL);
        return 0;
    }

    /* ---- 2. the data ---- */
    snprintf(data, sizeof data, "%s%cdata", dir, DXM_SEP);
    SDL_CreateDirectory(data);
    if (job.data.url[0]) {
        set_stage("Downloading data", 2, STEPS);
        snprintf(zip, sizeof zip, "%s%cdata.zip", dir, DXM_SEP);
        if (net_get_file(job.data.url, zip, on_progress, NULL, &cancel,
                         err, sizeof err) != 0) {
            fail("%s", err);
            return 0;
        }
        set_stage("Verifying", 2, STEPS);
        if (!sha256_matches(zip, job.data.sha256)) {
            remove(zip);
            fail("the game data does not match its checksum%s", NULL);
            return 0;
        }
        /* ---- 3. unpack ---- */
        set_stage("Installing", 3, STEPS);
        char destdir[LIB_PATH];
        snprintf(destdir, sizeof destdir, "%s%c", data, DXM_SEP);
        int n = 0;
        if (unzip_extract(zip, destdir, &n, err, sizeof err) != 0) {
            remove(zip);
            fail("%s", err);
            return 0;
        }
        /* The archive STAYS, beside the data it produced.  It is what a
         * reset restores from - saved games and settings wiped, the game
         * back as installed - with no network involved. */
    }

    SDL_LockMutex(mu);
    st.frac = 1.0;
    st.state = INST_DONE;
    SDL_UnlockMutex(mu);
    return 0;
}

int install_start(const cat_game *g) {
    if (!mu) mu = SDL_CreateMutex();
    if (st.state == INST_RUNNING) return -1;
    if (th) { SDL_WaitThread(th, NULL); th = NULL; }
    job = *g;
    memset(&st, 0, sizeof st);
    st.state = INST_RUNNING;
    snprintf(st.id, sizeof st.id, "%s", g->id);
    snprintf(st.title, sizeof st.title, "%s", g->title);
    snprintf(st.stage, sizeof st.stage, "Starting");
    cancel = 0;
    th = SDL_CreateThread(worker, "install", NULL);
    if (!th) { st.state = INST_FAILED;
               snprintf(st.err, sizeof st.err, "cannot start the download");
               return -1; }
    return 0;
}

void install_cancel(void) { cancel = 1; }

void install_poll(inst_status *out) {
    if (!mu) { memset(out, 0, sizeof *out); return; }
    SDL_LockMutex(mu);
    *out = st;
    SDL_UnlockMutex(mu);
    /* Reap the thread once, on the frame the caller first sees the result -
     * so the next install can start without a stale handle around. */
    if (th && (out->state == INST_DONE || out->state == INST_FAILED)) {
        SDL_WaitThread(th, NULL);
        th = NULL;
    }
}

void install_clear(void) {
    if (!mu) return;
    SDL_LockMutex(mu);
    if (st.state != INST_RUNNING) memset(&st, 0, sizeof st);
    SDL_UnlockMutex(mu);
}
