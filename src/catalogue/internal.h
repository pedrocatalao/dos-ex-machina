/* internal.h — shared inside src/catalogue only.  The public faces are
 * src/catalogue.h (the files) and src/catalog.h (the program). */
#ifndef DXM_CATALOGUE_INTERNAL_H
#define DXM_CATALOGUE_INTERNAL_H
#include "catalogue.h"
#include <stdint.h>
#include <stddef.h>

/* A catalogue as loaded: what it holds, and where the file it came from is.
 *
 * A catalogue owns no drive.  It is a list; where any of its titles ended up
 * is a fact about this machine and is kept in where.h, so that a catalogue
 * can be passed to somebody else without carrying an assumption about
 * anyone's disk.
 *
 * A catalogue that ships with the machine sits beside the program - inside
 * the application bundle on a Mac, under Program Files on Windows - where
 * it cannot be written to: the bundle is signed over its contents and
 * changing a file in it stops the machine launching.  So editing one writes
 * a whole copy of it into the machine's own folder, and that copy is what
 * loads from then on.  `shipped` remembers that the original is still
 * there, which is what makes resetting possible; `edited` says the copy
 * exists and is the file being loaded. */
typedef struct {
    cat_catalogue cat;
    char path[1200];    /* the file it was read from */
    char my_path[1200]; /* where this machine's copy of it goes */
    int shipped;        /* an original sits beside the program */
    int edited;         /* this machine has a copy, and that is what is loaded */
} shelf;

/* ---- art.c — the artwork, fetched, cached, fitted and quantised ------- */

/* The interface takes the palette entries under ART_FIRST (gui.c); the
 * pictures on screen share what is left. */
#define ART_FIRST 48
#define ART_COLOURS (256 - ART_FIRST)
#define ART_W 270 /* the box a picture is fitted into */
#define ART_H 130

typedef struct {
    int w, h;          /* what it came to inside the box, or 0 for none */
    const uint8_t *px; /* w*h indices 0..ART_COLOURS-1 */
} art_img;

void art_bind(const char *pref_dir);
/* Under a fixed clock the frame has to be the same every run, and a
 * picture that may or may not have arrived is not: show none. */
void art_offline(int on);
/* The title whose picture should be on screen, or NULL.  What is cached is
 * decoded and fitted; what is not is fetched in the background and appears
 * on a later call.  Returns 1 when the picture or the palette changed since
 * the last call, so the caller knows to look again. */
int art_want(const shelf *s, const cat_title *t);
const art_img *art_picture(void); /* w is 0 when there is none to show */
const uint8_t *art_palette(void); /* ART_COLOURS RGB triples */

/* ---- install.c — a title onto its drive ------------------------------ */

/* Where the machine last put this title, as DOS sees it and on the host -
 * empty when it has never put it anywhere (where.h).  The `run` file is
 * looked for there without regard to case, since an archive may have used
 * either and DOS does not care. */
void install_dir(const shelf *s, const cat_title *t, char *host, size_t hn, char *dos, size_t dn);
int install_present(const shelf *s, const cat_title *t);
/* A DOS path turned into a place on this computer; 0 when nothing is
 * mounted on that drive. */
int install_host_path(const char *dos, char *out, size_t n);
/* Where a title goes when nobody says otherwise:
 * <drive>:\DXM\<catalogue>\<category>\<id>. */
void install_default_dir(const shelf *s, const cat_title *t, char *dos, size_t dn);

/* Start one: fetch or open the archive, check it, unpack it, strip whatever
 * the packer wrapped round it, and put it at `dos_dest`.  `host_archive` is
 * an archive already on this computer, or NULL to fetch the title's own
 * download.  One at a time; 0 if one is already running.
 *
 * When it finishes well the place it landed is written to where.h - the
 * directory holding `run` when the title says what that is, and `dos_dest`
 * itself when it does not, for the caller to refine once somebody has
 * chosen. */
int install_begin(const shelf *s, const cat_title *t, const char *pref_dir, const char *dos_dest,
                  const char *host_archive);
int install_busy(void);
/* what it is doing, and how far: 0..1, or -1 when nothing is running */
float install_progress(char *status, size_t n);
/* 1 once when the last one finished well, -1 once when it failed (with
 * why), 0 otherwise */
int install_take_done(char *err, size_t n);
void install_cancel(void);

/* ---- gui.c — the look: dark, plain, no chrome --------------------------- */

