/* screen.c — CATALOG: what it holds, what the keys do, and the screen it
 * draws (SPEC §8.3).
 *
 * Tabs for the catalogues along the top with the count beside them; on the
 * left a row of filters over a Find field over the list of titles; on the
 * right the chosen title's picture and particulars; along the foot the keys
 * and what they do.  Typing goes to the Find field and the list narrows as
 * you type, so the actions are on function keys and the mouse, and ENTER
 * does the obvious thing to the chosen title.  RUN, SETUP and PROMPT are
 * errands the DOS runs while this screen waits out of sight; it comes back
 * as it was. */
#include "catalog.h"
#include "internal.h"
#include "setup/internal.h"
#include "dosbox.h"
#include "log.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* the layout, in the 640x400 */
enum {
    MARGIN = 24,
    TAB_Y = 12, /* the catalogues, and the count over on the right */
    RULE_Y = TAB_Y + CV_LINE + 8,
    LEFT_X = MARGIN,
    LEFT_W = 296,
    CHIP_Y = RULE_Y + 12, /* the filters */
    CHIP_H = 20,
    CHIP_GAP = 6,
    FIND_Y = CHIP_Y + CHIP_H + 8,
    FIND_H = 24,
    LIST_Y = FIND_Y + FIND_H + 8,
    ROW_H = 16,
    SCROLL_W = 12,
    RIGHT_X = LEFT_X + LEFT_W + 20,
    RIGHT_W = SCR_W - MARGIN - RIGHT_X,
    PIC_Y = CHIP_Y,
    PIC_H = ART_H + 6,
    NAME_Y = PIC_Y + PIC_H + 10,
    LABEL_W = 64,                      /* the particulars' labels */
    DETAIL_Y = NAME_Y - 2,             /* what scrolls: everything under the picture */
    DETAIL_W = RIGHT_W - SCROLL_W - 6, /* the text, leaving room for the scrollbar */
    FOOT_Y = SCR_H - 26,
    FOOT_RULE = FOOT_Y - 10,
    LIST_H = FOOT_RULE - 10 - LIST_Y,
    DETAIL_H = FOOT_RULE - 6 - DETAIL_Y,
    ROWS = (LIST_H - 4) / ROW_H,
    DOUBLE_MS = 400
};

/* Coming up: a moment of loading screen, then the screen arrives in bands
 * slid in from alternate sides.  Coming back from an errand, the bands
 * alone.  Under a fixed clock, neither. */
#define LOAD_MS 400
#define SLIDE_MS 450
#define BANDS 16
/* Leaving: the screen goes down to black on the palette, and only then is
 * the program over and the prompt back. */
#define CLOSE_MS 300

/* ---- the filters --------------------------------------------------------
 * Three along the top of the list, cycled by a click, and more behind the
 * [+], in a panel of their own.  Every one of them starts at "All". */

enum { F_TYPE, F_PLAYERS, F_NETWORK, F_YEAR, F_VIDEO, F_SOUND, F_CONTROLS, F_INSTALLED, FILTERS };
#define ROW_FILTERS 3 /* the ones along the top; the rest are in the panel */

static const char *const TYPE_OPT[] = {"All", "Games", "Education", "Tools", "Music"};
static const char *const TYPE_CAT[] = {NULL, "GAMES", "EDUCATION", "TOOLS", "MUSIC"};
static const char *const PLAYERS_OPT[] = {"All", "Single", "Multi"};
static const char *const NETWORK_OPT[] = {"All", "Yes", "No"};
static const char *const YEAR_OPT[] = {"All", "Before 1990", "1990 - 1994", "1995 - 1999",
                                       "2000 and later"};
static const char *const VIDEO_OPT[] = {"All", "CGA", "EGA", "VGA", "SVGA"};
static const char *const SOUND_OPT[] = {"All",           "PC speaker",        "AdLib",
                                        "Sound Blaster", "Gravis Ultrasound", "MIDI"};
static const char *const CONTROLS_OPT[] = {"All", "Keyboard", "Mouse", "Joystick"};
static const char *const INSTALLED_OPT[] = {"All", "Installed", "Not installed"};

static const struct {
    const char *label;
    const char *const *opt;
    int n;
} FILTER[FILTERS] = {
    {"Type", TYPE_OPT, 5},         {"Players", PLAYERS_OPT, 3},
    {"Network", NETWORK_OPT, 3},   {"Year", YEAR_OPT, 5},
    {"Video", VIDEO_OPT, 5},       {"Sound", SOUND_OPT, 6},
    {"Controls", CONTROLS_OPT, 4}, {"On the drive", INSTALLED_OPT, 3},
};

/* the panel behind the [+] */
enum {
    POP_W = 300,
    POP_ROW = 24,
    POP_H = 38 + (FILTERS - ROW_FILTERS) * POP_ROW + 34,
    POP_X = (SCR_W - POP_W) / 2,
    POP_Y = (SCR_H - POP_H) / 2,
    POP_VALUE_X = POP_X + 120
};

static shelf shelves[CAT_LIST];
static int nshelves;

static struct {
    int running, away; /* the program is up; the screen is out of sight */
    int shelf, pick;   /* which catalogue, which title (an index into `shown`) */
    int scroll;
    char find[32];
    int filter[FILTERS];           /* an index into each filter's options; 0 is All */
    int more;                      /* the panel of more filters is up */
    int more_row;                  /* where its cursor is: a filter, or Reset */
    int shown[CAT_TITLES], nshown; /* the titles that pass, as indices */
    int mx, my, held, hover;
    int fixed_clock;
    Uint64 open_t0, slide_t0, click_t0; /* open_t0 is 0 when there was no loading */
    Uint64 close_t0;                    /* when the fade out began, or 0 */
    int click_row;
    int dscroll, dheight;    /* the details: how far scrolled, and how tall they are */
    const cat_title *dtitle; /* whose details they are, so a new one starts at the top */
    char base[1024], pref[1024], c_drive[1024];
    /* what the pointer can land on this frame */
    struct hit {
        int x, y, w, h, what;
    } hit[64];
    int nhit;
    char note[160]; /* a line for the user: an install that failed, say */
    int note_bad;
    Uint64 note_t0;
    Uint64 typed_t0; /* the last keystroke into the Find field: the caret restarts on */
} S = {.mx = SCR_W / 2, .my = SCR_H / 2, .hover = -1, .click_row = -1};

