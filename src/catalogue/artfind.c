/* artfind.c — see artfind.h. */
#include "artfind.h"
#include "internal.h"
#include "net.h"
#include "sha256.h"
#include "log.h"
#include "setup/internal.h"
#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The panel sits where the editor's and the browser's do, so that moving
 * between them does not move the box.  The list takes the left of it and
 * the picture the right, at the size it will be on the catalogue screen -
 * what is judged here is exactly what will be shown later. */
enum {
    AX = 34,
    AY = 26,
    AW = 572,
    AH = 348,
    A_HEAD = 40,
    A_ROW = 18,
    A_FOUND = 12, /* candidates kept: a page of them, and no scrolling */
    A_LIST_W = AW - 28 - ART_W - 14,
    A_LIST_Y = AY + A_HEAD + 18 + CV_LINE + 5,
    A_LIST_BOTTOM = AY + AH - 30 - 4,
    A_ID = 96,    /* an archive.org identifier */
    A_PATH = 1400 /* a file in the preferences, which is a host path */
};

/* what the pointer can land on: a row, or one of the keys */
enum { AHIT_ROW = 1, AHIT_OK = 2000, AHIT_FILE, AHIT_CANCEL, AHIT_PANEL };

typedef struct {
    char id[A_ID];
    char name[CAT_NAME];
    char year[8];
} found;

typedef struct {
    int x, y, w, h, what;
} ahit;

/* Everything the worker touches is under `mx`.  The screen takes a copy of
 * it once a frame rather than reading it piecemeal, so that a list cannot
 * change length between the loop that draws it and the row count above. */
static struct {
    int up, row, scroll;
    char query[CAT_NAME];
    char pref[1024];
    char shown[A_PATH]; /* what the picture box has in it (the screen's own) */
    ahit hit[24];
    int nhit;

    SDL_Mutex *mx;
    SDL_Thread *th;
    int pending;   /* a search has been asked for and not started */
    int searching; /* and one is under way */
    found e[A_FOUND];
    int n;
    char note[140]; /* why there is nothing, for the person to read */

    /* the picture: `want` is the screen's, the rest the worker's */
    char want[A_ID], have[A_ID];
    char path[A_PATH], url[CAT_URL], sha[65];
} A;

typedef struct {
    found e[A_FOUND];
    int n, searching;
    char note[140], path[A_PATH], have[A_ID];
} snap;

static void take_snap(snap *s) {
    SDL_LockMutex(A.mx);
    memcpy(s->e, A.e, sizeof s->e);
    s->n = A.n;
    s->searching = A.searching;
    snprintf(s->note, sizeof s->note, "%s", A.note);
    snprintf(s->path, sizeof s->path, "%s", A.path);
    snprintf(s->have, sizeof s->have, "%s", A.have);
    SDL_UnlockMutex(A.mx);
}

/* ---- just enough JSON -------------------------------------------------- */

/* The objects of an array, one at a time.  `at` starts anywhere before the
 * first brace and is moved past each object returned.  One longer than the
 * buffer is stepped over rather than cut down: half an object would be read
 * as a whole one, and quietly wrong is worse than missing. */
static int next_object(const char **at, char *out, size_t n) {
    for (;;) {
        const char *p = *at;
        while (*p && *p != '{' && *p != ']')
            p++;
        if (*p != '{')
            return 0;
        const char *start = p;
        int depth = 0, instr = 0;
        for (; *p; p++) {
            if (instr) {
                if (*p == '\\' && p[1])
                    p++;
                else if (*p == '"')
                    instr = 0;
                continue;
            }
            if (*p == '"')
                instr = 1;
            else if (*p == '{')
                depth++;
            else if (*p == '}' && --depth == 0) {
                p++;
                break;
            }
        }
        size_t len = (size_t)(p - start);
        *at = p;
        if (len < n) {
            memcpy(out, start, len);
            out[len] = 0;
            return 1;
        }
    }
}

/* The value of `key` in one object.  A number comes back as its digits,
 * which is all that is wanted: archive.org gives a year as a string in one
 * answer and as a number in another, and a field it holds more than one of
 * arrives wrapped in an array. */
