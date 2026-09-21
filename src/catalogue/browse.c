/* browse.c — see browse.h. */
#include "browse.h"
#include "internal.h"
#include "dosname.h"
#include "setup/internal.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The panel sits where the editor's do, so that moving between them does
 * not move the box.  The list is between the folder's well and the rule
 * over the keys, and how many rows fit comes from that gap rather than the
 * other way round, so the last row cannot land on the rule. */
enum {
    BX = 34,
    BY = 26,
    BW = 572,
    BH = 348,
    B_HEAD = 40,
    B_FOOT = 30,
    B_ROW = 16,
    B_LIST_Y = BY + B_HEAD + 18 + CV_LINE + 5,
    B_LIST_BOTTOM = BY + BH - B_FOOT - 4,
    BROWSE_MAX = 1024,
    WALK_DEPTH = 3, /* how far under a folder its programs are looked for */
    B_PATH = 260    /* a path on this computer; a DOS one is far shorter */
};

/* what the pointer can land on: a row, or one of the keys */
enum { BHIT_ROW = 1, BHIT_OK = 2000, BHIT_UP, BHIT_FOLDER, BHIT_CANCEL, BHIT_PANEL };

/* Which side is being walked.  On a drive only folders and programs are
 * shown, because those are the only two things that can be chosen there:
 * walk into a folder, or take the program and the folder holding it.  A
 * list of programs is neither - it is flat, and every row is an answer. */
enum { B_HOST, B_DOS, B_PROGRAMS };

typedef struct {
    char name[176]; /* a name, or a program's place under the folder */
    int dir;
    long size;
} bentry;

typedef struct {
    int x, y, w, h, what;
} bhit;

static struct {
    int up, mode;
    browse_for what;
    char at[B_PATH]; /* the folder on show, ending in a separator */
    bentry e[BROWSE_MAX];
    int n, row, scroll;
    char find[24]; /* typed to jump; forgotten after a moment */
    Uint64 find_t0, click_t0;
    int click_row;
    char note[120]; /* when a folder will not open */
    bhit hit[48];
    int nhit;
} B;

int browse_open(void) {
    return B.up;
}

void browse_close(void) {
    B.up = 0;
}

/* Its own hit list: the editor has one for its panels, and this is over
 * them, so the two never want to answer the same click. */
static void add_hit(int x, int y, int w, int h, int what) {
    if (B.nhit < (int)(sizeof B.hit / sizeof B.hit[0]))
        B.hit[B.nhit++] = (bhit){x, y, w, h, what};
}

int browse_hit(int x, int y) {
    for (int i = 0; i < B.nhit; i++)
        if (x >= B.hit[i].x && x < B.hit[i].x + B.hit[i].w && y >= B.hit[i].y &&
            y < B.hit[i].y + B.hit[i].h)
            return B.hit[i].what;
    return -1;
}

/* ---- what is there ------------------------------------------------------ */

static char sep(void) {
    return B.mode == B_DOS ? '\\' : '/';
}

/* The folder on show, as a place on this computer.  On a drive that means
 * going through the letter it names. */
static int host_of(char *out, size_t n) {
    if (B.mode == B_DOS)
        return install_host_path(B.at, out, n);
    snprintf(out, n, "%s", B.at);
    return 1;
}

static void ensure_sep(char *p, size_t n) {
    size_t k = strlen(p);
    if (k && p[k - 1] != '/' && p[k - 1] != '\\' && k + 1 < n) {
        p[k] = sep();
        p[k + 1] = 0;
    }
}

/* a host path, ending in the separator SDL wants for enumerating it */
static void ensure_host_sep(char *p, size_t n) {
    size_t k = strlen(p);
    if (k && p[k - 1] != '/' && k + 1 < n) {
        p[k] = '/';
        p[k + 1] = 0;
    }
}

