/* library.c — see library.h.
 *
 * Directory work goes through SDL's filesystem API rather than opendir or
 * FindFirstFile.  DXM already links SDL3, and one implementation that is
 * the same everywhere is worth more here than the handful of bytes a
 * hand-rolled pair of #ifdef branches would save. */
#include "library.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static char       root[LIB_PATH];
static lib_game   games[LIB_MAX];
static int        n_games;
/* Modules stay open once opened - see the header.  Parallel to `games` by
 * index, and reset with it. */
static dxm_module mods[LIB_MAX];

const char *lib_root(void) {
    if (!root[0]) {
        const char *p = SDL_GetPrefPath("DOSexMachina", "dxm");
        snprintf(root, sizeof root, "%s", p ? p : "./");
    }
    return root;
}

static int exists(const char *path) {
    SDL_PathInfo st;
    return SDL_GetPathInfo(path, &st);
}

/* DOS data files are uppercase on disk and the probe names them in lower;
 * skyroads' own opener tries both, so this has to as well. */
static int probe_found(const char *dir, const char *probe) {
    char p[LIB_PATH];
    snprintf(p, sizeof p, "%s%s", dir, probe);
    if (exists(p)) return 1;
    size_t base = strlen(dir);
    snprintf(p, sizeof p, "%s%s", dir, probe);
    for (size_t i = base; p[i]; i++) p[i] = (char)toupper((unsigned char)p[i]);
    return exists(p);
}

/* One installed game, from its directory name. */
static void add_game(const char *id) {
    if (n_games >= LIB_MAX) return;
    lib_game *g = &games[n_games];
    memset(g, 0, sizeof *g);
    snprintf(g->id, sizeof g->id, "%s", id);
    snprintf(g->dir, sizeof g->dir, "%sgames%c%s%c",
             lib_root(), DXM_SEP, id, DXM_SEP);
    snprintf(g->module, sizeof g->module, "%sgame.dxm", g->dir);
    snprintf(g->data,   sizeof g->data,   "%sdata%c",   g->dir, DXM_SEP);

    /* A directory with no module in it is not a game - most likely a
     * download that was interrupted.  Say so rather than hiding it. */
    if (!exists(g->module)) {
        snprintf(g->title, sizeof g->title, "%s", id);
        snprintf(g->note, sizeof g->note, "no game.dxm - install unfinished");
        n_games++;
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
        n_games++;
        return;
    }
    snprintf(g->title, sizeof g->title, "%s", m.info->title);
    snprintf(g->by,    sizeof g->by,    "%s", m.info->publisher);
    g->year = m.info->year;
    if (m.info->data_probe && !probe_found(g->data, m.info->data_probe))
        snprintf(g->note, sizeof g->note, "data missing (%s)",
                 m.info->data_probe);
    else
        g->ready = 1;
    coreload_close(&m);
    n_games++;
}

static SDL_EnumerationResult SDLCALL on_entry(void *ud, const char *dirname,
                                             const char *fname) {
    (void)ud;
    char full[LIB_PATH];
    snprintf(full, sizeof full, "%s%c%s", dirname, DXM_SEP, fname);
    SDL_PathInfo st;
    if (SDL_GetPathInfo(full, &st) && st.type == SDL_PATHTYPE_DIRECTORY)
        add_game(fname);
    return SDL_ENUM_CONTINUE;
}

void lib_scan(void) {
    for (int i = 0; i < n_games; i++) coreload_close(&mods[i]);
    memset(mods, 0, sizeof mods);
    n_games = 0;
    char dir[LIB_PATH];
    snprintf(dir, sizeof dir, "%sgames", lib_root());
    if (!exists(dir)) { SDL_CreateDirectory(dir); return; }
    SDL_EnumerateDirectory(dir, on_entry, NULL);
}

int             lib_count(void)      { return n_games; }
const lib_game *lib_at(int i)        { return (i>=0 && i<n_games) ? &games[i] : NULL; }

const lib_game *lib_find(const char *id) {
    for (int i = 0; i < n_games; i++) {
        const char *a = games[i].id, *b = id;
        while (*a && *b && toupper((unsigned char)*a) == toupper((unsigned char)*b)) { a++; b++; }
        if (!*a && !*b) return &games[i];
    }
    return NULL;
}

const dxm_module *lib_module(const lib_game *g) {
    int i = (int)(g - games);
    if (i < 0 || i >= n_games) return NULL;
    if (mods[i].handle) return &mods[i];
    char err[128];
    if (coreload_open(&mods[i], g->module, err, sizeof err) != 0) {
        SDL_Log("[dxm] %s: %s", g->module, err);
        return NULL;
    }
    return &mods[i];
}

int lib_make_dir(const char *id, char *out, size_t outsz) {
    char games_dir[LIB_PATH];
    snprintf(games_dir, sizeof games_dir, "%sgames", lib_root());
    if (!exists(games_dir) && !SDL_CreateDirectory(games_dir)) return -1;
    snprintf(out, outsz, "%sgames%c%s", lib_root(), DXM_SEP, id);
    if (!exists(out) && !SDL_CreateDirectory(out)) return -1;
    return 0;
}