static int jget(const char *obj, const char *key, char *out, size_t n) {
    char pat[40];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(obj, pat);
    if (!p)
        return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':')
        p++;
    if (*p == '[') /* the first of them is the one wanted */
        for (p++; *p == ' '; p++)
            ;
    size_t k = 0;
    if (*p == '"') {
        for (p++; *p && *p != '"' && k + 1 < n; p++) {
            char c = *p;
            if (c == '\\') {
                if (!*++p)
                    break;
                switch (*p) {
                case 'n':
                case 'r':
                case 't':
                    c = ' ';
                    break;
                case 'u': /* not worth decoding for something only ever read */
                    c = ' ';
                    for (int i = 0; i < 4 && p[1]; i++)
                        p++;
                    break;
                default:
                    c = *p;
                }
            }
            out[k++] = c;
        }
    } else
        for (; *p >= '0' && *p <= '9' && k + 1 < n; p++)
            out[k++] = *p;
    out[k] = 0;
    return k > 0;
}

/* ---- the addresses ----------------------------------------------------- */

/* A path separator is left alone: this builds whole addresses, and nothing
 * put through it carries a slash that is meant to be a character. */
static void url_add(char *out, size_t n, const char *s) {
    size_t k = strlen(out);
    for (; *s && k + 4 < n; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || strchr("-_.~/", c))
            out[k++] = (char)c;
        else
            k += (size_t)snprintf(out + k, n - k, "%%%02X", c);
    }
    out[k] = 0;
}

/* The name as something to search on.  archive.org reads its query as a
 * language, and a name that still has a release group's brackets on it
 * would be read as syntax, so only the letters and digits go in. */
static void search_term(const char *name, char *out, size_t n) {
    size_t k = 0;
    int gap = 0;
    for (const char *p = name; *p && k + 1 < n; p++) {
        unsigned char c = (unsigned char)*p;
        if (!isalnum(c)) {
            gap = 1;
            continue;
        }
        if (gap && k && k + 1 < n)
            out[k++] = ' ';
        gap = 0;
        if (k + 1 < n)
            out[k++] = (char)c;
    }
    out[k] = 0;
}

#define DOS_ONLY "collection:(softwarelibrary_msdos OR softwarelibrary_msdos_games)"

static void search_url(const char *name, char *out, size_t n) {
    char term[CAT_NAME], q[CAT_NAME + 200];
    search_term(name, term, sizeof term);
    snprintf(q, sizeof q, "title:(%s) AND mediatype:software AND " DOS_ONLY, term);
    snprintf(out, n, "https://archive.org/advancedsearch.php?q=");
    url_add(out, n, q);
    size_t k = strlen(out);
    snprintf(out + k, n - k,
             "&fl%%5B%%5D=identifier&fl%%5B%%5D=title&fl%%5B%%5D=year"
             "&rows=%d&page=1&output=json",
             A_FOUND);
}

/* Which file of an item to show.  An MS-DOS item carries a picture the
 * emulator took of the game running, tagged as exactly that, and it is the
 * one worth having: the numbered screenshots beside it are a mix of those
 * and scans of the box, in no order anyone could rely on.  Where there is
 * no such file the item's own tile is always there - smaller, but never
 * missing, and one request rather than two. */
static void picture_url(const char *id, char *out, size_t n) {
    char meta[200], err[160], best[1024] = "";
    char *text = NULL;
    size_t tn = 0;
    snprintf(meta, sizeof meta, "https://archive.org/metadata/%s", id);
    if (net_get_mem(meta, &text, &tn, err, sizeof err) == 0 && text) {
        const char *files = strstr(text, "\"files\"");
        const char *at = files ? files : text;
        char obj[2048], fmt[64], name[256];
        while (next_object(&at, obj, sizeof obj))
            if (jget(obj, "format", fmt, sizeof fmt) && !strcmp(fmt, "Emulator Screenshot") &&
                jget(obj, "name", name, sizeof name)) {
                snprintf(best, sizeof best, "https://archive.org/download/%s/", id);
                url_add(best, sizeof best, name);
                break;
            }
    }
    free(text);
    /* A catalogue holds CAT_URL characters of address and no more, so one
     * that would not fit in the file is no use however good the picture:
     * the tile always fits, and is always there. */
    if (best[0] && strlen(best) < n)
        snprintf(out, n, "%s", best);
    else
        snprintf(out, n, "https://archive.org/services/img/%s", id);
}

static const char *ext_of(const char *url) {
    const char *dot = strrchr(url, '.'), *slash = strrchr(url, '/');
    if (dot && (!slash || dot > slash) && strlen(dot) <= 5)
        return dot;
    return ".img";
}

static int exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

/* ---- the worker -------------------------------------------------------- */

