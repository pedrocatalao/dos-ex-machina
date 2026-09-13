/* screen.c — CATALOG: what it holds, what the keys do, and the screen it
 * draws (SPEC §8.3).
 *
 * Tabs for the catalogues, a Find field over a list of titles on the left,
 * the chosen title's picture and particulars on the right, and along the
 * foot the keys and what they do.  Typing goes to the Find field and the
 * list narrows as you type, so the actions are on function keys and the
 * mouse, and ENTER does the obvious thing to the chosen title.  RUN, SETUP
 * and PROMPT are errands the DOS runs while this screen waits out of sight;
 * it comes back as it was. */
#include "catalog.h"
#include "internal.h"
#include "setup/internal.h"
#include "dosbox.h"
#include "log.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* the layout, in the 640x400 */
enum {
    MARGIN = 24,
    HEAD_Y = 14,             /* the machine's name, and the count */
    TAB_Y = HEAD_Y + 28,     /* the catalogues */
    RULE_Y = TAB_Y + CV_LINE + 8,
    LEFT_X = MARGIN,
    LEFT_W = 246,
    FIND_Y = RULE_Y + 14,
    FIND_H = 24,
    LIST_Y = FIND_Y + FIND_H + 8,
    LIST_H = 216,
    ROW_H = 16,
    ROWS = (LIST_H - 4) / ROW_H,
    SCROLL_W = 12,
    STATUS_Y = LIST_Y + LIST_H + 8,
    RIGHT_X = LEFT_X + LEFT_W + 24,
    RIGHT_W = SCR_W - MARGIN - RIGHT_X,
    PIC_Y = FIND_Y,
    PIC_H = ART_BIG_H + 6,
    NAME_Y = PIC_Y + PIC_H + 10,
    SUB_Y = NAME_Y + CV_LINE + 1,
    PART_Y = SUB_Y + CV_LINE + 6,
    DESC_Y = PART_Y + CV_LINE + 6,
    FOOT_Y = SCR_H - 26,
    DESC_LINES = (FOOT_Y - 14 - DESC_Y) / CV_LINE,
    DOUBLE_MS = 400
};

/* Coming up: a moment of loading screen, then the screen arrives in bands
 * slid in from alternate sides.  Coming back from an errand, the bands
 * alone.  Under a fixed clock, neither. */
#define LOAD_MS 400
#define SLIDE_MS 450
#define BANDS 16

static shelf shelves[CAT_LIST];
static int nshelves;

static struct {
    int running, away; /* the program is up; the screen is out of sight */
    int shelf, pick;   /* which catalogue, which title (an index into `shown`) */
    int scroll;
    char find[32];
    int shown[CAT_TITLES], nshown; /* the titles that match, as indices */
    int mx, my, held, hover;
    int fixed_clock;
    Uint64 open_t0, slide_t0, click_t0; /* open_t0 is 0 when there was no loading */
    int click_row;
    char base[1024], pref[1024], c_drive[1024];
    /* what the pointer can land on this frame */
    struct hit {
        int x, y, w, h, what; /* 0..999 a row, 1000+ a tab, 2000+ a key, 3000+ scroll */
    } hit[48];
    int nhit;
    char note[160]; /* a line for the user: an install that failed, say */
    int note_bad;
    Uint64 note_t0;
} S = {.mx = SCR_W / 2, .my = SCR_H / 2, .hover = -1, .click_row = -1};

enum { HIT_TAB = 1000, HIT_KEY = 2000, HIT_SCROLL_UP = 3000, HIT_SCROLL_DOWN };
enum { KEY_MAIN, KEY_SETUP, KEY_PROMPT, KEY_TAB, KEY_BACK, KEY_CANCEL };

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

/* The list, then each catalogue in it; what is wrong with any of them goes
 * to the log, and the rest is read anyway. */
void catalog_load(void) {
    cat_list l;
    char path[1400];
    snprintf(path, sizeof path, "%scatalogues/catalogues.lst", S.base);
    nshelves = 0;
    if (cat_read_list(&l, path) < 0) {
        dxm_log("catalog: no list at %s", path);
        return;
    }
    for (int i = 0; i < l.n_notes; i++)
        dxm_log("catalog: list: %s", l.notes[i]);
    for (int i = 0; i < l.n && nshelves < CAT_LIST; i++) {
        shelf *s = &shelves[nshelves];
        s->entry = l.entries[i];
        snprintf(path, sizeof path, "%scatalogues/%s", S.base, s->entry.file);
        if (cat_read(&s->cat, path) < 0) {
            dxm_log("catalog: %s: cannot read %s", s->entry.id, path);
            continue;
        }
        for (int k = 0; k < s->cat.n_notes; k++)
            dxm_log("catalog: %s: %s", s->entry.id, s->cat.notes[k]);
        /* the folder that is its drive: C: is the machine's own */
        if (s->entry.drive == 'C')
            snprintf(s->mount, sizeof s->mount, "%s/", S.c_drive);
        else
            snprintf(s->mount, sizeof s->mount, "%scatalogues/%s/", S.pref, s->entry.id);
        SDL_CreateDirectory(s->mount);
        dxm_log("catalog: %s on %c: - %d titles", s->entry.name, s->entry.drive, s->cat.n);
        nshelves++;
    }
}