static SDL_EnumerationResult add_entry(void *ud, const char *dirname, const char *fname) {
    (void)ud;
    if (fname[0] == '.')
        return SDL_ENUM_CONTINUE; /* what the host hides, this hides */
    if (B.n >= BROWSE_MAX - 1)
        return SDL_ENUM_SUCCESS;
    char path[1400];
    snprintf(path, sizeof path, "%s%s", dirname, fname);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info))
        return SDL_ENUM_CONTINUE;
    int dir = info.type == SDL_PATHTYPE_DIRECTORY;
    /* On a drive there is no point showing what cannot be chosen: a folder
     * to walk into, or a program to start.  A data file is neither. */
    if (B.mode == B_DOS && !dir && !dos_is_program(fname))
        return SDL_ENUM_CONTINUE;
    bentry *e = &B.e[B.n++];
    snprintf(e->name, sizeof e->name, "%s", fname);
    e->dir = dir;
    e->size = (long)info.size;
    return SDL_ENUM_CONTINUE;
}

static int by_entry(const void *a, const void *b) {
    const bentry *x = a, *y = b;
    if (x->dir != y->dir)
        return y->dir - x->dir; /* folders first, as they always were */
    return SDL_strcasecmp(x->name, y->name);
}

/* Show `dir`, and put the cursor on `keep` if it is in there - which is how
 * coming back up leaves the eye on the folder just left. */
static void list_folder(const char *dir, const char *keep) {
    snprintf(B.at, sizeof B.at, "%s", dir);
    ensure_sep(B.at, sizeof B.at);
    B.n = 0;
    B.note[0] = 0;
    B.find[0] = 0;
    char host[1400];
    if (!host_of(host, sizeof host))
        snprintf(B.note, sizeof B.note, "that drive is not mounted");
    else if (!SDL_EnumerateDirectory(host, add_entry, NULL))
        snprintf(B.note, sizeof B.note, "this folder will not open");
    qsort(B.e, (size_t)B.n, sizeof B.e[0], by_entry);
    char up[B_PATH];
    if (dos_up(B.at, up, sizeof up)) { /* `..`, first, as the period put it */
        memmove(&B.e[1], &B.e[0], (size_t)B.n * sizeof B.e[0]);
        B.n++;
        snprintf(B.e[0].name, sizeof B.e[0].name, "..");
        B.e[0].dir = 1;
        B.e[0].size = 0;
    }
    B.row = 0;
    for (int i = 0; i < B.n; i++)
        if (keep && !strcmp(B.e[i].name, keep))
            B.row = i;
    B.scroll = 0;
    B.click_row = -1;
}

/* ---- the programs under a folder ---------------------------------------- */

typedef struct {
    program *out;
    int n, max, depth;
    char prefix[128]; /* how deep this walk is, as a DOS path */
} walk;

static SDL_EnumerationResult walk_cb(void *ud, const char *dirname, const char *fname);

static void walk_under(const char *host_dir, walk *w) {
    char at[1400];
    snprintf(at, sizeof at, "%s", host_dir);
    ensure_host_sep(at, sizeof at);
    SDL_EnumerateDirectory(at, walk_cb, w);
}

static SDL_EnumerationResult walk_cb(void *ud, const char *dirname, const char *fname) {
    walk *w = ud;
    if (fname[0] == '.' || w->n >= w->max)
        return SDL_ENUM_CONTINUE;
    char path[1400];
    snprintf(path, sizeof path, "%s%s", dirname, fname);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info))
        return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        if (w->depth >= WALK_DEPTH)
            return SDL_ENUM_CONTINUE;
        char was[128];
        snprintf(was, sizeof was, "%s", w->prefix);
        snprintf(w->prefix, sizeof w->prefix, "%s%s\\", was, fname);
        w->depth++;
        walk_under(path, w);
        w->depth--;
        snprintf(w->prefix, sizeof w->prefix, "%s", was);
        return SDL_ENUM_CONTINUE;
    }
    if (!dos_is_program(fname))
        return SDL_ENUM_CONTINUE;
    program *e = &w->out[w->n++];
    snprintf(e->rel, sizeof e->rel, "%s%s", w->prefix, fname);
    dos_upper(e->rel);
    e->size = (long)info.size;
    return SDL_ENUM_CONTINUE;
}

int browse_programs_under(const char *dos_dir, program *out, int max) {
    char host[1400];
    if (!install_host_path(dos_dir, host, sizeof host))
        return 0;
    walk w = {out, 0, max, 0, ""};
    walk_under(host, &w);
    return w.n;
}