static void do_search(const char *name) {
    char url[1200], err[160] = "";
    char *text = NULL;
    size_t tn = 0;
    search_url(name, url, sizeof url);
    int got = net_get_mem(url, &text, &tn, err, sizeof err);

    SDL_LockMutex(A.mx);
    A.n = 0;
    A.note[0] = 0;
    A.have[0] = A.path[0] = A.url[0] = A.sha[0] = 0;
    if (got != 0 || !text)
        snprintf(A.note, sizeof A.note, "archive.org did not answer");
    else {
        const char *docs = strstr(text, "\"docs\"");
        const char *at = docs ? docs : text;
        char obj[2048];
        while (A.n < A_FOUND && next_object(&at, obj, sizeof obj)) {
            found f;
            memset(&f, 0, sizeof f);
            if (!jget(obj, "identifier", f.id, sizeof f.id))
                continue;
            /* An item with no title of its own goes by its identifier, read
             * again into the shorter field rather than copied down into it. */
            if (!jget(obj, "title", f.name, sizeof f.name))
                jget(obj, "identifier", f.name, sizeof f.name);
            jget(obj, "year", f.year, sizeof f.year);
            A.e[A.n++] = f;
        }
        if (!A.n)
            snprintf(A.note, sizeof A.note, "no MS-DOS item by that name");
    }
    A.searching = 0;
    SDL_UnlockMutex(A.mx);
    if (got != 0)
        dxm_log("catalog: looking for artwork: %s", err);
    free(text);
}

static void do_picture(const char *id) {
    char url[CAT_URL], path[A_PATH], dir[1100], err[160] = "", sha[65] = "";
    picture_url(id, url, sizeof url);
    snprintf(dir, sizeof dir, "%sartwork/.found", A.pref);
    SDL_CreateDirectory(dir);
    snprintf(path, sizeof path, "%s/%s%s", dir, id, ext_of(url));
    int ok = exists(path) || net_get_file(url, path, NULL, NULL, NULL, err, sizeof err) == 0;
    if (ok)
        sha256_file(path, sha);
    else
        dxm_log("catalog: %s has no picture: %s", id, err);

    SDL_LockMutex(A.mx);
    snprintf(A.have, sizeof A.have, "%s", id);
    snprintf(A.path, sizeof A.path, "%s", ok ? path : "");
    snprintf(A.url, sizeof A.url, "%s", ok ? url : "");
    snprintf(A.sha, sizeof A.sha, "%s", sha);
    SDL_UnlockMutex(A.mx);
}

/* One thread, for as long as the panel is up: a search when one has been
 * asked for, then the picture belonging to whichever row the cursor is on.
 * It ends when the panel closes, and opening one starts it again - so there
 * is never a second thread writing the same table. */
static int SDLCALL worker(void *ud) {
    (void)ud;
    for (;;) {
        char name[CAT_NAME] = "", want[A_ID] = "";
        SDL_LockMutex(A.mx);
        if (!A.up) {
            A.th = NULL;
            SDL_UnlockMutex(A.mx);
            return 0;
        }
        if (A.pending) {
            A.pending = 0;
            snprintf(name, sizeof name, "%s", A.query);
        } else if (A.want[0] && strcmp(A.want, A.have))
            snprintf(want, sizeof want, "%s", A.want);
        SDL_UnlockMutex(A.mx);

        if (name[0])
            do_search(name);
        else if (want[0])
            do_picture(want);
        else
            SDL_Delay(30);
    }
}

/* ---- opening and closing ----------------------------------------------- */

int artfind_up(void) {
    return A.up;
}

/* `up` is set under the lock and the worker reads it there, in the same
 * breath as it gives up its own handle: that is what stops a close and an
 * open racing into either two threads or none. */
void artfind_close(void) {
    if (!A.mx)
        return;
    SDL_LockMutex(A.mx);
    A.up = 0;
    SDL_UnlockMutex(A.mx);
    A.shown[0] = 0;
    art_preview(NULL); /* the chosen title's own picture comes back */
}

void artfind_open(const char *name, const char *pref_dir) {
    if (!A.mx)
        A.mx = SDL_CreateMutex();
    A.row = A.scroll = 0;
    A.shown[0] = 0;
    snprintf(A.query, sizeof A.query, "%s", name ? name : "");
    snprintf(A.pref, sizeof A.pref, "%s", pref_dir ? pref_dir : "./");

    SDL_LockMutex(A.mx);
    A.up = 1;
    A.n = 0;
    A.note[0] = 0;
    A.pending = 1;
    A.searching = 1;
    A.want[0] = A.have[0] = A.path[0] = A.url[0] = A.sha[0] = 0;
    if (!A.th) {
        A.th = SDL_CreateThread(worker, "artfind", NULL);
        if (A.th)
            SDL_DetachThread(A.th);
    }
    SDL_UnlockMutex(A.mx);
    art_preview(NULL);
}

