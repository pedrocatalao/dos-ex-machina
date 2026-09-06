/* library.c — see library.h.
 *
 * Directory work goes through SDL's filesystem API rather than opendir or
 * FindFirstFile.  DXM already links SDL3, and one implementation that is
 * the same everywhere is worth more here than the handful of bytes a
 * hand-rolled pair of #ifdef branches would save. */
#include "library.h"
#include "unzip.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* The library: the preferences root, the games found, their modules */
static struct {
    char root[LIB_PATH];
    lib_game games[LIB_MAX];
    int n_games;
    /* Modules stay open once opened - see the header.  Parallel to `games` by *index, and reset with it. */
    dxm_module mods[LIB_MAX];
} lib;

const char *lib_root(void) {
    if (!lib.root[0]) {
        const char *p = SDL_GetPrefPath("DOSexMachina", "dxm");
        snprintf(lib.root, sizeof lib.root, "%s", p ? p : "./");
    }
    return lib.root;
}

/* Join path components with the platform separator.  A component that
 * already ends in a separator is not given another, so the preferences
 * directory (which comes with one) joins cleanly; an empty last component
 * leaves a trailing separator, for the directories the rest of the code
 * appends file names to.  Returns 0, or -1 with dst empty when the result
 * would not fit: a truncated path is a wrong path, not a shorter one. */
static int path_join(char *dst, size_t n, ...) {
    va_list ap; va_start(ap, n);
    size_t len = 0; dst[0] = 0;
    for (const char *part = va_arg(ap, const char *); part; part = va_arg(ap, const char *)) {
        size_t pl = strlen(part);
        int sep = len > 0 && dst[len-1] != DXM_SEP;
        if (len + sep + pl + 1 > n) { va_end(ap); dst[0] = 0; return -1; }
        if (sep) dst[len++] = DXM_SEP;
        memcpy(dst + len, part, pl); len += pl; dst[len] = 0;
    }
    va_end(ap);
    return 0;
}

static int exists(const char *path) {
    SDL_PathInfo st;
    return SDL_GetPathInfo(path, &st);
}

/* rm -rf, through SDL: SDL_RemovePath takes a file or an EMPTY directory,
 * so the walk has to empty each one from the bottom up.  Counts what it
 * could not remove rather than stopping - a locked file should not leave
 * the rest of the tree behind. */
static int rm_rf(const char *path);
static SDL_EnumerationResult SDLCALL rm_entry(void *ud, const char *dirname,
                                              const char *fname) {
    int *fails = (int *)ud;
    char full[LIB_PATH];
    if (path_join(full, sizeof full, dirname, fname, NULL) != 0) { (*fails)++; return SDL_ENUM_CONTINUE; }
    SDL_PathInfo st;
    if (SDL_GetPathInfo(full, &st) && st.type == SDL_PATHTYPE_DIRECTORY)
        *fails += rm_rf(full);
    else if (!SDL_RemovePath(full))
        (*fails)++;
    return SDL_ENUM_CONTINUE;
}
static int rm_rf(const char *path) {
    int fails = 0;
    if (!exists(path)) return 0;
    SDL_EnumerateDirectory(path, rm_entry, &fails);
    if (!SDL_RemovePath(path)) fails++;
    return fails;
}

/* DOS data files are uppercase on disk and the probe names them in lower;
 * skyroads' own opener tries both, so this has to as well. */
static int probe_found(const char *dir, const char *probe) {
    char p[LIB_PATH];
    if (path_join(p, sizeof p, dir, probe, NULL) != 0) return 0;
    if (exists(p)) return 1;
    size_t base = strlen(dir);
    for (size_t i = base; p[i]; i++) p[i] = (char)toupper((unsigned char)p[i]);
    return exists(p);
}

/* One installed game, from its directory name. */
static void add_game(const char *id) {
    if (lib.n_games >= LIB_MAX) return;
    lib_game *g = &lib.games[lib.n_games];
    memset(g, 0, sizeof *g);
    snprintf(g->id, sizeof g->id, "%s", id);
    if (path_join(g->dir,    sizeof g->dir,    lib_root(), "games", id, "", NULL) != 0 ||
        path_join(g->module, sizeof g->module, lib_root(), "games", id, "game.dxm", NULL) != 0 ||
        path_join(g->data,   sizeof g->data,   lib_root(), "games", id, "data", "", NULL) != 0) {
        /* a preferences directory deep enough for this is a directory DXM
         * cannot work in; say so rather than work in the wrong one */
        snprintf(g->title, sizeof g->title, "%s", id);
        snprintf(g->note, sizeof g->note, "path too long");
        lib.n_games++;
        return;
    }

    /* A directory with no module in it is not a game - most likely a
     * download that was interrupted.  Say so rather than hiding it. */
    if (!exists(g->module)) {
        snprintf(g->title, sizeof g->title, "%s", id);
        snprintf(g->note, sizeof g->note, "no game.dxm - install unfinished");
        lib.n_games++;
        return;
    }
    /* Ask the module what it is.  This also settles whether it loads at all
     * on this machine and whether its ABI is one we speak, which is exactly
     * what the user needs told if the answer is no. */
    dxm_module m;
    char err[128];
    if (coreload_open(&m, g->module, err, sizeof err) != 0) {
        snprintf(g->title, sizeof g->title, "%s", id);
        snprintf(g->note, sizeof g->note, "%s", err);
        lib.n_games++;
        return;
    }
    snprintf(g->title, sizeof g->title, "%s", m.info->title);
    snprintf(g->by,    sizeof g->by,    "%s", m.info->publisher);
    g->year = m.info->year;
    /* the dates the panel shows: the module's own timestamp, and the
     * marker lib_touch_played() leaves */
    { SDL_PathInfo st; char pl[LIB_PATH];
      if (path_join(pl, sizeof pl, g->dir, "played", NULL) == 0 && SDL_GetPathInfo(pl, &st))
          g->played_ns = st.modify_time;
      /* the release the installer wrote down; absent on older installs */
      FILE *f = path_join(pl, sizeof pl, g->dir, "version", NULL) == 0 ? fopen(pl, "rb") : NULL;
      if (f) {
          if (fgets(g->version, sizeof g->version, f)) {
              char *nl = strpbrk(g->version, "\r\n"); if (nl) *nl = 0;
          }
          fclose(f);
      } }
    if (m.info->data_probe && !probe_found(g->data, m.info->data_probe))
        snprintf(g->note, sizeof g->note, "data missing (%s)",
                 m.info->data_probe);
    else
        g->ready = 1;
    coreload_close(&m);
    lib.n_games++;
}

