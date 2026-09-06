/* corehost.c — runs one core on its own thread and implements dxm_host.
 * The game owns its control flow (SPEC §4.1), so synchronisation happens at
 * the plat_present() boundary and nowhere else. */
#include "corehost.h"
#include "coreload.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* SDL's threading rather than pthreads: DXM has to build on Windows too,
 * and SDL is already linked here.  The mutexes have no static initialiser,
 * so they are created in corehost_start and every use guards on NULL - the
 * audio callback can fire before a core has ever been started. */
#define NKEYS 256
/* The running core: its thread, frames, keys and audio hooks */
static struct {
    SDL_Thread *th;
    SDL_Mutex *mu;
    volatile int running, quit_req;
    char cur_data[1024], cur_pref[1026];
    uint8_t rgb[2][320 * 400 * 3]; /* double buffer, generous for 640x400 */
    int fw[2], fh[2], flines[2];
    volatile int front;
    int back;
    volatile unsigned char keys[NKEYS];
    int chq[64];
    volatile int chq_r, chq_w;
    /* No core is linked in: a game is always a module opened at run time,
     * so these start NULL and corehost_start refuses until one is chosen. */
    dxm_core_main_fn f_main;
    dxm_core_audio_fn f_audio;
    /* The core's one lock lives here, not in the core: the shell owns the *thread, and a core
     * shipped as a loadable module must not link a threading *library of its own. */
    SDL_Mutex *game_mu;
} core = {.front = -1};
static const dxm_core_info *cur_info;

void corehost_use_module(const dxm_module *m) {
    core.f_main = (m && m->handle) ? m->main_fn : NULL;
    core.f_audio = (m && m->handle) ? m->audio_fn : NULL;
}

static double now_s(void) {
    return (double)SDL_GetTicksNS() * 1e-9;
}
static void h_present(const dxm_frame *f) {
    const dxm_mode *m = f->mode;
    int n = m->w * m->h;
    if ((size_t)n * 3 > sizeof core.rgb[0])
        return;
    uint8_t *d = core.rgb[core.back];
    if (m->format == DXM_FB_INDEX8 && f->palette) {
        const uint8_t *pal = f->palette;
        for (int i = 0; i < n; i++) {
            const uint8_t *c = pal + f->pixels[i] * 3;
            d[i * 3] = c[0];
            d[i * 3 + 1] = c[1];
            d[i * 3 + 2] = c[2];
        }
    } else
        memcpy(d, f->pixels, (size_t)n * 3);
    core.fw[core.back] = m->w;
    core.fh[core.back] = m->h;
    core.flines[core.back] = m->crt_lines;
    SDL_LockMutex(core.mu);
    core.front = core.back;
    core.back = 1 - core.back;
    SDL_UnlockMutex(core.mu);
    /* SPEC 4.1: pace at the present boundary.  The game's wait-loops spin on
     * the 36.4Hz tick calling present each iteration; without this they
     * busy-burn a core publishing hundreds of identical frames.  A short
     * sleep keeps tick resolution (27ms period) while cooling the loop.
     * Game SPEED is unaffected either way - it is wall-clock tick driven. */
    SDL_DelayNS(2500000);
}
static int h_key_down(int sc) {
    return sc >= 0 && sc < NKEYS ? core.keys[sc] : 0;
}
static int h_getch(void) {
    if (core.chq_r == core.chq_w)
        return 0;
    int v = core.chq[core.chq_r];
    core.chq_r = (core.chq_r + 1) % 64;
    return v;
}
static int h_should_quit(void) {
    return core.quit_req;
}
static double h_now(void) {
    return now_s();
}
static void h_sleep(int ms) {
    SDL_Delay((Uint32)ms);
}
static void h_log(const char *m) {
    fprintf(stderr, "[core] %s\n", m);
}

static void h_lock(void) {
    if (core.game_mu)
        SDL_LockMutex(core.game_mu);
}
static void h_unlock(void) {
    if (core.game_mu)
        SDL_UnlockMutex(core.game_mu);
}