/* the interface's palette, above the VGA sixteen and below the artwork */
enum {
    G_BG = 16, /* the ground */
    G_WELL,    /* a field, a list, the picture: a shade darker */
    G_LINE,    /* the hairline round a well */
    G_TEXT,    /* text */
    G_TEXT2,   /* quieter text */
    G_WHITE,   /* what matters */
    G_BAR,     /* the chosen row */
    G_GOLD,    /* the keys, the caret, the chosen tab */
    G_GREEN,   /* installed */
    G_RED,     /* something went wrong */
    G_OFF,     /* a key with nothing to do */
    G_TROUGH,  /* the scrollbar's, the progress bar's */
    G_THUMB,   /* the scrollbar's handle */
    G_COUNT    /* the first entry after them */
};
void gui_init(void);     /* the palette */
void gui_backdrop(void); /* the whole screen: the ground */
void gui_well(int x, int y, int w, int h);
/* a tab: text, underlined in gold when chosen; `w` gets its width */
void gui_tab(int x, int y, const char *label, int on, int *w);
/* a text field, with what to say when it is empty, and the caret */
void gui_field(int x, int y, int w, int h, const char *text, const char *empty, int caret);
/* a vertical scrollbar: where the window is (0..1) and how much it shows */
void gui_scrollbar(int x, int y, int w, int h, float at, float shown);
/* The artwork where there is some; where there is not, the plate the
 * program draws itself, with `category`'s mark on it. */
void gui_picture(int x, int y, int w, int h, const art_img *img, const char *category);
/* one key and what it does, along the foot; returns its width */
int gui_hint(int x, int y, const char *key, const char *what, int hover, int off);
void gui_bar(int x, int y, int w, int h, float t); /* a progress bar */
/* a filter: its name (or NULL) and what it is set to; gold when narrowing */
void gui_chip(int x, int y, int w, int h, const char *label, const char *value, int active,
              int hover);
void gui_panel(int x, int y, int w, int h); /* a panel over the screen */
/* `s` cut down to `w` pixels, with three dots where it was cut.  A name is
 * cut at its end and a path at its front: the end of a path is the part
 * that says where you are. */
void gui_fit(const char *s, int w, int from_front, char *out, int n);
/* What a key puts in a field.  The scancode is where the key sits, not what
 * is printed on it, so a board that is not American gives its own marks for
 * the punctuation. */
char gui_typed(int sdl_scancode, int shift);

/* ---- edit.c — changing what is in a catalogue ------------------------- */

/* The editor is a stack of panels over the screen: a menu, a form, the
 * browser that finds a file, and the question asked before anything is
 * removed.  While one is up it has the keys and the pointer to itself and
 * the screen behind it answers nothing, which is what `edit_up` is for. */
int edit_up(void);
void edit_open(shelf *s); /* the menu, for this catalogue */
/* Where to put it, asked before anything is fetched: a panel with the
 * default filled in, which ENTER accepts.  What an installer of the period
 * opened with. */
void edit_install(shelf *s, const cat_title *t);
/* The same panel asking the other question: where it already is.  Writes
 * the record and checks the folder, and moves nothing. */
void edit_point(shelf *s, const cat_title *t);
/* What the installer finished with, for an add that was waiting on it. */
void edit_installed(int ok, const char *err);
void edit_key(int sdl_scancode, int shift);
void edit_click(int mx, int my);
void edit_draw(void); /* over everything, last */

/* ---- screen.c, for the editor to reach the shelves -------------------- */

/* Re-read every catalogue from disk, come back to `shelf_id` with
 * `title_id` under the cursor if they are still there, and say `note` at
 * the top of the screen.  What is on screen is then what is in the files,
 * read by the same reader as everything else. */
void cat_reload(const char *shelf_id, const char *title_id, const char *note, int bad);
/* Every shelf the machine has loaded, so a new catalogue can be checked
 * against them for a clash of id before it is written. */
const shelf *cat_shelves(int *n);
/* Which title the cursor is on, as an index into the catalogue's own table
 * (not into what the filters are showing), or -1. */
int cat_picked_index(void);
/* The folder this machine writes catalogues into, ending in a separator,
 * and the machine's own folder above it. */
const char *cat_my_dir(void);
const char *cat_pref_dir(void);
/* The host folder a DOS drive letter stands for, ending in a separator, or
 * 0 when nothing is mounted on that letter.  Only C: for now: catalogues no
 * longer bring drives with them, and mounting others is SETUP's to offer. */
int cat_drive_mount(char letter, char *out, size_t n);
/* The drive a title goes on when nobody says otherwise. */
char cat_default_drive(void);

/* ---- plate.c — the pictures the program draws itself ------------------ */

/* The picture box for a title with no artwork: a dithered EGA plate with a
 * mark on it for its category.  `category` NULL or empty leaves the plate
 * bare, which is what the box shows when nothing is chosen. */
void plate_picture(int x, int y, int w, int h, const char *category);
/* The colour bars at the end of a rule, and how wide they come out. */
void plate_colourbar(int x, int y);
int plate_colourbar_w(void);

#endif
