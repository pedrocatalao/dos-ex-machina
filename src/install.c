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
#include "catalogue.h"
#include "library.h"
#include "net.h"
#include "sha256.h"
#include "unzip.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* The install in progress, shared with its worker thread */
static struct {
    inst_status st;
    SDL_Mutex *mu;
    SDL_Thread *th;
    volatile int cancel;
    cat_game job;
    int data_only; /* skip the module: a reset */
} inst;

static void set_stage(const char *what, int step, int steps) {
    SDL_LockMutex(inst.mu);
    snprintf(inst.st.stage, sizeof inst.st.stage, "%s", what);
    inst.st.step = step;
    inst.st.steps = steps;
    inst.st.frac = 0.0;
    SDL_UnlockMutex(inst.mu);
}
static void on_progress(void *ud, double got, double total) {
    (void)ud;
    SDL_LockMutex(inst.mu);
    inst.st.got = got;
    inst.st.total = total;
    inst.st.frac = total > 0 ? got / total : 0.0;
    SDL_UnlockMutex(inst.mu);
}
static void fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void fail(const char *fmt, ...) {
    SDL_LockMutex(inst.mu);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(inst.st.err, sizeof inst.st.err, fmt, ap);
    va_end(ap);
    inst.st.state = INST_FAILED;
    SDL_UnlockMutex(inst.mu);
}

static int SDLCALL worker(void *ud) {
    (void)ud;
    char dir[LIB_PATH], path[LIB_PATH], data[LIB_PATH], zip[LIB_PATH];
    char err[192];
    /* the three the user waits through */
    const int STEPS = 3;

    if (lib_make_dir(inst.job.id, dir, sizeof dir) != 0) {
        fail("cannot create the game directory");
        return 0;
    }

    /* ---- 1. the module ---- */
    if (!inst.data_only) {
        set_stage("Downloading game", 1, STEPS);
        snprintf(path, sizeof path, "%s%cgame.dxm", dir, DXM_SEP);
        if (net_get_file(inst.job.module.url, path, on_progress, NULL, &inst.cancel, err,
                         sizeof err) != 0) {
            fail("%s", err);
            return 0;
        }
        set_stage("Verifying", 1, STEPS);
        if (!sha256_matches(path, inst.job.module.sha256)) {
            remove(path);
            fail("the download does not match its checksum");
            return 0;
        }
    }

    /* ---- 2. the data ---- */
    snprintf(data, sizeof data, "%s%cdata", dir, DXM_SEP);
    SDL_CreateDirectory(data);
    if (inst.job.data.url[0]) {
        set_stage("Downloading data", 2, STEPS);
        snprintf(zip, sizeof zip, "%s%cdata.zip", dir, DXM_SEP);
        if (net_get_file(inst.job.data.url, zip, on_progress, NULL, &inst.cancel, err,
                         sizeof err) != 0) {
            fail("%s", err);
            return 0;
        }
        set_stage("Verifying", 2, STEPS);
        if (!sha256_matches(zip, inst.job.data.sha256)) {
            remove(zip);
            fail("the game data does not match its checksum");
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

    /* Which release this is, written beside the module.  The catalogue
     * knows the CURRENT release; only this file knows the one on disk. */
    if (inst.job.version[0]) {
        snprintf(path, sizeof path, "%s%cversion", dir, DXM_SEP);
        FILE *f = fopen(path, "wb");
        if (f) {
            fprintf(f, "%s\n", inst.job.version);
            fclose(f);
        }
    }

    SDL_LockMutex(inst.mu);
    inst.st.frac = 1.0;
    inst.st.state = INST_DONE;
    SDL_UnlockMutex(inst.mu);
    return 0;
}

static int start(const cat_game *g, int only_data);
int install_start(const cat_game *g) {
    return start(g, 0);
}
int install_start_data(const cat_game *g) {
    return start(g, 1);
}
static int start(const cat_game *g, int only_data) {
    if (!inst.mu)
        inst.mu = SDL_CreateMutex();
    if (inst.st.state == INST_RUNNING)
        return -1;
    inst.data_only = only_data;
    if (inst.th) {
        SDL_WaitThread(inst.th, NULL);
        inst.th = NULL;
    }
    inst.job = *g;
    memset(&inst.st, 0, sizeof inst.st);
    inst.st.state = INST_RUNNING;
    snprintf(inst.st.id, sizeof inst.st.id, "%s", g->id);
    snprintf(inst.st.title, sizeof inst.st.title, "%s", g->title);
    snprintf(inst.st.stage, sizeof inst.st.stage, "Starting");
    inst.cancel = 0;
    inst.th = SDL_CreateThread(worker, "install", NULL);
    if (!inst.th) {
        inst.st.state = INST_FAILED;
        snprintf(inst.st.err, sizeof inst.st.err, "cannot start the download");
        return -1;
    }
    return 0;
}

void install_cancel(void) {
    inst.cancel = 1;
}

void install_poll(inst_status *out) {
    if (!inst.mu) {
        memset(out, 0, sizeof *out);
        return;
    }
    SDL_LockMutex(inst.mu);
    *out = inst.st;
    SDL_UnlockMutex(inst.mu);
    /* Reap the thread once, on the frame the caller first sees the result -
     * so the next install can start without a stale handle around. */
    if (inst.th && (out->state == INST_DONE || out->state == INST_FAILED)) {
        SDL_WaitThread(inst.th, NULL);
        inst.th = NULL;
    }
}

void install_clear(void) {
    if (!inst.mu)
        return;
    SDL_LockMutex(inst.mu);
    if (inst.st.state != INST_RUNNING)
        memset(&inst.st, 0, sizeof inst.st);
    SDL_UnlockMutex(inst.mu);
}
