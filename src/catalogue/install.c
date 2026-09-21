/* install.c — a title from the catalogue onto its drive (SPEC §8.4).
 *
 * The archive comes from one of two places.  A catalogue of downloads names
 * one and says what it should turn out to be, and the machine checks both;
 * an archive already on this computer was not fetched here, cannot be
 * vouched for, and must be left exactly where it was found.
 *
 * Fetch or open the archive, check whatever can be checked, unpack it into
 * a scratch directory - and, for an archive that is an installer, unpack
 * the one the catalogue names inside it - take off whatever the packer
 * wrapped round it, give every folder a name DOS can reach, and move the
 * lot to the directory the caller chose.  Then the scratch and any download
 * of ours go; an archive the user already had is left where it was.
 *
 * Where it ended up is written to where.h, since that is a fact about this
 * machine rather than about the title.
 *
 * All of it on a thread, one at a time, with the screen asking how far. */
#include "internal.h"
#include "where.h"
#include "dosname.h"
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
    lookup l = {name, ""};
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

/* A DOS path to a place on the host: the drive's folder, then the rest of
 * it with the separators turned round.  0 when nothing is mounted on that
 * letter, which is the honest answer for a title on a drive that is not
 * there. */
int install_host_path(const char *dos, char *out, size_t n) {
    char mount[1200];
    if (!dos || !dos[0] || dos[1] != ':' || !cat_drive_mount(dos[0], mount, sizeof mount))
        return 0;
    const char *rest = dos + 2;
    while (*rest == '\\' || *rest == '/')
        rest++;
    char path[1400];
    snprintf(path, sizeof path, "%s%s", mount, rest);
    for (char *p = path; *p; p++)
        if (*p == '\\')
            *p = '/';
    snprintf(out, n, "%s", path);
    return 1;
}

/* Where a title installs when nobody says otherwise:
 * <drive>:\DXM\<catalogue>\<category>\<id>.  Under \DXM so the machine's
 * things stay together and do not litter the root of somebody's own drive,
 * and with the catalogue in the path so two catalogues holding the same
 * game do not want the same directory. */
void install_default_dir(const shelf *s, const cat_title *t, char *dos, size_t dn) {
    snprintf(dos, dn, "%c:\\DXM\\%s\\%s\\%s", cat_default_drive(), s->cat.id, t->category, t->id);
}

void install_dir(const shelf *s, const cat_title *t, char *host, size_t hn, char *dos, size_t dn) {
    const char *at = where_get(s->cat.id, t->category, t->id);
    if (dos)
        snprintf(dos, dn, "%s", at ? at : "");
    if (host) {
        host[0] = 0;
        if (at)
            install_host_path(at, host, hn);
    }
}