/* what a hit is: 0..999 a row of the list, and the rest by range */
enum {
    HIT_TAB = 1000,
    HIT_KEY = 2000,
    HIT_SCROLL_UP = 3000,
    HIT_SCROLL_DOWN,
    HIT_CHIP = 4000, /* + filter; HIT_CHIP + FILTERS is the [+] */
    HIT_POP = 5000,  /* + filter, inside the panel */
    HIT_POP_RESET = 5100,
    HIT_POP_CLOSE,
    HIT_POP_PANEL,        /* the panel itself, so a click on it does not close it */
    HIT_DETAIL_UP = 6000, /* the details' scrollbar, above and below its thumb */
    HIT_DETAIL_DOWN,
    HIT_DETAIL_LINE_UP,
    HIT_DETAIL_LINE_DOWN
};
enum { KEY_MAIN, KEY_SETUP, KEY_PROMPT, KEY_FILTERS, KEY_TAB, KEY_BACK, KEY_CANCEL };

void catalog_bind(const char *base_dir, const char *pref_dir, const char *c_drive) {
    snprintf(S.base, sizeof S.base, "%s", base_dir ? base_dir : "./");
    snprintf(S.pref, sizeof S.pref, "%s", pref_dir ? pref_dir : "./");
    snprintf(S.c_drive, sizeof S.c_drive, "%s", c_drive ? c_drive : "./");
    art_bind(S.pref);
}

void catalog_fixed_clock(int fixed) {
    S.fixed_clock = fixed;
    art_offline(fixed); /* the same picture every run: no artwork at all */
}

/* Every .cat in the catalogues folder beside the program, each saying for
 * itself where it goes.  They are kept in the order of their drives, so C:
 * is the first tab; one that wants a letter or an id already taken is left
 * out, and everything wrong with any of them goes to the log. */
typedef struct {
    char names[CAT_LIST * 2][64];
    int n;
} found_cats;

static SDL_EnumerationResult found_cat(void *ud, const char *dirname, const char *fname) {
    (void)dirname;
    found_cats *f = ud;
    size_t len = strlen(fname);
    if (len > 4 && len < sizeof f->names[0] && !SDL_strcasecmp(fname + len - 4, ".cat") &&
        f->n < (int)(sizeof f->names / sizeof f->names[0]))
        snprintf(f->names[f->n++], sizeof f->names[0], "%s", fname);
    return SDL_ENUM_CONTINUE;
}

static int by_name(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}
static int by_drive(const void *a, const void *b) {
    return ((const shelf *)a)->cat.drive - ((const shelf *)b)->cat.drive;
}

void catalog_load(void) {
    char dir[1400], path[1400];
    snprintf(dir, sizeof dir, "%scatalogues/", S.base);
    nshelves = 0;
    found_cats f = {.n = 0};
    if (!SDL_EnumerateDirectory(dir, found_cat, &f) || !f.n) {
        dxm_log("catalog: no catalogues in %s", dir);
        return;
    }
    /* a folder lists in whatever order its filesystem likes: sort it, so
     * which of two clashing catalogues wins is the same on every machine */
    qsort(f.names, (size_t)f.n, sizeof f.names[0], by_name);
    for (int i = 0; i < f.n; i++) {
        if (nshelves >= CAT_LIST) {
            dxm_log("catalog: more catalogues than the machine holds; %s and after left out",
                    f.names[i]);
            break;
        }
        shelf *s = &shelves[nshelves];
        snprintf(path, sizeof path, "%s%s", dir, f.names[i]);
        int n = cat_read(&s->cat, path);
        for (int k = 0; k < s->cat.n_notes; k++)
            dxm_log("catalog: %s: %s", f.names[i], s->cat.notes[k]);
        if (n < 0) {
            dxm_log("catalog: %s is not a catalogue this machine can use", f.names[i]);
            continue;
        }
        int clash = 0;
        for (int k = 0; k < nshelves; k++)
            if (shelves[k].cat.drive == s->cat.drive || !strcmp(shelves[k].cat.id, s->cat.id)) {
                dxm_log("catalog: %s wants %c: or the id %s, which %s already has", f.names[i],
                        s->cat.drive, s->cat.id, shelves[k].cat.id);
                clash = 1;
            }
        if (clash)
            continue;
        /* the folder that is its drive: C: is the machine's own.  The id is
         * copied out first: it lives in the same struct as the mount, and
         * GCC will not have snprintf read from what it is writing into. */
        char id[CAT_ID];
        memcpy(id, s->cat.id, sizeof id);
        if (s->cat.drive == 'C')
            snprintf(s->mount, sizeof s->mount, "%s/", S.c_drive);
        else
            snprintf(s->mount, sizeof s->mount, "%scatalogues/%s/", S.pref, id);
        SDL_CreateDirectory(s->mount);
        dxm_log("catalog: %s on %c: - %d titles, %s", s->cat.name, s->cat.drive, s->cat.n,
                s->cat.community ? "community" : "bundled");
        nshelves++;
    }
    qsort(shelves, (size_t)nshelves, sizeof shelves[0], by_drive);
}

void catalog_drives(char *out, size_t n) {
    size_t k = 0;
    out[0] = 0;
    for (int i = 0; i < nshelves; i++) {
        const shelf *s = &shelves[i];
        if (s->cat.drive == 'C' || k >= n)
            continue;
        k += (size_t)snprintf(out + k, n - k, "%c=%s=%s\n", s->cat.drive, s->cat.id, s->mount);
    }
}

static shelf *cur(void) {
    return nshelves ? &shelves[S.shelf] : NULL;
}
static const cat_title *picked(void) {
    shelf *s = cur();
    return (s && S.nshown) ? &s->cat.titles[S.shown[S.pick]] : NULL;
}

/* ---- which titles pass --------------------------------------------------- */

static int has_word(const char (*list)[CAT_WORD], const char *word) {
    for (int i = 0; i < CAT_WORDS && list[i][0]; i++)
        if (!SDL_strcasecmp(list[i], word))
            return 1;
    return 0;
}

