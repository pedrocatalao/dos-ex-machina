/* art.c — the artwork: fetched from where the catalogue says, kept in the
 * preferences, fitted to the frames it is shown in, and quantised together
 * with everything else on screen.
 *
 * The screen shows one picture at a time, the chosen title's, in the
 * ART_COLOURS entries the interface leaves it: fitted by median cut on its
 * histogram whenever it changes (the same algorithm as src/setup/banner.c).
 *
 * Fetching happens on a thread of its own, one file at a time, into
 * artwork/<catalogue>/<CATEGORY>/<ID>-<hash>.<ext> in the preferences; the
 * screen asks every frame and gets whatever has arrived. */
#include "internal.h"
#include "net.h"
#include "sha256.h"
#include "log.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF /* what archive.org keeps a DOS screenshot as */
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h" /* the implementation is in banner.c */
#pragma GCC diagnostic pop

static char pref[1024];
static int offline; /* neither the network nor the cache: empty frames */

void art_bind(const char *pref_dir) {
    snprintf(pref, sizeof pref, "%s", pref_dir ? pref_dir : "./");
}
void art_offline(int on) {
    offline = on;
}

/* ---- where a picture is kept ------------------------------------------ */

static const char *ext_of(const char *url) {
    const char *dot = strrchr(url, '.'), *slash = strrchr(url, '/');
    if (dot && (!slash || dot > slash) && strlen(dot) <= 5)
        return dot;
    return ".img";
}

/* The file is named for the picture as well as the title: the first part of
 * its hash, when the catalogue gives one.  A catalogue that changes a
 * title's picture then names a file the cache has not got, and the new one
 * is fetched, rather than the old one being shown for ever. */
static void cache_path(const shelf *s, const cat_title *t, char *out, size_t n) {
    /* A picture that came off this computer has no address, only a hash,
     * and lives by it: shared between whatever titles chose the same file,
     * and undisturbed when a title is renamed or put in another category. */
    if (!t->artwork.url[0] && t->artwork.sha256[0]) {
        snprintf(out, n, "%sartwork/.local/%s.img", pref, t->artwork.sha256);
        return;
    }
    char tag[10] = "";
    if (t->artwork.sha256[0])
        snprintf(tag, sizeof tag, "-%.8s", t->artwork.sha256);
    snprintf(out, n, "%sartwork/%s/%s/%s%s%s", pref, s->cat.id, t->category, t->id, tag,
             ext_of(t->artwork.url));
}

static int exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

/* ---- the fetcher ------------------------------------------------------- */

#define QUEUE 8
typedef struct {
    char url[CAT_URL], path[1400], sha[65];
} job;
static struct {
    SDL_Mutex *mx;
    SDL_Thread *th;
    job q[QUEUE];
    int n;
    volatile int arrived; /* something landed since the screen last looked */
    volatile int failed;  /* fetches that did not work, so they are not retried */
    char failed_path[8][1400];
    int nfailed;
} F;

static int SDLCALL fetcher(void *ud) {
    (void)ud;
    for (;;) {
        job j;
        SDL_LockMutex(F.mx);
        if (!F.n) {
            F.th = NULL; /* nothing to do: the thread ends, the next want starts one */
            SDL_UnlockMutex(F.mx);
            return 0;
        }
        j = F.q[0];
        memmove(&F.q[0], &F.q[1], sizeof(job) * (size_t)(F.n - 1));
        F.n--;
        SDL_UnlockMutex(F.mx);

        /* the directories on the way */
        char dir[1400];
        snprintf(dir, sizeof dir, "%s", j.path);
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = 0;
            SDL_CreateDirectory(dir);
        }
        char err[160];
        if (net_get_file(j.url, j.path, NULL, NULL, NULL, err, sizeof err) != 0) {
            dxm_log("catalog: artwork %s: %s", j.url, err);
            SDL_LockMutex(F.mx);
            if (F.nfailed < 8)
                snprintf(F.failed_path[F.nfailed++], 1400, "%s", j.path);
            SDL_UnlockMutex(F.mx);
            continue;
        }
        if (j.sha[0] && !sha256_matches(j.path, j.sha)) {
            dxm_log("catalog: artwork %s is not what the catalogue says it is", j.url);
            remove(j.path);
            SDL_LockMutex(F.mx);
            if (F.nfailed < 8)
                snprintf(F.failed_path[F.nfailed++], 1400, "%s", j.path);
            SDL_UnlockMutex(F.mx);
            continue;
        }
        F.arrived = 1;
    }
}

