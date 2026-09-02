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

static char      cur_key[65];        /* hash of what is decoded now */
static uint8_t  *cur_px;
static int       cur_w, cur_h;

static SDL_Thread  *th;
static volatile int busy;
static char         job_url[CAT_URL_MAX], job_path[LIB_PATH], job_sha[65];

static void cache_path(const char *sha, char *out, size_t n) {
    char dir[LIB_PATH];
    snprintf(dir, sizeof dir, "%sart", lib_root());
    SDL_CreateDirectory(dir);
    snprintf(out, n, "%s%c%.16s.png", dir, DXM_SEP, sha);
}

static int SDLCALL fetch(void *ud) {
    (void)ud;
    char err[160];
    if (net_get_file(job_url, job_path, NULL, NULL, NULL, err, sizeof err) != 0
     || !sha256_matches(job_path, job_sha)) {
        remove(job_path);                /* a bad cache entry is worse than none */
    }
    busy = 0;
    return 0;
}

const uint8_t *art_get(const cat_file *f, int *w, int *h) {
    if (!f || !f->url[0]) return NULL;
    const char *key = f->sha256[0] ? f->sha256 : f->url;

    if (cur_px && !strncmp(cur_key, key, sizeof cur_key - 1)) {
        *w = cur_w; *h = cur_h;
        return cur_px;
    }
    if (busy) return NULL;               /* a fetch is already in flight */
    if (th) { SDL_WaitThread(th, NULL); th = NULL; }

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
            free(cur_px);
            cur_px = px; cur_w = nw; cur_h = nh;
            snprintf(cur_key, sizeof cur_key, "%s", key);
            *w = nw; *h = nh;
            return px;
        }
        remove(path);                    /* undecodable: fetch it again */
    }

    snprintf(job_url,  sizeof job_url,  "%s", f->url);
    snprintf(job_sha,  sizeof job_sha,  "%s", f->sha256);
    snprintf(job_path, sizeof job_path, "%s", path);
    busy = 1;
    th = SDL_CreateThread(fetch, "art", NULL);
    if (!th) busy = 0;
    return NULL;                         /* the placeholder shows meanwhile */
}