static int passes(const shelf *s, const cat_title *t) {
    const int *f = S.filter;
    if (f[F_TYPE] && strcmp(t->category, TYPE_CAT[f[F_TYPE]]))
        return 0;
    if (f[F_PLAYERS] && t->multiplayer != (f[F_PLAYERS] == 2))
        return 0;
    if (f[F_NETWORK] && t->network != (f[F_NETWORK] == 1))
        return 0;
    if (f[F_YEAR]) {
        static const int lo[] = {0, 0, 1990, 1995, 2000}, hi[] = {0, 1989, 1994, 1999, 9999};
        if (t->year < lo[f[F_YEAR]] || t->year > hi[f[F_YEAR]])
            return 0;
    }
    if (f[F_VIDEO] && strcmp(t->video, VIDEO_OPT[f[F_VIDEO]]))
        return 0;
    if (f[F_SOUND]) {
        static const char *const word[] = {NULL, "pcspeaker", "adlib", "sb", "gus", NULL};
        if (f[F_SOUND] == 5) { /* MIDI: any of the MIDI devices */
            if (!has_word(t->sound, "mt32") && !has_word(t->sound, "gm") &&
                !has_word(t->sound, "sc55") && !has_word(t->sound, "midi"))
                return 0;
        } else if (!has_word(t->sound, word[f[F_SOUND]]))
            return 0;
    }
    if (f[F_CONTROLS] && !has_word(t->controls, CONTROLS_OPT[f[F_CONTROLS]]))
        return 0;
    if (f[F_INSTALLED] && install_present(s, t) != (f[F_INSTALLED] == 1))
        return 0;
    if (!S.find[0])
        return 1;
    char hay[CAT_NAME + 80 + 2], needle[32];
    snprintf(hay, sizeof hay, "%s %s", t->name, t->creator);
    for (char *p = hay; *p; p++)
        *p = (char)tolower((unsigned char)*p);
    snprintf(needle, sizeof needle, "%s", S.find);
    for (char *p = needle; *p; p++)
        *p = (char)tolower((unsigned char)*p);
    return strstr(hay, needle) != NULL;
}

static void refilter(void) {
    shelf *s = cur();
    int was = S.nshown ? S.shown[S.pick] : -1;
    S.nshown = 0;
    if (s)
        for (int i = 0; i < s->cat.n; i++)
            if (passes(s, &s->cat.titles[i]))
                S.shown[S.nshown++] = i;
    /* keep the eye on the same title if it is still there, else the first */
    S.pick = 0;
    for (int i = 0; i < S.nshown; i++)
        if (S.shown[i] == was)
            S.pick = i;
    if (S.pick < S.scroll)
        S.scroll = S.pick;
    if (S.pick >= S.scroll + ROWS)
        S.scroll = S.pick - ROWS + 1;
    if (S.scroll < 0)
        S.scroll = 0;
}

static int more_active(void) {
    int n = 0;
    for (int i = ROW_FILTERS; i < FILTERS; i++)
        n += S.filter[i] != 0;
    return n;
}

static void cycle_by(int which, int by) {
    S.filter[which] = (S.filter[which] + by + FILTER[which].n) % FILTER[which].n;
    refilter();
}
static void cycle(int which) {
    cycle_by(which, 1);
}

/* the panel's rows: one a filter behind the [+], and Reset after them */
enum { MORE_ROWS = FILTERS - ROW_FILTERS + 1, MORE_RESET = MORE_ROWS - 1 };

static void reset_more(void) {
    for (int i = ROW_FILTERS; i < FILTERS; i++)
        S.filter[i] = 0;
    refilter();
}

static void open_more(void) {
    S.more = 1;
    S.more_row = 0;
}

/* ---- coming and going ---------------------------------------------------- */

void catalog_open(void) {
    if (S.running)
        return;
    S.running = 1;
    S.away = 0;
    S.more = 0;
    S.mx = SCR_W / 2;
    S.my = SCR_H / 2;
    S.open_t0 = SDL_GetTicks();
    S.slide_t0 = S.open_t0 + LOAD_MS;
    S.close_t0 = 0;
    S.note[0] = 0;
    S.find[0] = 0;
    refilter();
    dxm_log("catalog: open");
}

/* The fade out is still the program running: DOS waits at CATALOG until
 * the screen is black, so the prompt does not appear under it. */
static void closed(void) {
    S.running = 0;
    S.away = 0;
    S.close_t0 = 0;
    dxm_log("catalog: closed");
}

void catalog_close(void) {
    if (!S.running || S.close_t0)
        return;
    if (S.fixed_clock || S.away)
        closed(); /* nothing on screen to fade */
    else
        S.close_t0 = SDL_GetTicks();
}

int catalog_running(void) {
    return S.running;
}
int catalog_visible(void) {
    return S.running && !S.away;
}
void catalog_back(void) {
    if (S.running && S.away) {
        S.away = 0;
        S.open_t0 = 0; /* no loading screen twice: straight to the bands */
        S.slide_t0 = SDL_GetTicks();
        refilter(); /* what is on the drive may have changed */
        dxm_log("catalog: back");
    }
}

static void say(const char *what, int bad) {
    snprintf(S.note, sizeof S.note, "%s", what);
    S.note_bad = bad;
    S.note_t0 = SDL_GetTicks();
}

/* ---- what the keys do -------------------------------------------------- */

static void detail_scroll(int px);

static void show_row(int r) {
    if (r < S.scroll)
        S.scroll = r;
    if (r >= S.scroll + ROWS)
        S.scroll = r - ROWS + 1;
}

static void move(int by) {
    if (!S.nshown)
        return;
    S.pick += by;
    if (S.pick < 0)
        S.pick = 0;
    if (S.pick >= S.nshown)
        S.pick = S.nshown - 1;
    show_row(S.pick);
}

static void go_shelf(int to) {
    if (!nshelves)
        return;
    S.shelf = (to + nshelves) % nshelves;
    S.scroll = 0;
    S.find[0] = 0;
    refilter();
}

/* The errands.  Each is a drive, a directory and a line for the DOS; the
 * screen steps out of sight until the core says it is back. */
static void errand(int kind, const char *cmd) {
    shelf *s = cur();
    const cat_title *t = picked();
    if (!s || !t || !install_present(s, t))
        return;
    char dos[80];
    install_dir(s, t, NULL, 0, dos, sizeof dos);
    dosbox_catalog_run(s->cat.drive, dos, cmd, kind);
    S.away = 1;
    dxm_log("catalog: %s in %c:%s", kind ? "prompt" : cmd, s->cat.drive, dos);
}