static int queued_or_failed(const char *path) {
    for (int i = 0; i < F.n; i++)
        if (!strcmp(F.q[i].path, path))
            return 1;
    for (int i = 0; i < F.nfailed; i++)
        if (!strcmp(F.failed_path[i], path))
            return 1;
    return 0;
}

static void fetch(const shelf *s, const cat_title *t, const char *path) {
    if (!F.mx)
        F.mx = SDL_CreateMutex();
    SDL_LockMutex(F.mx);
    if (F.n < QUEUE && !queued_or_failed(path)) {
        job *j = &F.q[F.n++];
        snprintf(j->url, sizeof j->url, "%s", t->artwork.url);
        snprintf(j->path, sizeof j->path, "%s", path);
        snprintf(j->sha, sizeof j->sha, "%s", t->artwork.sha256);
        if (!F.th)
            F.th = SDL_CreateThread(fetcher, "artwork", NULL);
        if (F.th)
            SDL_DetachThread(F.th);
    }
    SDL_UnlockMutex(F.mx);
    (void)s;
}

/* ---- decoding and fitting --------------------------------------------- */

/* A picture read from disk and fitted into a box, keeping its shape: the
 * average of the source pixels each destination pixel covers, which is what
 * the tube would have blurred them into anyway. */
typedef struct {
    int w, h;
    uint8_t *rgb;
} fitted;

static int fit_file(const char *path, int box_w, int box_h, fitted *out) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (n > 0 && n < 16L * 1024 * 1024) ? malloc((size_t)n) : NULL;
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);
    int sw, sh, comp;
    stbi_uc *src = stbi_load_from_memory(buf, (int)n, &sw, &sh, &comp, 3);
    free(buf);
    if (!src) {
        dxm_log("catalog: %s will not open: %s", path, stbi_failure_reason());
        return 0;
    }
    /* the size inside the box */
    int dw = box_w, dh = sh * box_w / sw;
    if (dh > box_h) {
        dh = box_h;
        dw = sw * box_h / sh;
    }
    if (dw < 1)
        dw = 1;
    if (dh < 1)
        dh = 1;
    uint8_t *dst = malloc((size_t)dw * dh * 3);
    if (!dst) {
        stbi_image_free(src);
        return 0;
    }
    for (int y = 0; y < dh; y++) {
        int y0 = y * sh / dh, y1 = (y + 1) * sh / dh;
        if (y1 <= y0)
            y1 = y0 + 1;
        for (int x = 0; x < dw; x++) {
            int x0 = x * sw / dw, x1 = (x + 1) * sw / dw;
            if (x1 <= x0)
                x1 = x0 + 1;
            long sum[3] = {0, 0, 0}, cnt = 0;
            for (int j = y0; j < y1 && j < sh; j++)
                for (int i = x0; i < x1 && i < sw; i++) {
                    const stbi_uc *p = src + ((size_t)j * sw + i) * 3;
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                    cnt++;
                }
            uint8_t *q = dst + ((size_t)y * dw + x) * 3;
            for (int k = 0; k < 3; k++)
                q[k] = (uint8_t)(cnt ? sum[k] / cnt : 0);
        }
    }
    stbi_image_free(src);
    out->w = dw;
    out->h = dh;
    out->rgb = dst;
    return 1;
}

/* ---- a picture chosen off this computer --------------------------------- */