static shelf *cur(void) {
    return nshelves ? &shelves[S.shelf] : NULL;
}
static const cat_title *picked(void) {
    shelf *s = cur();
    return (s && S.nshown) ? &s->cat.titles[S.shown[S.pick]] : NULL;
}

/* the titles the Find field lets through, in catalogue order */
static int matches(const cat_title *t, const char *find) {
    if (!find[0])
        return 1;
    char hay[CAT_NAME + 80 + 2], needle[32];
    snprintf(hay, sizeof hay, "%s %s", t->name, t->creator);
    for (char *p = hay; *p; p++)
        *p = (char)tolower((unsigned char)*p);
    snprintf(needle, sizeof needle, "%s", find);
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
            if (matches(&s->cat.titles[i], S.find))
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

void catalog_open(void) {
    if (S.running)
        return;
    S.running = 1;
    S.away = 0;
    S.mx = SCR_W / 2;
    S.my = SCR_H / 2;
    S.open_t0 = SDL_GetTicks();
    S.slide_t0 = S.open_t0 + LOAD_MS;
    S.note[0] = 0;
    S.find[0] = 0;
    refilter();
    dxm_log("catalog: open");
}

void catalog_close(void) {
    if (!S.running)
        return;
    S.running = 0;
    S.away = 0;
    dxm_log("catalog: closed");
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
        dxm_log("catalog: back");
    }
}

static void say(const char *what, int bad) {
    snprintf(S.note, sizeof S.note, "%s", what);
    S.note_bad = bad;
    S.note_t0 = SDL_GetTicks();
}

/* ---- what the keys do -------------------------------------------------- */

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
    dosbox_catalog_run(s->entry.drive, dos, cmd, kind);
    S.away = 1;
    dxm_log("catalog: %s in %c:%s", kind ? "prompt" : cmd, s->entry.drive, dos);
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
    if (S.away)
        return;
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
    case SDL_SCANCODE_UP:
        move(-1);
        break;
    case SDL_SCANCODE_DOWN:
        move(1);
        break;
    case SDL_SCANCODE_PAGEUP:
        move(-ROWS);
        break;
    case SDL_SCANCODE_PAGEDOWN:
        move(ROWS);
        break;
    case SDL_SCANCODE_HOME:
        move(-S.nshown);
        break;
    case SDL_SCANCODE_END:
        move(S.nshown);
        break;
    case SDL_SCANCODE_TAB:
        go_shelf(S.shelf + (shift ? -1 : 1));
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
            refilter();
        }
        break;
    }
    }
}

void catalog_wheel(int by) {
    if (S.away || install_busy())
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
    if (!down || S.away)
        return;
    int h = hit_at(S.mx, S.my);
    if (h < 0)
        return;
    int what = S.hit[h].what;
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
    else if (what == HIT_KEY + KEY_TAB)
        go_shelf(S.shelf + 1);
    else if (what == HIT_KEY + KEY_BACK)
        catalog_key(SDL_SCANCODE_ESCAPE, 0);
    else if (what == HIT_SCROLL_UP)
        catalog_wheel(1);
    else if (what == HIT_SCROLL_DOWN)
        catalog_wheel(-1);
}

/* ---- the screen -------------------------------------------------------- */

static void add_hit(int x, int y, int w, int h, int what) {
    if (S.nhit < (int)(sizeof S.hit / sizeof S.hit[0]))
        S.hit[S.nhit++] = (struct hit){x, y, w, h, what};
}

static int hovering(int what) {
    return S.hover >= 0 && S.hover < S.nhit && S.hit[S.hover].what == what;
}