/* ---- what the cursor is on --------------------------------------------- */

/* The row's item is what the worker should be fetching, and what ENTER
 * takes.  Set from the screen, read by the worker. */
static void want_row(void) {
    SDL_LockMutex(A.mx);
    if (A.row >= 0 && A.row < A.n)
        snprintf(A.want, sizeof A.want, "%s", A.e[A.row].id);
    else
        A.want[0] = 0;
    SDL_UnlockMutex(A.mx);
}

/* Nothing is taken until its picture has actually arrived, which is also
 * the promise that nobody accepts one they have not seen. */
static void accept(void) {
    char url[CAT_URL] = "", sha[65] = "";
    SDL_LockMutex(A.mx);
    if (A.row >= 0 && A.row < A.n && !strcmp(A.have, A.e[A.row].id) && A.url[0]) {
        snprintf(url, sizeof url, "%s", A.url);
        snprintf(sha, sizeof sha, "%s", A.sha);
    }
    SDL_UnlockMutex(A.mx);
    if (!url[0])
        return;
    artfind_close();
    artfind_took(url, sha);
}

static void give_up(void) {
    artfind_close();
    artfind_gave_up();
}

/* Nothing here is it, and there is a file on this computer that is. */
static void go_to_file(void) {
    artfind_close();
    artfind_wants_file();
}

static int count(void) {
    SDL_LockMutex(A.mx);
    int n = A.n;
    SDL_UnlockMutex(A.mx);
    return n;
}

static void move_row(int by) {
    int n = count();
    if (n < 1)
        return;
    A.row += by;
    if (A.row < 0)
        A.row = 0;
    if (A.row >= n)
        A.row = n - 1;
    want_row();
}

void artfind_key(int sdl_scancode, int shift) {
    (void)shift;
    switch (sdl_scancode) {
    case SDL_SCANCODE_ESCAPE:
        give_up();
        return;
    case SDL_SCANCODE_UP:
        move_row(-1);
        return;
    case SDL_SCANCODE_DOWN:
    case SDL_SCANCODE_TAB:
        move_row(1);
        return;
    case SDL_SCANCODE_PAGEUP:
        move_row(-8);
        return;
    case SDL_SCANCODE_PAGEDOWN:
        move_row(8);
        return;
    case SDL_SCANCODE_HOME:
        move_row(-A_FOUND);
        return;
    case SDL_SCANCODE_END:
        move_row(A_FOUND);
        return;
    case SDL_SCANCODE_F3:
        go_to_file();
        return;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        accept();
        return;
    default:
        return;
    }
}

void artfind_click(int what) {
    if (what < 0)
        return;
    if (what == AHIT_CANCEL)
        give_up();
    else if (what == AHIT_FILE)
        go_to_file();
    else if (what == AHIT_OK)
        accept();
    else if (what >= AHIT_ROW && what < AHIT_ROW + A_FOUND) {
        int was = A.row;
        A.row = what - AHIT_ROW;
        want_row();
        if (was == A.row) /* a second click on the row takes it */
            accept();
    }
}

/* ---- the panel --------------------------------------------------------- */

static void add_hit(int x, int y, int w, int h, int what) {
    if (A.nhit < (int)(sizeof A.hit / sizeof A.hit[0]))
        A.hit[A.nhit++] = (ahit){x, y, w, h, what};
}

int artfind_hit(int x, int y) {
    for (int i = 0; i < A.nhit; i++)
        if (x >= A.hit[i].x && x < A.hit[i].x + A.hit[i].w && y >= A.hit[i].y &&
            y < A.hit[i].y + A.hit[i].h)
            return A.hit[i].what;
    return -1;
}

/* The picture box shows the row's picture the moment it has landed, and
 * nothing while it is on its way: a picture belonging to the row before
 * this one would be a lie about what is being offered. */
static void show_picture(const snap *s) {
    char path[A_PATH] = "";
    if (A.row >= 0 && A.row < s->n && !strcmp(s->have, s->e[A.row].id))
        snprintf(path, sizeof path, "%s", s->path);
    if (strcmp(path, A.shown)) {
        snprintf(A.shown, sizeof A.shown, "%s", path);
        art_preview(path[0] ? path : NULL);
    }
}