int art_adopt(const char *host_path, char sha[65], char *err, size_t n) {
    FILE *f = fopen(host_path, "rb");
    if (!f) {
        snprintf(err, n, "that file will not open");
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (sz > 0 && sz < 16L * 1024 * 1024) ? malloc((size_t)sz) : NULL;
    int read_ok = buf && fread(buf, 1, (size_t)sz, f) == (size_t)sz;
    fclose(f);
    if (!read_ok) {
        free(buf);
        snprintf(err, n, "%s",
                 sz >= 16L * 1024 * 1024 ? "that file is too big to be a picture"
                                         : "that file will not read");
        return 0;
    }
    /* Asked before it is kept rather than after: a file that turns out not
     * to be a picture should be refused where somebody can still pick
     * another, not silently leave an empty box on the catalogue screen. */
    int w, h, comp;
    if (!stbi_info_from_memory(buf, (int)sz, &w, &h, &comp)) {
        free(buf);
        snprintf(err, n, "that is not a picture the machine can open");
        return 0;
    }
    sha256 c;
    sha256_init(&c);
    sha256_update(&c, buf, (size_t)sz);
    sha256_final(&c, sha);

    char dir[1100], path[1400];
    snprintf(dir, sizeof dir, "%sartwork/.local", pref);
    SDL_CreateDirectory(dir);
    snprintf(path, sizeof path, "%s/%s.img", dir, sha);
    if (!exists(path)) { /* the same picture twice is the same file */
        FILE *o = fopen(path, "wb");
        int wrote = o && fwrite(buf, 1, (size_t)sz, o) == (size_t)sz;
        if (o)
            fclose(o);
        if (!wrote) {
            remove(path);
            free(buf);
            snprintf(err, n, "the picture could not be kept");
            return 0;
        }
    }
    free(buf);
    dxm_log("catalog: took %s as artwork, %dx%d", host_path, w, h);
    return 1;
}

/* ---- the picture on screen, and its palette ----------------------------- */

#define CELLS (32 * 32 * 32)
#define SLOTS 1 /* the machinery fits a set; the screen has one picture in it */

static struct {
    char key[SLOTS][CAT_ID * 2 + 8]; /* which title is in each slot */
    fitted img[SLOTS];
    uint8_t *px[SLOTS];
    art_img out[SLOTS];
    uint8_t pal[ART_COLOURS * 3];
    int count[CELLS], cell[CELLS], ncell;
    uint8_t cmap[CELLS];
    int axis;
} A;

static int box_cmp(const void *a, const void *b) {
    int ca = *(const int *)a >> (A.axis * 5), cb = *(const int *)b >> (A.axis * 5);
    return (ca & 31) - (cb & 31);
}
static int cell_of(int r, int g, int b) {
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10);
}

typedef struct {
    int at, n;
    long pixels;
} cbox;

static void median_cut(int want) {
    cbox *boxes = calloc((size_t)want, sizeof *boxes);
    if (!boxes)
        return;
    int nb = 1;
    boxes[0].at = 0;
    boxes[0].n = A.ncell;
    for (int i = 0; i < A.ncell; i++)
        boxes[0].pixels += A.count[A.cell[i]];
    while (nb < want) {
        int pick = -1;
        for (int i = 0; i < nb; i++)
            if (boxes[i].n > 1 && (pick < 0 || boxes[i].pixels > boxes[pick].pixels))
                pick = i;
        if (pick < 0)
            break;
        cbox *bx = &boxes[pick];
        int lo[3] = {31, 31, 31}, hi[3] = {0, 0, 0};
        for (int i = bx->at; i < bx->at + bx->n; i++)
            for (int k = 0; k < 3; k++) {
                int v = (A.cell[i] >> (k * 5)) & 31;
                if (v < lo[k])
                    lo[k] = v;
                if (v > hi[k])
                    hi[k] = v;
            }
        A.axis = 0;
        for (int k = 1; k < 3; k++)
            if (hi[k] - lo[k] > hi[A.axis] - lo[A.axis])
                A.axis = k;
        qsort(&A.cell[bx->at], (size_t)bx->n, sizeof A.cell[0], box_cmp);
        long half = bx->pixels / 2, run = 0;
        int cut = 1;
        for (int i = 0; i < bx->n - 1; i++) {
            run += A.count[A.cell[bx->at + i]];
            if (run >= half) {
                cut = i + 1;
                break;
            }
        }
        long left = 0;
        for (int i = 0; i < cut; i++)
            left += A.count[A.cell[bx->at + i]];
        boxes[nb].at = bx->at + cut;
        boxes[nb].n = bx->n - cut;
        boxes[nb].pixels = bx->pixels - left;
        bx->n = cut;
        bx->pixels = left;
        nb++;
    }
    for (int i = 0; i < nb; i++) {
        long sr = 0, sg = 0, sb = 0, n = 0;
        for (int j = boxes[i].at; j < boxes[i].at + boxes[i].n; j++) {
            int c = A.cell[j];
            long w = A.count[c];
            sr += (((c & 31) << 3) | 4) * w;
            sg += ((((c >> 5) & 31) << 3) | 4) * w;
            sb += ((((c >> 10) & 31) << 3) | 4) * w;
            n += w;
            A.cmap[c] = (uint8_t)i;
        }
        if (!n)
            n = 1;
        A.pal[i * 3] = (uint8_t)(sr / n);
        A.pal[i * 3 + 1] = (uint8_t)(sg / n);
        A.pal[i * 3 + 2] = (uint8_t)(sb / n);
    }
    free(boxes);
}