static void words(char *out, size_t n, const char (*list)[CAT_WORD], int max) {
    out[0] = 0;
    for (int i = 0; i < max && list[i][0]; i++) {
        size_t k = strlen(out);
        snprintf(out + k, n - k, "%s%s", i ? ", " : "", list[i]);
    }
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

static void draw_status(const shelf *s) {
    char status[80];
    float p = install_progress(status, sizeof status);
    if (p >= 0.0f) {
        cv_text(LEFT_X, STATUS_Y, status, G_WHITE, -1);
        gui_bar(LEFT_X, STATUS_Y + CV_LINE + 4, LEFT_W, 6, p);
        return;
    }
    if (S.note[0]) {
        cv_text_wrap_max(LEFT_X, STATUS_Y, LEFT_W, S.note, S.note_bad ? G_RED : G_GREEN, 2);
        if (SDL_GetTicks() - S.note_t0 > 8000)
            S.note[0] = 0;
        return;
    }
    int installed = 0;
    for (int i = 0; i < s->cat.n; i++)
        installed += install_present(s, &s->cat.titles[i]);
    char line[96];
    if (S.find[0])
        snprintf(line, sizeof line, "%d of %d titles match, %d installed", S.nshown, s->cat.n,
                 installed);
    else
        snprintf(line, sizeof line, "%d titles, %d installed, on %c:", s->cat.n, installed,
                 s->entry.drive);
    cv_text(LEFT_X, STATUS_Y, line, G_TEXT2, -1);
}

static void draw_title(const cat_title *t) {
    gui_picture(RIGHT_X, PIC_Y, RIGHT_W, PIC_H, art_big());
    if (!t) {
        cv_text(RIGHT_X, NAME_Y, "Nothing chosen.", G_TEXT2, -1);
        return;
    }
    cv_text_bold(RIGHT_X, NAME_Y, t->name, G_WHITE, -1);
    char line[200], v[64], snd[64], ctl[64];
    snprintf(line, sizeof line, "%s%s%s, %d%s%s", t->creator, t->publisher[0] ? " / " : "",
             t->publisher[0] ? t->publisher : "", t->year, t->genre[0] ? " - " : "", t->genre);
    cv_text(RIGHT_X, SUB_Y, line, G_TEXT, -1);
    /* the particulars, on one line: what it needs, what it takes, who plays */
    words(snd, sizeof snd, t->sound, CAT_WORDS);
    words(ctl, sizeof ctl, t->controls, CAT_WORDS);
    snprintf(v, sizeof v, " \x07 %s%s", t->multiplayer ? "more than one player" : "one player",
             t->network ? ", network" : "");
    snprintf(line, sizeof line, "%s%s%s%s%s%s", t->video[0] ? t->video : "", snd[0] ? " \x07 " : "",
             snd, ctl[0] ? " \x07 " : "", ctl, v);
    if (line[0] == ' ')
        memmove(line, line + 3, strlen(line + 3) + 1);
    cv_text(RIGHT_X, PART_Y, line, G_TEXT2, -1);
    if (t->description[0])
        cv_text_wrap_max(RIGHT_X, DESC_Y, RIGHT_W, t->description, G_TEXT, DESC_LINES);
}

static void draw_foot(const shelf *s, const cat_title *t) {
    cv_rect(MARGIN, FOOT_Y - 10, SCR_W - 2 * MARGIN, 1, G_LINE);
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
        {"TAB", "Catalogue", KEY_TAB, nshelves < 2},
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
                dosbox_catalog_rescan(s->entry.drive);
        } else if (d < 0) {
            char line[200];
            snprintf(line, sizeof line, "Not installed: %s.", err);
            say(line, 1);
        }
    }
    if (s)
        art_want(s, t, NULL, 0); /* the picture for this frame */

    gui_backdrop();
    cv_palette(ART_FIRST, ART_COLOURS, art_palette());
    cv_fade(1.0f);

    /* the head: the machine's name, and the count over on the right */
    int pen = MARGIN;
    pen += cv_text_bold(pen, HEAD_Y, "DOS ex Machina", G_WHITE, -1) + 8;
    cv_text(pen, HEAD_Y, "Catalogue", G_TEXT2, -1);

    /* the catalogues, as tabs on a rule */
    pen = MARGIN;
    for (int i = 0; i < nshelves; i++) {
        int tw;
        gui_tab(pen, TAB_Y, shelves[i].entry.name, i == S.shelf, &tw);
        add_hit(pen - 4, TAB_Y - 2, tw + 8, CV_LINE + 6, HIT_TAB + i);
        pen += tw + 24;
    }
    cv_rect(MARGIN, RULE_Y, SCR_W - 2 * MARGIN, 1, G_LINE);
    if (!nshelves)
        cv_text(MARGIN, TAB_Y, "No catalogues beside the program.", G_RED, -1);

    if (s) {
        gui_field(LEFT_X, FIND_Y, LEFT_W, FIND_H, S.find, "Type to find a title", !install_busy());
        draw_list(s);
        draw_status(s);
        draw_title(t);
    }
    draw_foot(s, t);

    /* the arrival: the bands slide in, eased so they settle rather than
     * stop; the pointer is drawn once they have */
    if (!S.fixed_clock && now >= S.slide_t0 && now - S.slide_t0 < SLIDE_MS) {
        float u = (float)(now - S.slide_t0) / (float)SLIDE_MS;
        cv_slide_bands(BANDS, u * u * (3.0f - 2.0f * u));
        S.nhit = 0; /* nothing is where it will be yet */
    } else
        cv_pointer(S.mx, S.my);
    S.hover = hit_at(S.mx, S.my);
    *w = CANVAS_W;
    *h = CANVAS_H;
    return cv_rgb();
}
