/* internal.h — shared inside src/catalogue only.  The public faces are
 * src/catalogue.h (the files) and src/catalog.h (the program). */
#ifndef DXM_CATALOGUE_INTERNAL_H
#define DXM_CATALOGUE_INTERNAL_H
#include "catalogue.h"
#include <stdint.h>
#include <stddef.h>

/* A catalogue as mounted: its entry in the list, what it holds, and the
 * host folder that is its drive. */
typedef struct {
    cat_entry entry;
    cat_catalogue cat;
    char mount[1024]; /* ends in a separator */
} shelf;

/* ---- art.c — the artwork, fetched, cached, fitted and quantised ------- */

/* The interface takes the palette entries under ART_FIRST (gui.c); the
 * pictures on screen share what is left. */
#define ART_FIRST 48
#define ART_COLOURS (256 - ART_FIRST)
#define ART_BIG_W 270
#define ART_BIG_H 130
#define ART_THUMB_W 88
#define ART_THUMB_H 55
#define ART_THUMBS 5 /* the set can hold a strip of small ones; the screen shows one */

typedef struct {
    int w, h;          /* what it came to inside the box, or 0 for none */
    const uint8_t *px; /* w*h indices 0..ART_COLOURS-1 */
} art_img;

void art_bind(const char *pref_dir);
/* Under a fixed clock the frame has to be the same every run, and a
 * picture that may or may not have arrived is not: show none. */
void art_offline(int on);
/* What should be on screen: one title large, up to ART_THUMBS small.  What
 * is cached is decoded and fitted; what is not is fetched in the background
 * and appears on a later call.  Returns 1 when any picture or the palette
 * changed since the last call, so the caller knows to look again. */
int art_want(const shelf *s, const cat_title *big, const cat_title *const *thumbs, int nthumbs);
const art_img *art_big(void);
const art_img *art_thumb(int i);
const uint8_t *art_palette(void); /* ART_COLOURS RGB triples */

/* ---- install.c — a title onto its drive ------------------------------ */

/* \<CATEGORY>\<ID>\<run> exists on the mount, looked for without regard to
 * case, since the archive may have used either and DOS does not care. */
int install_present(const shelf *s, const cat_title *t);
/* Where the title lives on the host, and as DOS sees it. */
void install_dir(const shelf *s, const cat_title *t, char *host, size_t hn, char *dos, size_t dn);
/* Start one: download, check, unpack, put in place.  One at a time; 0 if
 * one is already running. */
int install_begin(const shelf *s, const cat_title *t, const char *pref_dir);
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
    G_GOLD2,   /* dimmer gold */
    G_GREEN,   /* installed */
    G_RED,     /* something went wrong */
    G_OFF,     /* a key with nothing to do */
    G_TROUGH,  /* the scrollbar's, the progress bar's */
    G_THUMB,
    G_BG2
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
void gui_picture(int x, int y, int w, int h, const art_img *img);
/* one key and what it does, along the foot; returns its width */
int gui_hint(int x, int y, const char *key, const char *what, int hover, int off);
void gui_bar(int x, int y, int w, int h, float t); /* a progress bar */
/* a filter: its name (or NULL) and what it is set to; gold when narrowing */
void gui_chip(int x, int y, int w, int h, const char *label, const char *value, int active,
              int hover);
void gui_panel(int x, int y, int w, int h); /* a panel over the screen */

#endif
