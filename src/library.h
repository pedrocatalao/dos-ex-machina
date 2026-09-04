/* library.h — the games installed on this machine.
 *
 * DXM links no game.  What it can run is whatever is sitting in its games
 * directory, which makes the shell and the catalogue independent: adding a
 * game is a download, not a rebuild.
 *
 *   <pref>/games/<id>/game.dxm     the core module
 *   <pref>/games/<id>/data/        the game's own data files
 *
 * One directory per game, because that is what makes "is it installed?" a
 * question with an unambiguous answer, and uninstalling a single delete. */
#ifndef DXM_LIBRARY_H
#define DXM_LIBRARY_H
#include "coreload.h"
#include <stdint.h>

#define LIB_MAX  32
#define LIB_PATH 1024

typedef struct {
    char id[32];                 /* directory name, and the DOS command    */
    char title[64], by[80];
    int  year;
    char dir[LIB_PATH];          /* <root>games/<id>/                      */
    char module[LIB_PATH];
    char data[LIB_PATH];
    int  ready;                  /* module loaded AND its data probe found */
    char note[96];               /* if not ready, why - shown to the user  */
    char version[16];            /* the release installed; "" = unknown    */
    int64_t played_ns;           /* when it last ran; 0 = never            */
} lib_game;

/* The preferences directory, with a trailing separator.  Everything DXM
 * owns on disk lives under it. */
const char *lib_root(void);

/* Rebuild the list from disk.  Cheap enough to call whenever something may
 * have changed - after an install, say. */
void            lib_scan(void);
int             lib_count(void);
const lib_game *lib_at(int i);
const lib_game *lib_find(const char *id);          /* case-insensitive */

/* The module for a game, opened on first use and kept open afterwards.
 * Reopening between runs would mean relying on dlclose actually unloading,
 * which is not something the platforms agree on. */
const dxm_module *lib_module(const lib_game *g);

/* <pref>games/<id>, created if missing - where an install writes. */
int lib_make_dir(const char *id, char *out, size_t outsz);

/* Let go of a game's module.  An update overwrites game.dxm, and on Windows
 * a loaded DLL cannot be overwritten; lib_module() reopens it on next use. */
void lib_unload(const lib_game *g);

/* Note that the game has just been started.  Kept as a file beside the
 * module, so it survives a reset and goes with a delete. */
void lib_touch_played(const lib_game *g);

/* Take a game off the machine: its whole directory.  Returns the number of
 * things that could NOT be removed (0 = clean). */
int lib_remove(const char *id);

/* Put a game's data back the way it was installed - saved games, high
 * scores and settings gone, the game itself kept.  Works from the archive
 * the install left beside the data, so it needs no network.  Returns 0, or
 * -1 with a reason in err. */
int lib_reset(const char *id, char *err, size_t errsz);

#endif