void browse_split(const char *rel, char *dir, size_t dn, char *name, size_t nn) {
    const char *at = strrchr(rel, '\\');
    if (!at) {
        dir[0] = 0;
        snprintf(name, nn, "%s", rel);
        return;
    }
    snprintf(dir, dn, "%.*s", (int)(at - rel), rel);
    snprintf(name, nn, "%s", at + 1);
}

/* ---- opening it --------------------------------------------------------- */

static void open_at(int mode, browse_for what, const char *start) {
    B.mode = mode;
    B.what = what;
    B.up = 1;
    list_folder(start, NULL);
}

void browse_archive(const char *start) {
    /* where the field already points, if it points anywhere; otherwise the
     * folder a browser would have put the archive in */
    char at[B_PATH] = "";
    if (!start || !start[0] || !dos_up(start, at, sizeof at)) {
        const char *dl = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
        if (!dl)
            dl = SDL_GetUserFolder(SDL_FOLDER_HOME);
        snprintf(at, sizeof at, "%s", dl ? dl : "/");
    }
    open_at(B_HOST, BROWSE_ARCHIVE, at);
}

void browse_drive(const char *start, browse_for what) {
    char at[B_PATH];
    snprintf(at, sizeof at, "%s", start && start[0] ? start : "C:\\");
    open_at(B_DOS, what, at);
}

/* No walking about here: the whole tree is already in front of you, each
 * one shown where it sits, so picking the one in a subfolder says both what
 * to run and where it runs. */
void browse_programs(const char *dos_dir, browse_for what) {
    static program found[PROGRAMS_MAX];
    B.mode = B_PROGRAMS;
    B.what = what;
    B.up = 1;
    B.n = 0;
    B.note[0] = 0;
    B.find[0] = 0;
    snprintf(B.at, sizeof B.at, "%s", dos_dir);
    int n = browse_programs_under(dos_dir, found, PROGRAMS_MAX);
    for (int i = 0; i < n && B.n < BROWSE_MAX; i++) {
        /* a setup program is only useful beside the thing it sets up */
        if (what == BROWSE_SETUP && strchr(found[i].rel, '\\'))
            continue;
        bentry *e = &B.e[B.n++];
        snprintf(e->name, sizeof e->name, "%s", found[i].rel);
        e->dir = 0;
        e->size = found[i].size;
    }
    if (!B.n)
        snprintf(B.note, sizeof B.note, "no program in there");
    B.row = 0;
    B.scroll = 0;
    B.click_row = -1;
}

/* ---- choosing --------------------------------------------------------- */

static int rows_that_fit(void) {
    return (B_LIST_BOTTOM - B_LIST_Y) / B_ROW;
}

static void show_row(int row) {
    int rows = rows_that_fit();
    if (row < 0)
        row = 0;
    if (row >= B.n)
        row = B.n - 1;
    B.row = row < 0 ? 0 : row;
    if (B.row < B.scroll)
        B.scroll = B.row;
    if (B.row >= B.scroll + rows)
        B.scroll = B.row - rows + 1;
    if (B.scroll < 0)
        B.scroll = 0;
}

/* The folder on show is the one: taken by its own key, or by a program
 * being chosen inside it. */
static void take_folder(const char *program_chosen) {
    char at[B_PATH];
    snprintf(at, sizeof at, "%s", B.at);
    dos_trim_sep(at);
    B.up = 0;
    browse_took_folder(at, program_chosen, B.what);
}

/* ENTER: into a folder, or out with what was chosen. */
static void take_row(void) {
    if (B.row < 0 || B.row >= B.n)
        return;
    const bentry *e = &B.e[B.row];
    if (e->dir) {
        char to[B_PATH];
        if (!strcmp(e->name, "..")) {
            /* the name being left, so the cursor lands back on it */
            char here[B_PATH], was[176];
            snprintf(here, sizeof here, "%s", B.at);
            dos_trim_sep(here);
            snprintf(was, sizeof was, "%s", dos_leaf(here));
            if (dos_up(B.at, to, sizeof to))
                list_folder(to, was);
            return;
        }
        snprintf(to, sizeof to, "%s%s%c", B.at, e->name, sep());
        list_folder(to, NULL);
        return;
    }
    if (B.mode == B_PROGRAMS) {
        /* the folder it actually sits in, which is not always the one the
         * listing started from */
        char dir[176], name[80], where[B_PATH];
        browse_split(e->name, dir, sizeof dir, name, sizeof name);
        snprintf(where, sizeof where, "%s%s%s", B.at, dir[0] ? "\\" : "", dir);
        B.up = 0;
        browse_took_program(where, name, B.what);
        return;
    }
    if (B.mode == B_DOS) {
        /* a program is a way of saying "this folder, and that is the one to
         * start"; where it goes is not a question a program answers */
        if (B.what != BROWSE_DEST)
            take_folder(e->name);
        return;
    }
    char path[1400];
    snprintf(path, sizeof path, "%s%s", B.at, e->name);
    B.up = 0;
    browse_took_archive(path);
}

