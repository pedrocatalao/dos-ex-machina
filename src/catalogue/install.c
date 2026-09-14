/* install.c — a title from the catalogue onto its drive (SPEC §8.4).
 *
 * Download, check the size and the hash, unpack into a scratch directory -
 * and, for a download that is an installer, unpack the archive the
 * catalogue names inside it - find the folder that holds the file the
 * catalogue says to run - that is the title, wherever the packer put it -
 * and move that folder to \<CATEGORY>\<ID> on the mount.  Then the scratch
 * and the download go.
 * All of it on a thread, one at a time, with the screen asking how far. */
#include "internal.h"
#include "net.h"
#include "unzip.h"
#include "sha256.h"
#include "log.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DOWNLOAD_MAX (200L * 1024 * 1024) /* past this it is not a DOS program */

/* ---- looking at the disk ---------------------------------------------- */

static int is_dir(const char *path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

typedef struct {
    const char *want;
    char found[1400];
    int is_directory;
} lookup;

static SDL_EnumerationResult find_ci(void *ud, const char *dirname, const char *fname) {
    lookup *l = ud;
    if (SDL_strcasecmp(fname, l->want) == 0) {
        snprintf(l->found, sizeof l->found, "%s%s", dirname, fname);
        return SDL_ENUM_SUCCESS;
    }
    return SDL_ENUM_CONTINUE;
}

/* The path of `name` in `dir`, whatever its case on disk, or 0. */
static int in_dir_ci(const char *dir, const char *name, char *out, size_t n) {
    lookup l = {name, "", 0};
    if (!SDL_EnumerateDirectory(dir, find_ci, &l) || !l.found[0])
        return 0;
    snprintf(out, n, "%s", l.found);
    return 1;
}

/* A relative path under `dir`, found a component at a time whatever the
 * case of each on disk - the catalogue writes OMF/OMF21.EXE, the archive may
 * have made it omf/Omf21.exe.  0 when some part of it is not there. */
static int path_ci(const char *dir, const char *rel, char *out, size_t n) {
    char at[1400], part[260];
    snprintf(at, sizeof at, "%s", dir);
    const char *p = rel;
    while (*p) {
        size_t k = 0;
        while (*p && *p != '/' && *p != '\\' && k + 1 < sizeof part)
            part[k++] = *p++;
        part[k] = 0;
        while (*p == '/' || *p == '\\')
            p++;
        if (!k)
            continue;
        char base[1410], found[1400];
        snprintf(base, sizeof base, "%s/", at);
        if (!in_dir_ci(base, part, found, sizeof found))
            return 0;
        snprintf(at, sizeof at, "%s", found);
    }
    snprintf(out, n, "%s", at);
    return 1;
}

void install_dir(const shelf *s, const cat_title *t, char *host, size_t hn, char *dos, size_t dn) {
    if (host)
        snprintf(host, hn, "%s%s/%s", s->mount, t->category, t->id);
    if (dos)
        snprintf(dos, dn, "\\%s\\%s", t->category, t->id);
}

int install_present(const shelf *s, const cat_title *t) {
    char dir[1400], found[1400];
    install_dir(s, t, dir, sizeof dir, NULL, 0);
    return is_dir(dir) && in_dir_ci(dir, t->run, found, sizeof found);
}

/* ---- the search for the title in what was unpacked -------------------- */

typedef struct {
    const char *want;
    char dir[1400];
    int depth;
} hunt;

static SDL_EnumerationResult hunt_cb(void *ud, const char *dirname, const char *fname);

/* The first directory, top down, that holds `want`: breadth is not needed,
 * a packer puts the game one folder deep at most, but depth is bounded so
 * a hostile archive cannot send this off for the afternoon. */
static int find_run(const char *dir, const char *want, char *out, size_t n, int depth) {
    char found[1400];
    if (in_dir_ci(dir, want, found, sizeof found)) {
        snprintf(out, n, "%s", dir);
        return 1;
    }
    if (depth >= 4)
        return 0;
    hunt h = {want, "", depth};
    SDL_EnumerateDirectory(dir, hunt_cb, &h);
    if (h.dir[0]) {
        snprintf(out, n, "%s", h.dir);
        return 1;
    }
    return 0;
}

static SDL_EnumerationResult hunt_cb(void *ud, const char *dirname, const char *fname) {
    hunt *h = ud;
    char path[1400];
    snprintf(path, sizeof path, "%s%s", dirname, fname);
    if (!is_dir(path))
        return SDL_ENUM_CONTINUE;
    char sub[1400];
    snprintf(sub, sizeof sub, "%s/", path);
    if (find_run(sub, h->want, h->dir, sizeof h->dir, h->depth + 1))
        return SDL_ENUM_SUCCESS;
    return SDL_ENUM_CONTINUE;
}

/* ---- removing what is left over ---------------------------------------- */

static SDL_EnumerationResult rm_cb(void *ud, const char *dirname, const char *fname) {
    (void)ud;
    char path[1400];
    snprintf(path, sizeof path, "%s%s", dirname, fname);
    if (is_dir(path)) {
        char sub[1400];
        snprintf(sub, sizeof sub, "%s/", path);
        SDL_EnumerateDirectory(sub, rm_cb, NULL);
    }
    SDL_RemovePath(path);
    return SDL_ENUM_CONTINUE;
}

static void rm_tree(const char *dir) {
    char sub[1400];
    snprintf(sub, sizeof sub, "%s/", dir);
    SDL_EnumerateDirectory(sub, rm_cb, NULL);
    SDL_RemovePath(dir);
}

/* ---- the job ----------------------------------------------------------- */

static struct {
    SDL_Mutex *mx;
    SDL_Thread *th;
    int busy, done; /* done: 1 well, -1 badly, taken once */
    volatile int cancel;
    float progress;
    char status[80], err[160];
    /* what is being installed */
    cat_title title;
    char mount[1024], pref[1024], id[CAT_ID];
} J;

static void say(const char *status, float progress) {
    SDL_LockMutex(J.mx);
    snprintf(J.status, sizeof J.status, "%s", status);
    J.progress = progress;
    SDL_UnlockMutex(J.mx);
}

static void on_progress(void *ud, double got, double total) {
    (void)ud;
    char s[80];
    if (total > 0) {
        snprintf(s, sizeof s, "DOWNLOADING  %.0f%%", 100.0 * got / total);
        say(s, (float)(got / total) * 0.85f);
    } else {
        snprintf(s, sizeof s, "DOWNLOADING  %.0f KB", got / 1024.0);
        say(s, 0.1f);
    }
}

static int fail(const char *why) {
    SDL_LockMutex(J.mx);
    snprintf(J.err, sizeof J.err, "%s", why);
    J.done = -1;
    J.busy = 0;
    SDL_UnlockMutex(J.mx);
    dxm_log("catalog: install %s: %s", J.id, why);
    return 0;
}

static int SDLCALL worker(void *ud) {
    (void)ud;
    const cat_title *t = &J.title;
    char zip[1400], scratch[1400], dest[1400], cat_dir[1400], err[160];
    snprintf(zip, sizeof zip, "%sdownloads/%s.zip", J.pref, t->id);
    snprintf(scratch, sizeof scratch, "%sdownloads/%s.tmp", J.pref, t->id);
    snprintf(cat_dir, sizeof cat_dir, "%s%s", J.mount, t->category);
    snprintf(dest, sizeof dest, "%s%s/%s", J.mount, t->category, t->id);
    {
        char dl[1400];
        snprintf(dl, sizeof dl, "%sdownloads", J.pref);
        SDL_CreateDirectory(dl);
    }
    if (is_dir(dest))
        return fail("that directory already exists on the drive");
    if (t->download.size > DOWNLOAD_MAX)
        return fail("the archive is larger than any DOS program");

    say("CONNECTING", 0.0f);
    if (net_get_file(t->download.url, zip, on_progress, NULL, &J.cancel, err, sizeof err) != 0) {
        remove(zip);
        return fail(J.cancel ? "cancelled" : err);
    }
    say("CHECKING", 0.87f);
    {
        FILE *f = fopen(zip, "rb");
        long n = -1;
        if (f) {
            fseek(f, 0, SEEK_END);
            n = ftell(f);
            fclose(f);
        }
        if (n != t->download.size) {
            remove(zip);
            return fail("the download is not the size the catalogue says");
        }
    }
    if (!sha256_matches(zip, t->download.sha256)) {
        remove(zip);
        return fail("the download is not what the catalogue says it is");
    }
    say("UNPACKING", 0.9f);
    rm_tree(scratch);
    SDL_CreateDirectory(scratch);
    {
        char into[1400];
        int n = 0;
        snprintf(into, sizeof into, "%s/", scratch);
        if (unzip_extract(zip, into, &n, err, sizeof err) != 0) {
            rm_tree(scratch);
            remove(zip);
            return fail(err);
        }
        dxm_log("catalog: %s: %d files unpacked", t->id, n);
    }
    /* an installer: the title is an archive inside what was unpacked */
    char inner[1400] = "";
    if (t->archive[0]) {
        char path[1400] = "", into[1400];
        int n = 0;
        snprintf(inner, sizeof inner, "%s.in", scratch);
        rm_tree(inner);
        SDL_CreateDirectory(inner);
        snprintf(into, sizeof into, "%s/", inner);
        if (!path_ci(scratch, t->archive, path, sizeof path) ||
            unzip_extract(path, into, &n, err, sizeof err) != 0) {
            rm_tree(inner);
            rm_tree(scratch);
            remove(zip);
            char why[200];
            snprintf(why, sizeof why, "%s will not open: %s", t->archive,
                     path[0] ? err : "it is not in the download");
            return fail(why);
        }
        dxm_log("catalog: %s: %d files unpacked from %s", t->id, n, t->archive);
    }
    say("INSTALLING", 0.96f);
    char found[1400];
    {
        char root[1400];
        snprintf(root, sizeof root, "%s/", inner[0] ? inner : scratch);
        if (!find_run(root, t->run, found, sizeof found, 0)) {
            if (inner[0])
                rm_tree(inner);
            rm_tree(scratch);
            remove(zip);
            char why[200];
            snprintf(why, sizeof why, "%s is not in the archive", t->run);
            return fail(why);
        }
        size_t fl = strlen(found);
        if (fl > 1 && found[fl - 1] == '/')
            found[fl - 1] = 0;
    }
    SDL_CreateDirectory(cat_dir);
    if (!SDL_RenamePath(found, dest)) {
        if (inner[0])
            rm_tree(inner);
        rm_tree(scratch);
        remove(zip);
        return fail("cannot put the title on the drive");
    }
    if (inner[0])
        rm_tree(inner);
    rm_tree(scratch);
    remove(zip);
    dxm_log("catalog: %s installed at %s", t->id, dest);
    SDL_LockMutex(J.mx);
    J.done = 1;
    J.busy = 0;
    J.progress = 1.0f;
    SDL_UnlockMutex(J.mx);
    return 0;
}

int install_begin(const shelf *s, const cat_title *t, const char *pref_dir) {
    if (!J.mx)
        J.mx = SDL_CreateMutex();
    SDL_LockMutex(J.mx);
    if (J.busy) {
        SDL_UnlockMutex(J.mx);
        return 0;
    }
    J.busy = 1;
    J.done = 0;
    J.cancel = 0;
    J.progress = 0.0f;
    J.err[0] = 0;
    J.title = *t;
    snprintf(J.mount, sizeof J.mount, "%s", s->mount);
    snprintf(J.pref, sizeof J.pref, "%s", pref_dir ? pref_dir : "./");
    snprintf(J.id, sizeof J.id, "%s", t->id);
    snprintf(J.status, sizeof J.status, "STARTING");
    SDL_UnlockMutex(J.mx);
    if (J.th)
        SDL_WaitThread(J.th, NULL); /* the last one, long finished */
    J.th = SDL_CreateThread(worker, "install", NULL);
    if (!J.th) {
        fail("cannot start the installer");
        return 0;
    }
    dxm_log("catalog: installing %s from %s", t->id, t->download.url);
    return 1;
}

int install_busy(void) {
    if (!J.mx)
        return 0;
    SDL_LockMutex(J.mx);
    int b = J.busy;
    SDL_UnlockMutex(J.mx);
    return b;
}

float install_progress(char *status, size_t n) {
    if (!J.mx)
        return -1.0f;
    SDL_LockMutex(J.mx);
    float p = J.busy ? J.progress : -1.0f;
    if (status)
        snprintf(status, n, "%s", J.status);
    SDL_UnlockMutex(J.mx);
    return p;
}

int install_take_done(char *err, size_t n) {
    if (!J.mx)
        return 0;
    SDL_LockMutex(J.mx);
    int d = J.done;
    J.done = 0;
    if (err)
        snprintf(err, n, "%s", J.err);
    SDL_UnlockMutex(J.mx);
    return d;
}

void install_cancel(void) {
    J.cancel = 1;
}