static void enter(void) {
    shelf *s = cur();
    const cat_title *t = picked();
    if (!s || !t || install_busy())
        return;
    if (install_present(s, t))
        errand(0, t->run);
    else if (!install_begin(s, t, S.pref))
        say("Something else is installing.", 1);
}

/* What a key types into the Find field: the letters, the digits and the
 * few marks a title's name can carry, on the host's own board as far as
 * the scancode says. */
static char typed(int sc, int shift) {
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
        return (char)((shift ? 'A' : 'a') + (sc - SDL_SCANCODE_A));
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
        return (char)('1' + (sc - SDL_SCANCODE_1));
    switch (sc) {
    case SDL_SCANCODE_0:
        return '0';
    case SDL_SCANCODE_SPACE:
        return ' ';
    case SDL_SCANCODE_MINUS:
        return '-';
    case SDL_SCANCODE_PERIOD:
        return '.';
    case SDL_SCANCODE_APOSTROPHE:
        return '\'';
    default:
        return 0;
    }
}

void catalog_key(int sdl_scancode, int shift) {
    if (S.away || S.close_t0)
        return;
    /* F4 to F6 cycle the filters along the top, as a click on them does,
     * backwards with shift; F7 opens and closes the panel of the rest.  They
     * work while something installs, since they only change what is shown. */
    if (sdl_scancode >= SDL_SCANCODE_F4 && sdl_scancode <= SDL_SCANCODE_F6) {
        if (!S.more)
            cycle_by(F_TYPE + (sdl_scancode - SDL_SCANCODE_F4), shift ? -1 : 1);
        return;
    }
    if (sdl_scancode == SDL_SCANCODE_F7) {
        if (S.more)
            S.more = 0;
        else
            open_more();
        return;
    }
    if (S.more) {
        /* The panel has the keys: up and down move its cursor, ENTER (or
         * right) cycles the filter under it forwards and left backwards,
         * ENTER on Reset clears them all, and ESC closes it. */
        int row = S.more_row, filter = ROW_FILTERS + row;
        switch (sdl_scancode) {
        case SDL_SCANCODE_ESCAPE:
            S.more = 0;
            break;
        case SDL_SCANCODE_UP:
            S.more_row = (row + MORE_ROWS - 1) % MORE_ROWS;
            break;
        case SDL_SCANCODE_DOWN:
            S.more_row = (row + 1) % MORE_ROWS;
            break;
        case SDL_SCANCODE_HOME:
            S.more_row = 0;
            break;
        case SDL_SCANCODE_END:
            S.more_row = MORE_RESET;
            break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_SPACE:
            if (row == MORE_RESET)
                reset_more();
            else
                cycle_by(filter, shift ? -1 : 1);
            break;
        case SDL_SCANCODE_RIGHT:
            if (row != MORE_RESET)
                cycle_by(filter, 1);
            break;
        case SDL_SCANCODE_LEFT:
            if (row != MORE_RESET)
                cycle_by(filter, -1);
            break;
        default:
            break;
        }
        return;
    }
    if (install_busy()) {
        if (sdl_scancode == SDL_SCANCODE_ESCAPE)
            install_cancel();
        return;
    }
    const cat_title *t = picked();
    switch (sdl_scancode) {
    case SDL_SCANCODE_ESCAPE:
        if (S.find[0]) { /* first the search goes, then the program */
            S.find[0] = 0;
            refilter();
        } else
            catalog_close();
        break;
    /* with shift held, the keys that move the list scroll the details */
    case SDL_SCANCODE_UP:
        if (shift)
            detail_scroll(-CV_LINE);
        else
            move(-1);
        break;
    case SDL_SCANCODE_DOWN:
        if (shift)
            detail_scroll(CV_LINE);
        else
            move(1);
        break;
    case SDL_SCANCODE_PAGEUP:
        if (shift)
            detail_scroll(-(DETAIL_H - CV_LINE));
        else
            move(-ROWS);
        break;
    case SDL_SCANCODE_PAGEDOWN:
        if (shift)
            detail_scroll(DETAIL_H - CV_LINE);
        else
            move(ROWS);
        break;
    case SDL_SCANCODE_HOME:
        move(-S.nshown);
        break;
    case SDL_SCANCODE_END:
        move(S.nshown);
        break;
    /* left and right step through the catalogues: nothing else on this
     * screen moves sideways - the Find field is typed into at its end - and
     * the panel of filters, which does use them, has the keys to itself */
    case SDL_SCANCODE_LEFT:
        go_shelf(S.shelf - 1);
        break;
    case SDL_SCANCODE_RIGHT:
        go_shelf(S.shelf + 1);
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        enter();
        break;
    case SDL_SCANCODE_F2:
        if (t && t->setup[0])
            errand(0, t->setup);
        break;
    case SDL_SCANCODE_F3:
        errand(1, "");
        break;
    case SDL_SCANCODE_BACKSPACE: {
        size_t n = strlen(S.find);
        if (n) {
            S.find[n - 1] = 0;
            S.typed_t0 = SDL_GetTicks();
            refilter();
        }
        break;
    }
    default: {
        char c = typed(sdl_scancode, shift);
        size_t n = strlen(S.find);
        if (c && n + 1 < sizeof S.find) {
            S.find[n] = c;
            S.find[n + 1] = 0;
            S.typed_t0 = SDL_GetTicks();
            refilter();
        }
        break;
    }
    }
}

/* The details under the picture, moved by `px` pixels and kept inside what
 * there is to see. */
static void detail_scroll(int px) {
    int max = S.dheight > DETAIL_H ? S.dheight - DETAIL_H : 0;
    S.dscroll += px;
    if (S.dscroll < 0)
        S.dscroll = 0;
    if (S.dscroll > max)
        S.dscroll = max;
}

/* The wheel scrolls whichever side the pointer is over. */
void catalog_wheel(int by) {
    if (S.away || S.close_t0 || S.more)
        return;
    if (S.mx >= RIGHT_X) {
        detail_scroll(-by * CV_LINE);
        return;
    }
    if (install_busy())
        return;
    int max = S.nshown > ROWS ? S.nshown - ROWS : 0;
    S.scroll -= by;
    if (S.scroll < 0)
        S.scroll = 0;
    if (S.scroll > max)
        S.scroll = max;
}