static SDL_EnumerationResult SDLCALL on_entry(void *ud, const char *dirname,
                                             const char *fname) {
    (void)ud;
    char full[LIB_PATH];
    if (path_join(full, sizeof full, dirname, fname, NULL) != 0) return SDL_ENUM_CONTINUE;
    SDL_PathInfo st;
    if (SDL_GetPathInfo(full, &st) && st.type == SDL_PATHTYPE_DIRECTORY)
        add_game(fname);
    return SDL_ENUM_CONTINUE;
}

void lib_scan(void) {
    for (int i = 0; i < lib.n_games; i++) coreload_close(&lib.mods[i]);
    memset(lib.mods, 0, sizeof lib.mods);
    lib.n_games = 0;
    char dir[LIB_PATH];
    if (path_join(dir, sizeof dir, lib_root(), "games", NULL) != 0) return;
    if (!exists(dir)) { SDL_CreateDirectory(dir); return; }
    SDL_EnumerateDirectory(dir, on_entry, NULL);
}

int             lib_count(void)      { return lib.n_games; }
const lib_game *lib_at(int i)        { return (i>=0 && i<lib.n_games) ? &lib.games[i] : NULL; }

const lib_game *lib_find(const char *id) {
    for (int i = 0; i < lib.n_games; i++) {
        const char *a = lib.games[i].id, *b = id;
        while (*a && *b && toupper((unsigned char)*a) == toupper((unsigned char)*b)) { a++; b++; }
        if (!*a && !*b) return &lib.games[i];
    }
    return NULL;
}

const dxm_module *lib_module(const lib_game *g) {
    int i = (int)(g - lib.games);
    if (i < 0 || i >= lib.n_games) return NULL;
    if (lib.mods[i].handle) return &lib.mods[i];
    char err[128];
    if (coreload_open(&lib.mods[i], g->module, err, sizeof err) != 0) {
        SDL_Log("[dxm] %s: %s", g->module, err);
        return NULL;
    }
    return &lib.mods[i];
}

void lib_unload(const lib_game *g) {
    int i = (int)(g - lib.games);
    if (i < 0 || i >= lib.n_games || !lib.mods[i].handle) return;
    coreload_close(&lib.mods[i]);
    memset(&lib.mods[i], 0, sizeof lib.mods[i]);
}

void lib_touch_played(const lib_game *g) {
    char pl[LIB_PATH];
    if (path_join(pl, sizeof pl, g->dir, "played", NULL) != 0) return;
    /* rewriting it is what moves the timestamp; the content is a courtesy */
    FILE *f = fopen(pl, "wb");
    if (!f) return;
    SDL_Time now = 0; SDL_GetCurrentTime(&now);
    fprintf(f, "%lld\n", (long long)now);
    fclose(f);
}

int lib_remove(const char *id) {
    /* the module may be open - on Windows an open DLL cannot be deleted */
    for (int i = 0; i < lib.n_games; i++)
        if (lib_find(id) == &lib.games[i]) { coreload_close(&lib.mods[i]); memset(&lib.mods[i], 0, sizeof lib.mods[i]); }
    char dir[LIB_PATH];
    if (path_join(dir, sizeof dir, lib_root(), "games", id, NULL) != 0) return 1;
    return rm_rf(dir);
}

int lib_reset(const char *id, char *err, size_t errsz) {
    char dir[LIB_PATH], zip[LIB_PATH], data[LIB_PATH], dest[LIB_PATH];
    if (path_join(dir,  sizeof dir,  lib_root(), "games", id, NULL) != 0 ||
        path_join(zip,  sizeof zip,  dir, "data.zip", NULL) != 0 ||
        path_join(data, sizeof data, dir, "data", NULL) != 0 ||
        path_join(dest, sizeof dest, dir, "data", "", NULL) != 0) {
        snprintf(err, errsz, "path too long");
        return -1;
    }
    if (!exists(zip)) {
        /* installed before the archive was kept: nothing here to restore
         * from - the caller can fetch the data again instead */
        snprintf(err, errsz, "no archive kept");
        return 1;
    }
    if (rm_rf(data) != 0) {
        snprintf(err, errsz, "could not clear the data directory");
        return -1;
    }
    SDL_CreateDirectory(data);
    int n = 0;
    return unzip_extract(zip, dest, &n, err, errsz) == 0 ? 0 : -1;
}

int lib_make_dir(const char *id, char *out, size_t outsz) {
    char games_dir[LIB_PATH];
    if (path_join(games_dir, sizeof games_dir, lib_root(), "games", NULL) != 0) return -1;
    if (!exists(games_dir) && !SDL_CreateDirectory(games_dir)) return -1;
    if (path_join(out, outsz, lib_root(), "games", id, NULL) != 0) return -1;
    if (!exists(out) && !SDL_CreateDirectory(out)) return -1;
    return 0;
}
