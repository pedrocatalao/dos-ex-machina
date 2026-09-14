/* catalogue.h — the catalogue files, read and checked (SPEC §8.2).
 *
 * Two kinds of file.  `catalogues.lst` names the catalogues; a `.cat` is
 * one of them, a list of titles.  Both are read from disk into fixed
 * tables here, and both are checked as they are read: a title that is not
 * what the format says is dropped with a note, not kept half-right, and
 * not allowed to take the rest of the catalogue with it.
 *
 * Nothing here touches the network or SDL.  Where the files are, and what
 * to do about a title, is the caller's; this only says what is in them. */
#ifndef DXM_CATALOGUE_H
#define DXM_CATALOGUE_H
#include <stddef.h>

/* The fixed vocabulary.  Categories are folder names on a DOS drive, hence
 * plural, upper case, eight characters or fewer. */
#define CAT_ID 9    /* an id or a category: 8 characters, then the NUL */
#define CAT_NAME 65 /* a title's name */
#define CAT_DESC 401
#define CAT_URL 400
#define CAT_LIST 8     /* catalogues the machine holds at once */
#define CAT_TITLES 256 /* titles in one catalogue */
#define CAT_WORDS 6    /* entries in a sound or controls list */
#define CAT_WORD 12    /* one such entry */
#define CAT_NOTES 16   /* dropped titles remembered for the log */

typedef struct {
    char url[CAT_URL];
    char sha256[65];
    long size; /* 0 for the artwork, which does not say */
} cat_file;

typedef struct {
    char id[CAT_ID], category[CAT_ID]; /* \<category>\<id> on the drive */
    char name[CAT_NAME];
    char creator[80], publisher[80];
    int year;
    char version[16], genre[32];
    char description[CAT_DESC];
    int multiplayer, network;
    char video[8]; /* CGA, EGA, VGA, SVGA, or empty */
    char sound[CAT_WORDS][CAT_WORD], controls[CAT_WORDS][CAT_WORD];
    char run[64], setup[64];    /* setup is empty for a title without one */
    char archive[64];           /* an archive inside the download to unpack instead, or empty */
    cat_file download, artwork; /* artwork.url empty for a title without */
} cat_title;

typedef struct {
    int format;
    char id[CAT_ID]; /* the host folder, and the drive's volume label */
    char drive;      /* the letter it is a drive on, 'C'..'Z' */
    int community;   /* origin: bundled (0) or community (1) */
    char name[CAT_NAME], about[CAT_DESC], updated[16];
    cat_title titles[CAT_TITLES];
    int n;
    /* what was dropped, and why - one line each, for the caller to log */
    char notes[CAT_NOTES][160];
    int n_notes;
} cat_catalogue;

/* Read a catalogue from JSON text.  Returns the number of titles kept, or -1
 * when the text is not a catalogue at all - or is one without the id, drive
 * and origin that say where it goes, since without them it cannot be put
 * anywhere.  A bad title is dropped and noted; a good one after it is still
 * read. */
int cat_parse(cat_catalogue *c, const char *json);

/* The same, from a file.  -1 also when the file cannot be read. */
int cat_read(cat_catalogue *c, const char *path);

/* The categories the format allows, NULL-terminated, and whether a word is
 * one of them. */
extern const char *const cat_categories[];
int cat_category_ok(const char *word);

/* Whether a word is a legal DOS 8.3 name for the purpose here: 1..8 of
 * A-Z, 0-9, and the punctuation DOS allowed, upper case only. */
int cat_id_ok(const char *word);

#endif