/* ---- the keys ----------------------------------------------------------- */

/* A letter does not type into anything here, so it jumps to the next name
 * that starts with what has been typed - the quick search every file
 * manager of the period had, forgotten after a second so the next letter
 * starts again. */
void browse_key(int sdl_scancode, int shift) {
    int rows = rows_that_fit();
    switch (sdl_scancode) {
    case SDL_SCANCODE_ESCAPE:
        B.up = 0;
        browse_gave_up(B.what);
        return;
    case SDL_SCANCODE_UP:
        show_row(B.row - 1);
        return;
    case SDL_SCANCODE_DOWN:
        show_row(B.row + 1);
        return;
    case SDL_SCANCODE_PAGEUP:
        show_row(B.row - rows);
        return;
    case SDL_SCANCODE_PAGEDOWN:
        show_row(B.row + rows);
        return;
    case SDL_SCANCODE_HOME:
        show_row(0);
        return;
    case SDL_SCANCODE_END:
        show_row(B.n - 1);
        return;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        take_row();
        return;
    case SDL_SCANCODE_F10:
        if (B.mode == B_DOS)
            take_folder(NULL);
        return;
    case SDL_SCANCODE_BACKSPACE:
    case SDL_SCANCODE_LEFT: {
        char up[B_PATH];
        if (B.mode != B_PROGRAMS && dos_up(B.at, up, sizeof up))
            list_folder(up, NULL);
        return;
    }
    default:
        break;
    }
    char c = gui_typed(sdl_scancode, shift);
    if (!c || c == ' ')
        return;
    Uint64 now = SDL_GetTicks();
    if (now - B.find_t0 > 1000)
        B.find[0] = 0;
    B.find_t0 = now;
    size_t k = strlen(B.find);
    if (k + 1 >= sizeof B.find)
        return;
    B.find[k] = c;
    B.find[k + 1] = 0;
    for (int i = 0; i < B.n; i++)
        if (!SDL_strncasecmp(B.e[i].name, B.find, k + 1)) {
            show_row(i);
            return;
        }
    B.find[k] = 0; /* nothing starts with that: keep what did match */
}

void browse_click(int what) {
    Uint64 now = SDL_GetTicks();
    if (what == BHIT_CANCEL) {
        B.up = 0;
        browse_gave_up(B.what);
    } else if (what == BHIT_OK) {
        take_row();
    } else if (what == BHIT_UP) {
        char up[B_PATH];
        if (B.mode != B_PROGRAMS && dos_up(B.at, up, sizeof up))
            list_folder(up, NULL);
    } else if (what == BHIT_FOLDER) {
        if (B.mode == B_DOS)
            take_folder(NULL);
    } else if (what >= BHIT_ROW && what < BHIT_OK) {
        int row = what - BHIT_ROW;
        int again = row == B.click_row && now - B.click_t0 < 400;
        show_row(row);
        B.click_row = row;
        B.click_t0 = now;
        if (again)
            take_row();
    }
}

/* ---- the panel ---------------------------------------------------------- */

static const char *heading(void) {
    switch (B.what) {
    case BROWSE_ARCHIVE:
        return "Find the archive";
    case BROWSE_DEST:
        return "Where to put it";
    case BROWSE_RUN:
        return "Which one does it run";
    case BROWSE_SETUP:
        return "Which one sets it up";
    default:
        return "Find the folder it is in";
    }
}