void artfind_draw(void) {
    enum { NAME_X = AX + 18, HEAD_Y = AY + A_HEAD + 18 };
    snap s;
    char line[CAT_NAME + 64];

    A.nhit = 0;
    take_snap(&s);
    if (A.row >= s.n)
        A.row = s.n - 1;
    if (A.row < 0)
        A.row = 0;
    want_row();
    show_picture(&s);

    int rows = (A_LIST_BOTTOM - A_LIST_Y) / A_ROW;
    if (A.row < A.scroll)
        A.scroll = A.row;
    if (A.row >= A.scroll + rows)
        A.scroll = A.row - rows + 1;
    if (A.scroll < 0)
        A.scroll = 0;

    gui_panel(AX, AY, AW, AH);
    snprintf(line, sizeof line, "A picture for %s", A.query);
    char head[CAT_NAME + 64];
    gui_fit(line, AW - 150, 0, head, sizeof head);
    cv_text_bold(AX + 16, AY + 12, head, G_WHITE, -1);
    cv_text_small(AX + AW - 16 - cv_width_small("ARCHIVE.ORG"), AY + 14, "ARCHIVE.ORG", G_TEXT2);

    cv_text_small(NAME_X, HEAD_Y, "WHAT IT FOUND", G_TEXT2);
    cv_rect(AX + 14, HEAD_Y + CV_LINE, A_LIST_W, 1, G_LINE);

    for (int i = A.scroll; i < s.n && i < A.scroll + rows; i++) {
        int y = A_LIST_Y + (i - A.scroll) * A_ROW, on = i == A.row;
        int yw = s.e[i].year[0] ? cv_width_small(s.e[i].year) + 12 : 0;
        if (on) {
            cv_rect(AX + 14, y - 1, A_LIST_W, A_ROW, G_BAR);
            cv_rect(AX + 14, y - 1, 3, A_ROW, G_GOLD);
        }
        gui_fit(s.e[i].name, A_LIST_W - 26 - yw, 0, line, sizeof line);
        cv_text(NAME_X, y, line, on ? G_WHITE : G_TEXT, -1);
        if (s.e[i].year[0])
            cv_text_small(AX + 14 + A_LIST_W - 8 - cv_width_small(s.e[i].year), y + 2, s.e[i].year,
                          on ? G_WHITE : G_TEXT2);
        add_hit(AX + 14, y - 1, A_LIST_W, A_ROW, AHIT_ROW + i);
    }
    if (!s.n) {
        const char *say = s.searching ? "looking on archive.org..."
                          : s.note[0] ? s.note
                                      : "nothing";
        gui_fit(say, A_LIST_W - 26, 0, line, sizeof line);
        cv_text(NAME_X, A_LIST_Y + 4, line, s.searching ? G_TEXT2 : G_RED, -1);
    }
    if (s.n > rows) {
        float shown = (float)rows / (float)s.n;
        float at = (float)A.scroll / (float)(s.n - rows);
        gui_scrollbar(AX + 14 + A_LIST_W - 14, A_LIST_Y - 2, 12, rows * A_ROW, at, shown);
    }

    /* the picture, at the size it will be on the catalogue screen */
    int px = AX + AW - 14 - ART_W, py = A_LIST_Y - 2;
    gui_picture(px, py, ART_W, ART_H, art_picture(), NULL);
    const found *on = (A.row >= 0 && A.row < s.n) ? &s.e[A.row] : NULL;
    int ready = on && !strcmp(s.have, on->id) && s.path[0];
    if (on) {
        gui_fit(on->id, ART_W, 1, line, sizeof line);
        cv_text_small(px, py + ART_H + 8, line, G_TEXT2);
        if (!ready)
            cv_text_small(px, py + ART_H + 10 + CV_LINE,
                          strcmp(s.have, on->id) ? "fetching it..." : "this one has no picture",
                          strcmp(s.have, on->id) ? G_TEXT2 : G_RED);
    }

    int fy = AY + AH - 26;
    cv_rect(AX + 12, fy - 8, AW - 24, 1, G_LINE);
    int w = gui_hint(AX + 16, fy, "ENTER", "Use this picture", 0, !ready);
    add_hit(AX + 16, fy - 2, w, CV_LINE + 4, AHIT_OK);
    int pen2 = AX + 16 + w + 20;
    w = gui_hint(pen2, fy, "F3", "A file on this computer", 0, 0);
    add_hit(pen2, fy - 2, w, CV_LINE + 4, AHIT_FILE);
    w = gui_hint(0, -100, "ESC", "Cancel", 0, 0);
    int pen = AX + AW - 16 - w;
    gui_hint(pen, fy, "ESC", "Cancel", 0, 0);
    add_hit(pen, fy - 2, w, CV_LINE + 4, AHIT_CANCEL);
    add_hit(AX, AY, AW, AH, AHIT_PANEL);
}