/* ---- the pointer ------------------------------------------------------- */

static int hit_at(int x, int y) {
    for (int i = 0; i < S.nhit; i++)
        if (x >= S.hit[i].x && x < S.hit[i].x + S.hit[i].w && y >= S.hit[i].y &&
            y < S.hit[i].y + S.hit[i].h)
            return i;
    return -1;
}

void catalog_mouse(int dx, int dy) {
    S.mx += dx;
    S.my += dy;
    if (S.mx < 0)
        S.mx = 0;
    if (S.my < 0)
        S.my = 0;
    if (S.mx >= SCR_W)
        S.mx = SCR_W - 1;
    if (S.my >= SCR_H)
        S.my = SCR_H - 1;
    S.hover = hit_at(S.mx, S.my);
}

void catalog_click(int down) {
    S.held = down;
    if (!down || S.away || S.close_t0)
        return;
    int h = hit_at(S.mx, S.my);
    int what = h < 0 ? -1 : S.hit[h].what;
    if (S.more) { /* the panel: its rows cycle, anywhere off it closes it */
        if (what >= HIT_POP && what < HIT_POP + FILTERS) {
            S.more_row = what - HIT_POP - ROW_FILTERS; /* the cursor goes where the click was */
            cycle(what - HIT_POP);
        } else if (what == HIT_POP_RESET) {
            S.more_row = MORE_RESET;
            reset_more();
        } else if (what != HIT_POP_PANEL)
            S.more = 0;
        return;
    }
    if (what < 0)
        return;
    if (install_busy()) {
        if (what == HIT_KEY + KEY_CANCEL)
            install_cancel();
        return;
    }
    const cat_title *t = picked();
    Uint64 now = SDL_GetTicks();
    if (what < HIT_TAB) {
        /* a row; the same row again inside the moment is a double-click */
        int again = what == S.click_row && now - S.click_t0 < DOUBLE_MS;
        S.pick = what;
        S.click_row = what;
        S.click_t0 = now;
        if (again)
            enter();
    } else if (what < HIT_KEY)
        go_shelf(what - HIT_TAB);
    else if (what == HIT_KEY + KEY_MAIN)
        enter();
    else if (what == HIT_KEY + KEY_SETUP && t && t->setup[0])
        errand(0, t->setup);
    else if (what == HIT_KEY + KEY_PROMPT)
        errand(1, "");
    else if (what == HIT_KEY + KEY_FILTERS)
        open_more(); /* the one of them that is not already a click away */
    else if (what == HIT_KEY + KEY_TAB)
        go_shelf(S.shelf + 1);
    else if (what == HIT_KEY + KEY_BACK)
        catalog_key(SDL_SCANCODE_ESCAPE, 0);
    else if (what == HIT_SCROLL_UP)
        catalog_wheel(1);
    else if (what == HIT_SCROLL_DOWN)
        catalog_wheel(-1);
    else if (what == HIT_DETAIL_UP)
        detail_scroll(-(DETAIL_H - CV_LINE));
    else if (what == HIT_DETAIL_DOWN)
        detail_scroll(DETAIL_H - CV_LINE);
    else if (what == HIT_DETAIL_LINE_UP)
        detail_scroll(-CV_LINE);
    else if (what == HIT_DETAIL_LINE_DOWN)
        detail_scroll(CV_LINE);
    else if (what >= HIT_CHIP && what < HIT_CHIP + ROW_FILTERS)
        cycle(what - HIT_CHIP);
    else if (what == HIT_CHIP + FILTERS)
        open_more();
}

/* ---- the screen -------------------------------------------------------- */

static void add_hit(int x, int y, int w, int h, int what) {
    if (S.nhit < (int)(sizeof S.hit / sizeof S.hit[0]))
        S.hit[S.nhit++] = (struct hit){x, y, w, h, what};
}

static int hovering(int what) {
    return S.hover >= 0 && S.hover < S.nhit && S.hit[S.hover].what == what;
}

/* A catalogue's words, as a person would write them. */
static const char *said(const char *word) {
    static const struct {
        const char *word, *says;
    } W[] = {
        {"pcspeaker", "PC speaker"},  {"adlib", "AdLib"},       {"sb", "Sound Blaster"},
        {"gus", "Gravis Ultrasound"}, {"mt32", "Roland MT-32"}, {"sc55", "Roland SC-55"},
        {"gm", "General MIDI"},       {"midi", "MIDI"},         {"tandy", "Tandy"},
        {"covox", "Covox"},           {"keyboard", "Keyboard"}, {"mouse", "Mouse"},
        {"joystick", "Joystick"},     {"gamepad", "Gamepad"},
    };
    for (size_t i = 0; i < sizeof W / sizeof W[0]; i++)
        if (!SDL_strcasecmp(word, W[i].word))
            return W[i].says;
    return word;
}

static void words(char *out, size_t n, const char (*list)[CAT_WORD]) {
    out[0] = 0;
    for (int i = 0; i < CAT_WORDS && list[i][0]; i++) {
        size_t k = strlen(out);
        snprintf(out + k, n - k, "%s%s", i ? ", " : "", said(list[i]));
    }
}