void browse_draw(void) {
    enum { NAME_X = BX + 18 };
    int rows = rows_that_fit(), head_y = BY + B_HEAD + 18;
    B.nhit = 0;
    gui_panel(BX, BY, BW, BH);
    cv_text_bold(BX + 16, BY + 12, heading(), G_WHITE, -1);
    /* what has been typed to jump, where a file manager put it */
    if (B.find[0] && SDL_GetTicks() - B.find_t0 < 1000) {
        char q[40];
        snprintf(q, sizeof q, "%s_", B.find);
        cv_text_small(BX + BW - 16 - cv_width_small(q), BY + 14, q, G_GOLD);
    }
    /* the folder on show, in a well of its own across the panel */
    char line[B_PATH + 8];
    gui_well(BX + 14, BY + B_HEAD - 8, BW - 28, 20);
    gui_fit(B.at, BW - 52, 1, line, sizeof line);
    cv_text(BX + 22, BY + B_HEAD - 6, line, G_TEXT, -1);

    cv_text_small(NAME_X, head_y, "NAME", G_TEXT2);
    cv_text_small(BX + BW - 38 - cv_width_small("SIZE"), head_y, "SIZE", G_TEXT2);
    cv_rect(BX + 14, head_y + CV_LINE, BW - 28, 1, G_LINE);

    for (int i = B.scroll; i < B.n && i < B.scroll + rows; i++) {
        const bentry *e = &B.e[i];
        int y = B_LIST_Y + (i - B.scroll) * B_ROW, on = i == B.row;
        if (on) {
            cv_rect(BX + 14, y - 1, BW - 44, B_ROW, G_BAR);
            cv_rect(BX + 14, y - 1, 3, B_ROW, G_GOLD);
        }
        gui_fit(e->name, BW - 62 - 86, 0, line, sizeof line);
        cv_text(NAME_X, y, line, e->dir ? G_WHITE : (on ? G_WHITE : G_TEXT), -1);
        /* <DIR> where a folder's size would be, as DOS itself printed it */
        char sz[24];
        if (e->dir)
            snprintf(sz, sizeof sz, "%s", !strcmp(e->name, "..") ? "UP--DIR" : "<DIR>");
        else
            snprintf(sz, sizeof sz, "%ld", e->size);
        cv_text_small(BX + BW - 38 - cv_width_small(sz), y + 2, sz,
                      e->dir ? G_TEXT2 : (on ? G_WHITE : G_TEXT2));
        add_hit(BX + 14, y - 1, BW - 44, B_ROW, BHIT_ROW + i);
    }
    if (!B.n)
        cv_text(NAME_X, B_LIST_Y + 4, B.note[0] ? B.note : "nothing in here",
                B.note[0] ? G_RED : G_TEXT2, -1);

    if (B.n > rows) {
        float shown = (float)rows / (float)B.n;
        float at = (float)B.scroll / (float)(B.n - rows);
        gui_scrollbar(BX + BW - 26, B_LIST_Y - 2, 12, rows * B_ROW, at, shown);
    }

    int fy = BY + BH - 26;
    cv_rect(BX + 12, fy - 8, BW - 24, 1, G_LINE);
    const bentry *on = (B.row >= 0 && B.row < B.n) ? &B.e[B.row] : NULL;
    int pen = BX + 16;
    int w = gui_hint(pen, fy, "ENTER", on && on->dir ? "Open" : "Choose", 0, !on);
    add_hit(pen, fy - 2, w, CV_LINE + 4, BHIT_OK);
    pen += w + 20;
    if (B.mode != B_PROGRAMS) { /* a list of programs is not somewhere to walk */
        w = gui_hint(pen, fy, "\x1B", "Up a folder", 0, 0);
        add_hit(pen, fy - 2, w, CV_LINE + 4, BHIT_UP);
        pen += w + 20;
    }
    if (B.mode == B_DOS) { /* the folder on show can be taken as it stands */
        w = gui_hint(pen, fy, "F10", "This folder", 0, 0);
        add_hit(pen, fy - 2, w, CV_LINE + 4, BHIT_FOLDER);
    }
    w = gui_hint(0, -100, "ESC", "Cancel", 0, 0);
    pen = BX + BW - 16 - w;
    gui_hint(pen, fy, "ESC", "Cancel", 0, 0);
    add_hit(pen, fy - 2, w, CV_LINE + 4, BHIT_CANCEL);
    add_hit(BX, BY, BW, BH, BHIT_PANEL);
}
