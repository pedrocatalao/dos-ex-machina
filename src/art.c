/* art.c — the picture in the navigator's right pane.
 *
 * Fetched once and kept on disk, keyed by the hash the catalogue gives it.
 * Keying by hash rather than by name means staleness is not a question
 * anyone has to answer: a different image is a different file, and the old
 * one simply stops being asked for.
 *
 * One image is held decoded at a time - the panel only ever shows the
 * selected entry, and a 960x720 RGBA buffer is 2.6 MB. */
#include "art.h"
#include "library.h"
#include "net.h"
#include "png.h"
#include "sha256.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The artwork cache: what is decoded now, and the fetch in flight */
static struct {
    char cur_key[65]; /* hash of what is decoded now */
    uint8_t *cur_px;
    int cur_w, cur_h;
    SDL_Thread *th;
    volatile int busy;
    char job_url[CAT_URL_MAX], job_path[LIB_PATH], job_sha[65];
} art;

static void cache_path(const char *sha, char *out, size_t n) {
    char dir[LIB_PATH];
    snprintf(dir, sizeof dir, "%sart", lib_root());
    SDL_CreateDirectory(dir);
    snprintf(out, n, "%s%c%.16s.png", dir, DXM_SEP, sha);
}

static int SDLCALL fetch(void *ud) {
    (void)ud;
    char err[160];
    if (net_get_file(art.job_url, art.job_path, NULL, NULL, NULL, err, sizeof err) != 0 ||
        !sha256_matches(art.job_path, art.job_sha)) {
        remove(art.job_path); /* a bad cache entry is worse than none */
    }
    art.busy = 0;
    return 0;
}

const uint8_t *art_get(const cat_file *f, int *w, int *h) {
    if (!f || !f->url[0])
        return NULL;
    const char *key = f->sha256[0] ? f->sha256 : f->url;

    if (art.cur_px && !strncmp(art.cur_key, key, sizeof art.cur_key - 1)) {
        *w = art.cur_w;
        *h = art.cur_h;
        return art.cur_px;
    }
    if (art.busy)
        return NULL; /* a fetch is already in flight */
    if (art.th) {
        SDL_WaitThread(art.th, NULL);
        art.th = NULL;
    }

    char path[LIB_PATH];
    cache_path(key, path, sizeof path);

    SDL_PathInfo st;
    if (SDL_GetPathInfo(path, &st)) {
        /* On disk already - decode on this thread.  It is a few
         * milliseconds once, not every frame, because the result is held. */
        char err[160];
        int nw, nh;
        uint8_t *px = png_load(path, &nw, &nh, err, sizeof err);
        if (px) {
            free(art.cur_px);
            art.cur_px = px;
            art.cur_w = nw;
            art.cur_h = nh;
            snprintf(art.cur_key, sizeof art.cur_key, "%s", key);
            *w = nw;
            *h = nh;
            return px;
        }
        remove(path); /* undecodable: fetch it again */
    }

    snprintf(art.job_url, sizeof art.job_url, "%s", f->url);
    snprintf(art.job_sha, sizeof art.job_sha, "%s", f->sha256);
    snprintf(art.job_path, sizeof art.job_path, "%s", path);
    art.busy = 1;
    art.th = SDL_CreateThread(fetch, "art", NULL);
    if (!art.th)
        art.busy = 0;
    return NULL; /* the placeholder shows meanwhile */
}