int install_present(const shelf *s, const cat_title *t) {
    char dir[1400], at[1410], found[1400];
    install_dir(s, t, dir, sizeof dir, NULL, 0);
    if (!dir[0] || !is_dir(dir))
        return 0;
    snprintf(at, sizeof at, "%s/", dir); /* as every other walk here is given it */
    return in_dir_ci(at, t->run, found, sizeof found);
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

/* ---- what a packer wrapped round it ------------------------------------ */

/* What a zip made on somebody's desktop carries that DOS has no use for.
 * These go before the tree is looked at, because otherwise a plain wrapper
 * folder is not the only thing at the top level and would not be seen as a
 * wrapper at all. */
static int is_junk(const char *name) {
    return name[0] == '.' || !SDL_strcasecmp(name, "__MACOSX") ||
           !SDL_strcasecmp(name, "Thumbs.db");
}

typedef struct {
    char names[32][260];
    int n;
} sweep;

static SDL_EnumerationResult junk_cb(void *ud, const char *dirname, const char *fname) {
    sweep *sw = ud;
    (void)dirname;
    if (is_junk(fname) && sw->n < (int)(sizeof sw->names / sizeof sw->names[0]))
        snprintf(sw->names[sw->n++], sizeof sw->names[0], "%s", fname);
    return SDL_ENUM_CONTINUE;
}

/* Collected first and removed after: taking entries out of a directory
 * while walking it is not something every host stands for. */
static void drop_junk(const char *dir) {
    char at[1400];
    snprintf(at, sizeof at, "%s/", dir);
    sweep sw = {{{0}}, 0};
    SDL_EnumerateDirectory(at, junk_cb, &sw);
    for (int i = 0; i < sw.n; i++) {
        char path[1500];
        snprintf(path, sizeof path, "%s%s", at, sw.names[i]);
        if (is_dir(path))
            rm_tree(path);
        else
            SDL_RemovePath(path);
    }
}

typedef struct {
    char only[260];   /* the one directory */
    int dirs, theirs; /* and how many files beside it DOS could reach */
    char loose[32][260];
    int n_loose;
} toplevel;

static SDL_EnumerationResult top_cb(void *ud, const char *dirname, const char *fname) {
    toplevel *tl = ud;
    char path[1400];
    snprintf(path, sizeof path, "%s%s", dirname, fname);
    if (is_dir(path)) {
        tl->dirs++;
        snprintf(tl->only, sizeof tl->only, "%s", fname);
        return SDL_ENUM_CONTINUE;
    }
    /* A file DOS can reach is one the title could be opening; one it cannot
     * is something that came with the release and not with the game - the
     * notes a scene packer drops beside the folder. */
    if (dos_reachable(fname))
        tl->theirs++;
    else if (tl->n_loose < (int)(sizeof tl->loose / sizeof tl->loose[0]))
        snprintf(tl->loose[tl->n_loose++], sizeof tl->loose[0], "%s", fname);
    return SDL_ENUM_CONTINUE;
}

/* One directory at the top is a wrapper the packer added rather than part
 * of the title, so long as nothing beside it could belong to the title
 * either: step into it, and again, since some archives are wrapped twice.
 *
 * "Nothing beside it" is not the same as "nothing at all".  A release very
 * often carries its notes next to the folder - PREHISTORIK 2
 * [REPLAYERS.ORG] with a replayers.nfo beside it - and those are names DOS
 * cannot reach, which is exactly what marks them as not being the game.  A
 * file DOS *can* reach might be something the title opens, so its presence
 * stops the dig.
 *
 * What is left behind comes along rather than being dropped: it is
 * somebody's readme, and it costs nothing to keep.
 *
 * Anything else is left exactly as it came.  An archive with a tree of its
 * own - the program in one folder, its data in another beside it - means
 * that tree, and a program that opens ..\DATA will not find it once the
 * tree has been flattened. */
static void strip_wrapper(char *dir, size_t n) {
    for (int k = 0; k < 4; k++) {
        char at[1400];
        snprintf(at, sizeof at, "%s/", dir);
        toplevel tl = {"", 0, 0, {{0}}, 0};
        SDL_EnumerateDirectory(at, top_cb, &tl);
        if (tl.dirs != 1 || tl.theirs)
            return;
        char next[1400];
        snprintf(next, sizeof next, "%s%s", at, tl.only);
        for (int i = 0; i < tl.n_loose; i++) { /* the notes come too */
            char from[1500], to[1600];
            snprintf(from, sizeof from, "%s%s", at, tl.loose[i]);
            snprintf(to, sizeof to, "%s/%s", next, tl.loose[i]);
            SDL_RenamePath(from, to);
        }
        snprintf(dir, n, "%s", next);
    }
}

/* ---- names DOS can actually reach -------------------------------------- */

/* Give every folder under `dir` a name DOS can reach, deepest last so a
 * walk is never pulled out from under itself.  Only folders: a data file
 * is opened by the program under the name DOS cut for it, which is the
 * name it already has. */
typedef struct {
    char names[64][260];
    int n;
} kids;

static SDL_EnumerationResult kids_cb(void *ud, const char *dirname, const char *fname) {
    kids *k = ud;
    char path[1400];
    snprintf(path, sizeof path, "%s%s", dirname, fname);
    if (is_dir(path) && k->n < (int)(sizeof k->names / sizeof k->names[0]))
        snprintf(k->names[k->n++], sizeof k->names[0], "%s", fname);
    return SDL_ENUM_CONTINUE;
}

static void dosify_tree(const char *dir, int depth) {
    if (depth > 6)
        return;
    char at[1400];
    snprintf(at, sizeof at, "%s/", dir);
    kids k = {{{0}}, 0};
    SDL_EnumerateDirectory(at, kids_cb, &k);
    for (int i = 0; i < k.n; i++) {
        char from[1500], to[1500], want[DOS_NAME];
        snprintf(from, sizeof from, "%s%s", at, k.names[i]);
        dos_name(k.names[i], want, sizeof want);
        if (!dos_reachable(k.names[i])) {
            snprintf(to, sizeof to, "%s%s", at, want);
            /* A name already taken gets a digit, in the stem so that an
             * extension is not what gets lost. */
            for (int try = 1; try < 10 && is_dir(to); try++) {
                char stem[DOS_NAME], uniq[DOS_NAME + 2];
                snprintf(stem, sizeof stem, "%s", want);
                char *dot = strchr(stem, '.');
                if (dot)
                    *dot = 0;
                snprintf(uniq, sizeof uniq, "%.7s%d%s%s", stem, try, dot ? "." : "",
                         dot ? dot + 1 : "");
                snprintf(to, sizeof to, "%s%s", at, uniq);
            }
            if (SDL_RenamePath(from, to)) {
                dxm_log("catalog: %s is not a name DOS can reach; renamed to %s", k.names[i],
                        strrchr(to, '/') + 1);
                snprintf(from, sizeof from, "%s", to);
            }
        }
        dosify_tree(from, depth + 1);
    }
}

/* ---- the job ----------------------------------------------------------- */

static struct {
    SDL_Mutex *mx;
    SDL_Thread *th;
    int busy, done; /* done: 1 well, -1 badly, taken once */
    volatile int cancel;
    float progress;
    char status[80], err[160];
    /* what is being installed, and where it was told to put it */
    cat_title title;
    char shelf_id[CAT_ID];
    char dest_dos[WHERE_PATH]; /* where it goes, as DOS sees it */
    char dest_host[1400];      /* and the same place on this computer */
    char archive[CAT_PATH];    /* an archive already here, or empty to fetch one */
    char pref[1024], id[CAT_ID];
    /* what the run file turned out to be under, for the caller to refine
     * when the title did not say what to run */
    char landed[WHERE_PATH];
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

/* How big the file at `path` is, or -1. */
static long file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    long n = -1;
    if (f) {
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fclose(f);
    }
    return n;
}

static int SDLCALL worker(void *ud) {
    (void)ud;
    const cat_title *t = &J.title;
    char zip[1400], scratch[1400], err[160];
    /* Whose archive it is.  One this machine fetched is its own and goes
     * when the job is over; one the user already had stays exactly where
     * they left it, whether the install worked or not. */
    int ours = !J.archive[0];
    if (ours)
        snprintf(zip, sizeof zip, "%sdownloads/%s.zip", J.pref, t->id);
    else
        snprintf(zip, sizeof zip, "%s", J.archive);
    snprintf(scratch, sizeof scratch, "%sdownloads/%s.tmp", J.pref, t->id);
    {
        char dl[1400];
        snprintf(dl, sizeof dl, "%sdownloads", J.pref);
        SDL_CreateDirectory(dl);
    }
    if (!J.dest_host[0])
        return fail("that drive is not mounted");
    if (is_dir(J.dest_host))
        return fail("that directory already exists on the drive");
    if (t->download.size > DOWNLOAD_MAX)
        return fail("the archive is larger than any DOS program");

    if (ours) {
        say("CONNECTING", 0.0f);
        if (net_get_file(t->download.url, zip, on_progress, NULL, &J.cancel, err, sizeof err) !=
            0) {
            remove(zip);
            return fail(J.cancel ? "cancelled" : err);
        }
        say("CHECKING", 0.87f);
        if (file_size(zip) != t->download.size) {
            remove(zip);
            return fail("the download is not the size the catalogue says");
        }
        if (!sha256_matches(zip, t->download.sha256)) {
            remove(zip);
            return fail("the download is not what the catalogue says it is");
        }
    } else {
        /* An archive the user fetched themselves.  The machine did not see
         * where it came from and will not pretend to vouch for it; it only
         * says whether it is there. */
        say("OPENING", 0.5f);
        long n = file_size(zip);
        if (n < 0)
            return fail("that file is not there any more");
        if (n > DOWNLOAD_MAX)
            return fail("the archive is larger than any DOS program");
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
            if (ours)
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
            if (ours)
                remove(zip);
            char why[200];
            snprintf(why, sizeof why, "%s will not open: %s", t->archive,
                     path[0] ? err : "it is not in the download");
            return fail(why);
        }
        dxm_log("catalog: %s: %d files unpacked from %s", t->id, n, t->archive);
    }
    say("INSTALLING", 0.96f);
    /* What the packer wrapped round it comes off; what it brought with it
     * stays.  Whatever is left is the title, and all of it goes over. */
    char root[1400];
    snprintf(root, sizeof root, "%s", inner[0] ? inner : scratch);
    drop_junk(root);
    strip_wrapper(root, sizeof root);
    dosify_tree(root, 0); /* so what DOS is told matches what is on the disk */
    {
        /* the folders above it, since the destination may be several deep */
        char up[1400];
        snprintf(up, sizeof up, "%s", J.dest_host);
        for (char *p = up + 1; *p; p++)
            if (*p == '/') {
                *p = 0;
                SDL_CreateDirectory(up);
                *p = '/';
            }
    }
    if (!SDL_RenamePath(root, J.dest_host)) {
        if (inner[0])
            rm_tree(inner);
        rm_tree(scratch);
        if (ours)
            remove(zip);
        return fail("cannot put the title on the drive");
    }
    if (inner[0])
        rm_tree(inner);
    rm_tree(scratch);
    if (ours)
        remove(zip);

    /* Where it ended up.  A title that says what it runs can be looked for,
     * since its own folder may be one of several the archive brought; one
     * that does not is left at the destination for whoever picks. */
    snprintf(J.landed, sizeof J.landed, "%s", J.dest_dos);
    if (t->run[0]) {
        char at[1400], from[1400];
        snprintf(from, sizeof from, "%s/", J.dest_host);
        if (find_run(from, t->run, at, sizeof at, 0)) {
            size_t fl = strlen(at);
            if (fl > 1 && at[fl - 1] == '/')
                at[--fl] = 0;
            size_t base = strlen(J.dest_host);
            if (fl > base) { /* it sits in a folder of the archive's own */
                char tailpath[WHERE_PATH];
                snprintf(tailpath, sizeof tailpath, "%s", at + base);
                for (char *p = tailpath; *p; p++)
                    if (*p == '/')
                        *p = '\\';
                /* Silently cutting this would record a directory that is
                 * not there, after saying the install worked. */
                if (strlen(J.dest_dos) + strlen(tailpath) >= sizeof J.landed) {
                    rm_tree(J.dest_host);
                    return fail("the path inside the archive is longer than DOS allows");
                }
                snprintf(J.landed, sizeof J.landed, "%s%s", J.dest_dos, tailpath);
            }
        }
    }
    dxm_log("catalog: %s installed at %s", t->id, J.landed);
    SDL_LockMutex(J.mx);
    J.done = 1;
    J.busy = 0;
    J.progress = 1.0f;
    SDL_UnlockMutex(J.mx);
    return 0;
}

int install_begin(const shelf *s, const cat_title *t, const char *pref_dir, const char *dos_dest,
                  const char *host_archive) {
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
    J.landed[0] = 0;
    snprintf(J.shelf_id, sizeof J.shelf_id, "%s", s->cat.id);
    snprintf(J.dest_dos, sizeof J.dest_dos, "%s", dos_dest ? dos_dest : "");
    J.dest_host[0] = 0;
    install_host_path(J.dest_dos, J.dest_host, sizeof J.dest_host);
    snprintf(J.archive, sizeof J.archive, "%s", host_archive ? host_archive : "");
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
    dxm_log("catalog: installing %s from %s to %s", t->id,
            host_archive ? host_archive : t->download.url, dos_dest);
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
    /* Where it landed is written down here rather than by the worker.  The
     * table in where.h has no lock on it and the screen reads it every
     * frame to say what is installed; taking the result is the moment the
     * worker has finished with the job, so this is the one thread that
     * ever touches it. */
    if (d > 0 && J.landed[0])
        where_set(J.shelf_id, J.title.category, J.title.id, J.landed, 1);
    return d;
}

void install_cancel(void) {
    J.cancel = 1;
}