static void draw_chips(void) {
    /* Each as wide as its widest option, so a click does not move the
     * others; what the left column has over is shared out among them, and
     * the [+] keeps the end of the row. */
    enum { PAD = 10, PLUS_W = 22 };
    int w[ROW_FILTERS], total = 0;
    for (int i = 0; i < ROW_FILTERS; i++) {
        w[i] = 0;
        for (int k = 0; k < FILTER[i].n; k++) {
            int ww = cv_width_small(FILTER[i].opt[k]);
            w[i] = ww > w[i] ? ww : w[i];
        }
        if (i != F_TYPE)
            w[i] += cv_width_small(FILTER[i].label) + 5;
        w[i] += PAD;
        total += w[i];
    }
    /* what the column has over (or is short) is shared out exactly, the odd
     * pixels to the first, so every gap is the same and the [+] ends flush
     * with the column */
    int spare = LEFT_W - PLUS_W - ROW_FILTERS * CHIP_GAP - total;
    int x = LEFT_X;
    for (int i = 0; i < ROW_FILTERS; i++) {
        int cw = w[i] + spare / ROW_FILTERS + (i == 0 ? spare % ROW_FILTERS : 0);
        gui_chip(x, CHIP_Y, cw, CHIP_H, i == F_TYPE ? NULL : FILTER[i].label,
                 FILTER[i].opt[S.filter[i]], S.filter[i] != 0, hovering(HIT_CHIP + i));
        add_hit(x, CHIP_Y, cw, CHIP_H, HIT_CHIP + i);
        x += cw + CHIP_GAP;
    }
    /* the [+], and how many of what is behind it are set */
    int n = more_active();
    char plus[8];
    snprintf(plus, sizeof plus, n ? "+%d" : "+", n);
    int px = x;
    gui_chip(px, CHIP_Y, PLUS_W, CHIP_H, NULL, plus, n != 0, hovering(HIT_CHIP + FILTERS));
    add_hit(px, CHIP_Y, PLUS_W, CHIP_H, HIT_CHIP + FILTERS);
}

static void draw_list(const shelf *s) {
    int x = LEFT_X, w = LEFT_W - SCROLL_W - 4;
    gui_well(x, LIST_Y, w, LIST_H);
    for (int i = 0; i < ROWS && S.scroll + i < S.nshown; i++) {
        int r = S.scroll + i, y = LIST_Y + 2 + i * ROW_H, on = r == S.pick;
        const cat_title *t = &s->cat.titles[S.shown[r]];
        if (on) {
            cv_rect(x + 1, y, w - 2, ROW_H, G_BAR);
            cv_rect(x + 1, y, 3, ROW_H, G_GOLD);
        }
        if (install_present(s, t))
            cv_text(x + 9, y, "\x07", G_GREEN, -1);
        if (on)
            cv_text_bold(x + 20, y, t->name, G_WHITE, -1);
        else
            cv_text(x + 20, y, t->name, G_TEXT, -1);
        char year[8];
        snprintf(year, sizeof year, "%d", t->year);
        cv_text(x + w - 8 - cv_width(year), y, year, on ? G_TEXT : G_TEXT2, -1);
        add_hit(x + 1, y, w - 2, ROW_H, r);
    }
    if (!S.nshown) {
        const char *msg = s->cat.n ? "Nothing matches." : "Nothing on this shelf.";
        cv_text(x + 10, LIST_Y + 8, msg, G_TEXT2, -1);
    }
    /* the scrollbar, whether or not there is anything to scroll */
    int sx = x + w + 4;
    float shown = S.nshown > ROWS ? (float)ROWS / (float)S.nshown : 1.0f;
    float at = S.nshown > ROWS ? (float)S.scroll / (float)(S.nshown - ROWS) : 0.0f;
    gui_scrollbar(sx, LIST_Y, SCROLL_W, LIST_H, at, shown);
    add_hit(sx, LIST_Y, SCROLL_W, SCROLL_W + 2, HIT_SCROLL_UP);
    add_hit(sx, LIST_Y + LIST_H - SCROLL_W - 2, SCROLL_W, SCROLL_W + 2, HIT_SCROLL_DOWN);
}

/* Top right, beside the tabs: the count, or while something is happening,
 * what it is. */
static void draw_status(const shelf *s) {
    int right = SCR_W - MARGIN;
    char status[80];
    float p = install_progress(status, sizeof status);
    if (p >= 0.0f) {
        int bw = 120;
        gui_bar(right - bw, TAB_Y + 6, bw, 5, p);
        cv_text(right - bw - 10 - cv_width(status), TAB_Y, status, G_WHITE, -1);
        return;
    }
    if (S.note[0]) {
        cv_text(right - cv_width(S.note), TAB_Y, S.note, S.note_bad ? G_RED : G_GREEN, -1);
        if (SDL_GetTicks() - S.note_t0 > 8000)
            S.note[0] = 0;
        return;
    }
    if (!s)
        return;
    int installed = 0;
    for (int i = 0; i < s->cat.n; i++)
        installed += install_present(s, &s->cat.titles[i]);
    char line[96];
    if (S.nshown != s->cat.n)
        snprintf(line, sizeof line, "%d of %d titles  \x07  %d installed  \x07  %c:", S.nshown,
                 s->cat.n, installed, s->cat.drive);
    else
        snprintf(line, sizeof line, "%d titles  \x07  %d installed  \x07  %c:", s->cat.n, installed,
                 s->cat.drive);
    cv_text(right - cv_width(line), TAB_Y, line, G_TEXT2, -1);
}

/* One particular, on one line: the label in its column, the value beside
 * it.  A value longer than its line runs past like a marquee: it holds at
 * the start long enough to be read, then scrolls until its end has gone
 * by, with a gap before it comes round again.  Still under a fixed clock.
 * Returns the height it took. */
#define MARQUEE_HOLD_MS 1500
#define MARQUEE_PX_S 30
#define MARQUEE_GAP 40

static int particular(int y, const char *label, const char *value) {
    cv_text_small(RIGHT_X, y, label, G_TEXT2); /* the label smaller than what it labels */
    int x = RIGHT_X + LABEL_W, room = DETAIL_W - LABEL_W, tw = cv_width(value);
    if (tw <= room) {
        cv_text(x, y, value, G_TEXT, -1);
        return CV_LINE;
    }
    /* the value's own strip, inside the pane it scrolls in */
    int top = y > DETAIL_Y ? y : DETAIL_Y;
    int bottom = y + CV_LINE < DETAIL_Y + DETAIL_H ? y + CV_LINE : DETAIL_Y + DETAIL_H;
    if (bottom <= top)
        return CV_LINE; /* scrolled out of sight */
    int off = 0;
    if (!S.fixed_clock) {
        int period = tw + MARQUEE_GAP;
        Uint64 cycle = MARQUEE_HOLD_MS + (Uint64)period * 1000 / MARQUEE_PX_S;
        Uint64 t = SDL_GetTicks() % cycle;
        off = t < MARQUEE_HOLD_MS ? 0 : (int)((t - MARQUEE_HOLD_MS) * MARQUEE_PX_S / 1000);
    }
    cv_clip(x, top, room, bottom - top);
    cv_text(x - off, y, value, G_TEXT, -1);
    cv_text(x - off + tw + MARQUEE_GAP, y, value, G_TEXT, -1); /* coming round */
    cv_clip(RIGHT_X, DETAIL_Y, RIGHT_W, DETAIL_H);
    return CV_LINE;
}

