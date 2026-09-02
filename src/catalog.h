/* catalog.h — the list of games DXM knows how to install.
 *
 * Fetched at run time from the project repo and cached on disk, so adding a
 * game is an edit to catalogue.json rather than a new DXM release.  The
 * cache is what makes the navigator work offline: a machine that has never
 * been online shows nothing extra, and one that has shows what it saw last. */
#ifndef DXM_CATALOG_H
#define DXM_CATALOG_H
#include <stddef.h>

#define CAT_MAX      32
#define CAT_URL_MAX  400
#define CAT_DESC     4

typedef struct {
    char url[CAT_URL_MAX];
    char sha256[65];
    long size;
} cat_file;

typedef struct {
    char id[32], title[64], by[80];
    int  year, abi;
    char desc[CAT_DESC][64];
    cat_file art, module, data;
    char data_kind[16];          /* "freeware" | "byo"        */
    char data_format[8];         /* "zip"                     */
    char data_probe[32];         /* the file that proves data */
    int  have_module;            /* a build exists for THIS platform */
} cat_game;

/* "macos-universal", "linux-x86_64", "windows-x86_64", ... - the key the
 * catalogue's `module` map is indexed by. */
const char *cat_platform(void);

/* Load the cached copy if there is one.  Never touches the network. */
int  cat_load_cached(void);
/* Fetch a fresh copy and cache it.  Safe to call from a worker thread. */
int  cat_refresh(char *err, size_t errsz);

/* Non-blocking refresh: begin() starts a fetch, collect() is called from the
 * main loop and returns 1 the frame a fresh catalogue has been parsed in. */
void cat_refresh_begin(void);
int  cat_refresh_collect(void);

int             cat_count(void);
const cat_game *cat_at(int i);
const cat_game *cat_find(const char *id);

#endif