static void refit(void) {
    /* Whatever was on screen goes first.  A set with no pictures in it at
     * all - a title without artwork, or one still on its way - would
     * otherwise leave the last title's picture showing, drawn from pixels
     * that have already been freed. */
    for (int s = 0; s < SLOTS; s++) {
        free(A.px[s]);
        A.px[s] = NULL;
        A.out[s].w = A.out[s].h = 0;
        A.out[s].px = NULL;
    }
    memset(A.count, 0, sizeof A.count);
    A.ncell = 0;
    for (int s = 0; s < SLOTS; s++) {
        const fitted *f = &A.img[s];
        for (int i = 0; i < f->w * f->h; i++) {
            const uint8_t *p = f->rgb + i * 3;
            int c = cell_of(p[0], p[1], p[2]);
            if (!A.count[c])
                A.cell[A.ncell++] = c;
            A.count[c]++;
        }
    }
    if (!A.ncell)
        return;
    median_cut(ART_COLOURS);
    for (int s = 0; s < SLOTS; s++) {
        const fitted *f = &A.img[s];
        free(A.px[s]);
        A.px[s] = f->w ? malloc((size_t)f->w * f->h) : NULL;
        if (A.px[s])
            for (int i = 0; i < f->w * f->h; i++) {
                const uint8_t *p = f->rgb + i * 3;
                A.px[s][i] = A.cmap[cell_of(p[0], p[1], p[2])];
            }
        A.out[s].w = A.px[s] ? f->w : 0;
        A.out[s].h = A.px[s] ? f->h : 0;
        A.out[s].px = A.px[s];
    }
}

/* Put the title's picture in a slot: from the cache if it is there,
 * otherwise nothing yet and a fetch on its way.  Returns 1 if the slot's
 * contents changed. */
static int slot(int n, const shelf *s, const cat_title *t, int box_w, int box_h) {
    char key[CAT_ID * 2 + 8] = "";
    char path[1400] = "";
    if (t && (t->artwork.url[0] || t->artwork.sha256[0]) && !offline) {
        snprintf(key, sizeof key, "%s/%s/%s", s->cat.id, t->category, t->id);
        cache_path(s, t, path, sizeof path);
        if (!exists(path)) {
            /* One with only a hash is one this machine either has or has
             * not: there is nowhere to fetch it from, and the plate stands
             * in - which is what somebody else's machine will show. */
            if (t->artwork.url[0])
                fetch(s, t, path);
            key[0] = 0; /* nothing to show for it yet */
        }
    }
    if (!strcmp(A.key[n], key))
        return 0;
    snprintf(A.key[n], sizeof A.key[n], "%s", key);
    free(A.img[n].rgb);
    memset(&A.img[n], 0, sizeof A.img[n]);
    if (key[0] && !fit_file(path, box_w, box_h, &A.img[n]))
        A.key[n][0] = 0;
    return 1;
}

/* Something being judged, rather than a title's own.  It is held by path
 * because it has no title yet: it is a file that arrived a moment ago and
 * may never be taken. */
static char preview[1400];

int art_preview(const char *path) {
    if (!path || !path[0]) {
        if (!preview[0])
            return 0;
        preview[0] = 0;
    } else {
        if (!strcmp(preview, path))
            return 0;
        snprintf(preview, sizeof preview, "%s", path);
    }
    free(A.img[0].rgb);
    memset(&A.img[0], 0, sizeof A.img[0]);
    /* The key is cleared either way, so that giving the box back makes
     * art_want fit the chosen title again rather than see no change. */
    A.key[0][0] = 0;
    if (preview[0] && fit_file(preview, ART_W, ART_H, &A.img[0]))
        snprintf(A.key[0], sizeof A.key[0], "*");
    refit();
    return 1;
}

int art_want(const shelf *s, const cat_title *t) {
    if (preview[0])
        return 0; /* whoever is choosing one owns the box */
    int changed = F.arrived;
    F.arrived = 0;
    if (changed) /* whatever landed: look at every slot again */
        for (int i = 0; i < SLOTS; i++)
            A.key[i][0] = 0;
    changed |= slot(0, s, t, ART_W, ART_H);
    if (changed)
        refit();
    return changed;
}

const art_img *art_picture(void) {
    return &A.out[0];
}
const uint8_t *art_palette(void) {
    return A.pal;
}