static void draw_title(const cat_title *t) {
    gui_picture(RIGHT_X, PIC_Y, RIGHT_W, PIC_H, art_picture());
    if (!t) {
        cv_text(RIGHT_X, NAME_Y, "Nothing chosen.", G_TEXT2, -1);
        return;
    }
    if (t != S.dtitle) { /* another title: its details start at the top */
        S.dtitle = t;
        S.dscroll = 0;
    }

    /* Everything under the picture scrolls as one: drawn moved up by how
     * far it is scrolled, cut at the edges of its pane, and measured as it
     * goes so the scrolling knows where the end is. */
    cv_clip(RIGHT_X, DETAIL_Y, RIGHT_W, DETAIL_H);
    int y0 = NAME_Y - S.dscroll, y = y0;
    cv_text_bold(RIGHT_X, y, t->name, G_WHITE, -1);
    char line[200];
    snprintf(line, sizeof line, "%s%s%s, %d%s%s", t->creator, t->publisher[0] ? " / " : "",
             t->publisher[0] ? t->publisher : "", t->year, t->genre[0] ? " - " : "", t->genre);
    y += CV_LINE + 1;
    y += cv_text_wrap(RIGHT_X, y, DETAIL_W, line, G_TEXT2) * CV_LINE;

    /* the particulars, a label and a value a line */
    char v[160];
    y += 8;
    snprintf(v, sizeof v, "%s%s", t->multiplayer ? "Multiplayer" : "Single player",
             t->network ? ", over a network" : "");
    y += particular(y, "Players", v);
    if (t->video[0])
        y += particular(y, "Video", t->video);
    words(v, sizeof v, t->sound);
    if (v[0])
        y += particular(y, "Sound", v);
    words(v, sizeof v, t->controls);
    if (v[0])
        y += particular(y, "Controls", v);

    if (t->description[0]) {
        y += 8;
        y += cv_text_wrap(RIGHT_X, y, DETAIL_W, t->description, G_TEXT) * CV_LINE;
    }
    cv_noclip();
    S.dheight = y - y0 + 2;
    detail_scroll(0); /* keep it in range, now the height is known */

    /* the scrollbar, the same as the list's: the arrows at its ends step a
     * line, the trough either side of the thumb a page */
    int bx = RIGHT_X + RIGHT_W - SCROLL_W;
    float shown = S.dheight > DETAIL_H ? (float)DETAIL_H / (float)S.dheight : 1.0f;
    float at = S.dheight > DETAIL_H ? (float)S.dscroll / (float)(S.dheight - DETAIL_H) : 0.0f;
    gui_scrollbar(bx, DETAIL_Y, SCROLL_W, DETAIL_H, at, shown);
    int track = DETAIL_H - 2 * (SCROLL_W + 2), th = (int)((float)track * shown + 0.5f);
    if (th < 8)
        th = 8;
    if (th > track)
        th = track;
    int ty = DETAIL_Y + SCROLL_W + 2 + (int)((float)(track - th) * at + 0.5f);
    add_hit(bx, DETAIL_Y, SCROLL_W, SCROLL_W + 2, HIT_DETAIL_LINE_UP);
    add_hit(bx, DETAIL_Y + DETAIL_H - SCROLL_W - 2, SCROLL_W, SCROLL_W + 2, HIT_DETAIL_LINE_DOWN);
    add_hit(bx, DETAIL_Y + SCROLL_W + 2, SCROLL_W, ty - DETAIL_Y - SCROLL_W - 2, HIT_DETAIL_UP);
    add_hit(bx, ty + th, SCROLL_W, DETAIL_Y + DETAIL_H - SCROLL_W - 2 - ty - th, HIT_DETAIL_DOWN);
}

static void draw_foot(const shelf *s, const cat_title *t) {
    cv_rect(MARGIN, FOOT_RULE, SCR_W - 2 * MARGIN, 1, G_LINE);
    if (install_busy()) {
        int what = HIT_KEY + KEY_CANCEL;
        int w = gui_hint(MARGIN, FOOT_Y, "ESC", "Cancel", hovering(what), 0);
        add_hit(MARGIN, FOOT_Y - 2, w, CV_LINE + 4, what);
        return;
    }
    int present = t && install_present(s, t);
    struct {
        const char *key, *what;
        int id, off;
    } keys[] = {
        {"ENTER", present ? "Run" : "Install", KEY_MAIN, !t},
        {"F2", "Setup", KEY_SETUP, !(present && t->setup[0])},
        {"F3", "Prompt", KEY_PROMPT, !present},
        {"F4-F7", "Filters", KEY_FILTERS, 0},
        {"\x1B\x1A", "Catalogue", KEY_TAB, nshelves < 2},
        {"ESC", S.find[0] ? "Clear" : "Back", KEY_BACK, 0},
    };
    int pen = MARGIN;
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        int what = HIT_KEY + keys[i].id;
        int w = gui_hint(pen, FOOT_Y, keys[i].key, keys[i].what, hovering(what), keys[i].off);
        if (!keys[i].off)
            add_hit(pen, FOOT_Y - 2, w, CV_LINE + 4, what);
        pen += w + 22;
    }
}

/* The panel of more filters: over everything, the screen behind it veiled,
 * a row a filter with its value as a chip that a click cycles. */