static dxm_host HOST = {h_present, h_key_down, h_getch,  h_should_quit, h_now, h_sleep,
                        h_log,     h_lock,     h_unlock, NULL,          NULL};

static int SDLCALL thread_main(void *ud) {
    (void)ud;
    core.f_main(&HOST, core.cur_data);
    core.running = 0;
    return 0;
}
int corehost_start(const dxm_core_info *info, const char *data_dir) {
    if (core.running)
        return -1;
    if (!core.f_main) {
        fprintf(stderr, "[dxm] no core chosen - corehost_use_module first\n");
        return -1;
    }
    if (!info || info->abi != DXM_ABI) {
        fprintf(stderr, "[dxm] core ABI %d, this build speaks %d - refusing\n",
                info ? info->abi : -1, DXM_ABI);
        return -1;
    }
    cur_info = info;
    snprintf(core.cur_data, sizeof core.cur_data, "%s", data_dir);
    /* plat_pref_path() is concatenated directly by callers (skyroads'
     * cfg_path() does "%sskyroads.cfg"), so it MUST end in a separator. */
    size_t n = strlen(core.cur_data);
    if (n && core.cur_data[n - 1] != '/' && n + 1 < sizeof core.cur_pref)
        snprintf(core.cur_pref, sizeof core.cur_pref, "%s/", core.cur_data);
    else
        snprintf(core.cur_pref, sizeof core.cur_pref, "%s", core.cur_data);
    HOST.data_dir = core.cur_data;
    HOST.pref_dir = core.cur_pref;
    for (size_t i = 0; i < sizeof core.keys / sizeof core.keys[0]; i++)
        core.keys[i] = 0;
    core.chq_r = core.chq_w = 0;
    core.front = -1;
    core.back = 0;
    core.quit_req = 0;
    core.running = 1;
    if (!core.mu)
        core.mu = SDL_CreateMutex();
    if (!core.game_mu)
        core.game_mu = SDL_CreateMutex();
    core.th = SDL_CreateThread(thread_main, "core", NULL);
    if (!core.th) {
        core.running = 0;
        return -1;
    }
    return 0;
}
void corehost_stop(void) {
    if (!core.running && core.front < 0)
        return;
    core.quit_req = 1;
    for (int i = 0; i < 200 && core.running; i++)
        SDL_Delay(10); /* ~2s (PORTING §3.6) */
    SDL_WaitThread(core.th, NULL);
    core.th = NULL;
    core.running = 0;
    core.front = -1;
    core.quit_req = 0;
    /* An audio callback that entered f_audio before running dropped is
     * still inside the module; take the mutex it holds so the caller can
     * unload the module knowing nothing is executing in it. */
    if (core.mu) {
        SDL_LockMutex(core.mu);
        SDL_UnlockMutex(core.mu);
    }
}
int corehost_running(void) {
    return core.running;
}
const uint8_t *corehost_frame(int *w, int *h, int *lines) {
    if (!core.mu)
        return NULL;
    SDL_LockMutex(core.mu);
    int f = core.front;
    SDL_UnlockMutex(core.mu);
    if (f < 0)
        return NULL;
    *w = core.fw[f];
    *h = core.fh[f];
    *lines = core.flines[f];
    return core.rgb[f];
}
void corehost_push_key(int sc, int down, int ch) {
    if (sc >= 0 && sc < NKEYS)
        core.keys[sc] = (unsigned char)down;
    if (down && ch) {
        int n = (core.chq_w + 1) % 64;
        if (n != core.chq_r) {
            core.chq[core.chq_w] = ch;
            core.chq_w = n;
        }
    }
}
void corehost_audio(int16_t *out, int nframes) {
    /* Only pull once the core has published a frame: before that it is still
     * inside audio_init, and rendering concurrently races the soundfont load.
     * The mutex closes the same window during teardown. */
    SDL_LockMutex(core.mu);
    int ok = core.running && core.front >= 0;
    if (ok && core.f_audio)
        core.f_audio(out, nframes);
    else
        memset(out, 0, (size_t)nframes * 4);
    SDL_UnlockMutex(core.mu);
}
