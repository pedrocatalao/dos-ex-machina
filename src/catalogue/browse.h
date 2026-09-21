/* browse.h — the machine's own file browser, and the listing it stands on.
 *
 * Asking the host to put up its file dialog would work and would be less
 * code, but it would also be the one moment in the whole program that
 * admits there is a host: a modern sheet sliding down over a CRT.  So this
 * is a panel like the others, laid out the way the file managers of the
 * period laid one out - `..` first, folders before files, <DIR> where a
 * size goes, sizes in bytes and right aligned, and a letter typed jumping
 * to the next name that starts with it.
 *
 * It walks two different things and shows them the same way: this
 * computer's own folders, to find an archive, and the emulated drives, to
 * find where something is or where it should go.  It knows nothing about
 * catalogues or titles; when something is chosen it says so and the caller
 * decides what that means. */
#ifndef DXM_BROWSE_H
#define DXM_BROWSE_H
#include <stddef.h>

/* What it was opened to find.  The caller gets it back with the answer, so
 * one callback can serve two questions of the same shape. */
typedef enum {
    BROWSE_ARCHIVE, /* a file on this computer */
    BROWSE_PICTURE, /* a picture on this computer */
    BROWSE_FOLDER,  /* a folder on a drive, and what is in it */
    BROWSE_DEST,    /* a folder on a drive to put something in */
    BROWSE_RUN,     /* which program a title starts */
    BROWSE_SETUP    /* and which one sets it up */
} browse_for;

/* Open it.  `start` is where to begin: a folder on this computer, a DOS
 * path, or the folder whose programs are to be listed.  An empty or
 * unreachable start falls back to somewhere sensible. */
void browse_archive(const char *start, browse_for what);
void browse_drive(const char *start, browse_for what);
void browse_programs(const char *dos_dir, browse_for what);
void browse_close(void);

int browse_open(void); /* one is up, and has the keys */
void browse_key(int sdl_scancode, int shift);
/* A click, given what the pointer landed on from browse_hit(). */
void browse_click(int what);
int browse_hit(int x, int y); /* what is under the pointer, or -1 */
void browse_draw(void);       /* which also lays out what can be hit */

/* ---- what it hands back, which the caller implements ------------------- */

/* A file on this computer, as a host path. */
void browse_took_archive(const char *host_path, browse_for what);
/* A folder on a drive, as a DOS path without its trailing separator.  For
 * BROWSE_FOLDER `program` is the one that was chosen inside it, or NULL if
 * the folder itself was taken; for BROWSE_DEST it is always NULL. */
void browse_took_folder(const char *dos_dir, const char *program, browse_for what);
/* One of the programs under a folder: `dos_dir` is the folder it actually
 * sits in, which is not always the one the listing started from. */
void browse_took_program(const char *dos_dir, const char *name, browse_for what);
/* ESC, or a click on Cancel. */
void browse_gave_up(browse_for what);

/* ---- the programs under a folder --------------------------------------- */

/* Collected once and used twice: the browser shows this list when somebody
 * would rather pick than accept a guess, and whatever is doing the guessing
 * reads the same thing. */
#define PROGRAMS_MAX 128

typedef struct {
    char rel[176]; /* under the folder, with DOS separators, upper case */
    long size;
} program;

int browse_programs_under(const char *dos_dir, program *out, int max);

/* The directory part of such a relative path, and the name at the end. */
void browse_split(const char *rel, char *dir, size_t dn, char *name, size_t nn);

#endif