static void draw_more(void) {
    S.nhit = 0; /* the panel is modal: nothing behind it answers */
    cv_scrim(0, 0, SCR_W, SCR_H, 2, 0);
    gui_panel(POP_X, POP_Y, POP_W, POP_H);
    cv_text_bold(POP_X + 16, POP_Y + 12, "More filters", G_WHITE, -1);
    int y = POP_Y + 38;
    for (int i = ROW_FILTERS; i < FILTERS; i++) {
        int on = S.more_row == i - ROW_FILTERS;
        if (on) { /* the cursor: the row's bar, as the list draws its chosen one */
            cv_rect(POP_X + 8, y - 2, POP_W - 16, CHIP_H + 4, G_BAR);
            cv_rect(POP_X + 8, y - 2, 3, CHIP_H + 4, G_GOLD);
        }
        cv_text(POP_X + 16, y + 2, FILTER[i].label, on ? G_WHITE : G_TEXT, -1);
        int w = 0;
        for (int k = 0; k < FILTER[i].n; k++) {
            int ww = cv_width_small(FILTER[i].opt[k]);
            w = ww > w ? ww : w;
        }
        w += 18;
        int what = HIT_POP + i;
        gui_chip(POP_VALUE_X, y, w, CHIP_H, NULL, FILTER[i].opt[S.filter[i]], S.filter[i] != 0,
                 hovering(what));
        add_hit(POP_VALUE_X, y, w, CHIP_H, what);
        y += POP_ROW;
    }
    int fy = POP_Y + POP_H - 26;
    cv_rect(POP_X + 12, fy - 8, POP_W - 24, 1, G_LINE);
    int pen = POP_X + 16;
    if (S.more_row == MORE_RESET) {
        int rw = gui_hint(0, -100, "Reset", "", 0, 0);
        cv_rect(pen - 8, fy - 2, rw + 16, CV_LINE + 4, G_BAR);
        cv_rect(pen - 8, fy - 2, 3, CV_LINE + 4, G_GOLD);
    }
    int w = gui_hint(pen, fy, "Reset", "", hovering(HIT_POP_RESET) || S.more_row == MORE_RESET,
                     !more_active());
    add_hit(pen, fy - 2, w, CV_LINE + 4, HIT_POP_RESET);
    w = gui_hint(0, -100, "ESC", "Close", 0, 0);
    pen = POP_X + POP_W - 16 - w;
    gui_hint(pen, fy, "ESC", "Close", hovering(HIT_POP_CLOSE), 0);
    add_hit(pen, fy - 2, w, CV_LINE + 4, HIT_POP_CLOSE);
    /* last, so everything on the panel is found before the panel itself */
    add_hit(POP_X, POP_Y, POP_W, POP_H, HIT_POP_PANEL);
}

/* The screen the machine puts up while it gets the catalogue ready: its
 * name, and a line that fills as the moment passes. */
static const uint8_t *loading(Uint64 since, int *w, int *h) {
    cv_clear(C_BLACK);
    gui_init();
    cv_fade(1.0f);
    const char *title = "DOS ex Machina", *sub = "CATALOGUE";
    int y = SCR_H / 2 - 28;
    cv_text_bold((SCR_W - cv_width(title)) / 2, y, title, G_WHITE, -1);
    cv_text((SCR_W - cv_width(sub)) / 2, y + 20, sub, G_GOLD, -1);
    int bw = 220, bx = (SCR_W - bw) / 2, by = y + 52;
    float t = (float)since / (float)LOAD_MS;
    cv_rect(bx, by, bw, 1, G_LINE);
    cv_rect(bx, by, (int)((float)bw * (t > 1.0f ? 1.0f : t)), 1, G_GOLD);
    *w = CANVAS_W;
    *h = CANVAS_H;
    return cv_rgb();
}

const uint8_t *catalog_render(int *w, int *h) {
    shelf *s = cur();
    const cat_title *t = picked();
    S.nhit = 0;
    cv_noclip();

    Uint64 now = SDL_GetTicks();
    if (!S.fixed_clock && S.open_t0 && now - S.open_t0 < LOAD_MS)
        return loading(now - S.open_t0, w, h);

    /* an install that ended: say so, and have DOS look at the drive again */
    {
        char err[160];
        int d = install_take_done(err, sizeof err);
        if (d > 0) {
            say("Installed.", 0);
            if (s)
                dosbox_catalog_rescan(s->cat.drive);
            refilter();
        } else if (d < 0) {
            char line[200];
            snprintf(line, sizeof line, "Not installed: %s.", err);
            say(line, 1);
        }
    }
    if (s)
        art_want(s, t); /* the picture for this frame */

    gui_backdrop();
    cv_palette(ART_FIRST, ART_COLOURS, art_palette());
    cv_fade(1.0f);

    /* the catalogues, as tabs on a rule */
    int pen = MARGIN;
    for (int i = 0; i < nshelves; i++) {
        int tw;
        gui_tab(pen, TAB_Y, shelves[i].cat.name, i == S.shelf, &tw);
        add_hit(pen - 4, TAB_Y - 2, tw + 8, CV_LINE + 6, HIT_TAB + i);
        pen += tw + 24;
    }
    cv_rect(MARGIN, RULE_Y, SCR_W - 2 * MARGIN, 1, G_LINE);
    if (!nshelves)
        cv_text(MARGIN, TAB_Y, "No catalogues beside the program.", G_RED, -1);
    draw_status(s);

    if (s) {
        draw_chips();
        /* the caret blinks, half a second on and half off, and is on again
         * the moment a key goes in, so it is never missing while typing */
        int blink = S.fixed_clock || (now - S.typed_t0) % 1060 < 530;
        gui_field(LEFT_X, FIND_Y, LEFT_W, FIND_H, S.find, "Type to find a title",
                  !install_busy() && !S.more && blink);
        draw_list(s);
        draw_title(t);
    }
    draw_foot(s, t);
    if (S.more)
        draw_more();

    /* the way out: down to black on the palette, and then the program ends */
    if (S.close_t0) {
        float u = (float)(now - S.close_t0) / (float)CLOSE_MS;
        cv_fade(1.0f - u);
        S.nhit = 0;
        if (u >= 1.0f)
            closed();
    } else if (!S.fixed_clock && now >= S.slide_t0 && now - S.slide_t0 < SLIDE_MS) {
        /* the arrival: the bands slide in, eased so they settle rather than
         * stop; the pointer is drawn once they have */
        float u = (float)(now - S.slide_t0) / (float)SLIDE_MS;
        cv_slide_bands(BANDS, u * u * (3.0f - 2.0f * u));
        S.nhit = 0; /* nothing is where it will be yet */
    } else if (!S.fixed_clock)
        cv_pointer(S.mx, S.my); /* not in a golden frame: a real mouse can move it */
    S.hover = hit_at(S.mx, S.my);
    *w = CANVAS_W;
    *h = CANVAS_H;
    return cv_rgb();
}
