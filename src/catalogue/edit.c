/* edit.c — adding to a catalogue, changing it, and taking things out.
 *
 * A catalogue that came with the machine sits beside the program, where it
 * cannot be written: on a Mac the application bundle is signed over its own
 * contents and altering a file inside it stops the machine launching at
 * all.  So nothing here writes where it read from.  Editing a catalogue
 * writes a whole copy of it into the machine's own folder, and that copy is
 * what loads from then on (screen.c).  It is an ordinary .cat - the writer
 * puts out the same bytes a curator would have typed - so it can be opened
 * in a text editor, sent to somebody, or thrown away, and the one that
 * shipped is still underneath, which is what Reset puts back.
 *
 * Four panels, one at a time: a menu of what can be done, a form for a
 * title or for a catalogue, a browser for finding a file on this computer,
 * and the question asked before anything goes.  The form is a list of
 * fields and nothing else knows what is on it, so the same code puts up
 * both forms.
 *
 * Nothing is saved that the reader would not have accepted: the rules are
 * cat_title_fault's, the same ones a title from a file is held to, and the
 * form says which one is unmet rather than refusing silently. */
#include "catalog.h"
#include "internal.h"
#include "where.h"
#include "dosname.h"
#include "setup/internal.h"
#include "log.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* ---- the panels ------------------------------------------------------- */

enum {
    PANEL_NONE,
    PANEL_MENU,
    PANEL_TITLE,
    PANEL_SHELF,
    PANEL_CONFIRM,
    PANEL_BROWSE,
    PANEL_DEST, /* where to put it, asked before anything is fetched */
    PANEL_ADD   /* an archive, or something already here */
};

/* Adding a title to a disk catalogue, which is a sequence rather than a
 * form: the two ways in ask for things in a different order, and both end
 * with a folder and the program to start in it.
 *
 *   from an archive   pick it -> where it goes -> unpack
 *   already here      find the folder it is in
 *
 * and both then end the same way: the machine reads the folder, works out
 * what it can - the name, what to run, its setup program - and puts that on
 * the form to be corrected rather than asking for it.  Nothing is typed
 * that could be found out.
 *
 * The title is written only at the end, so an add that is given up on
 * leaves no half a title in the catalogue. */
enum {
    ADD_OFF,
    ADD_ZIP,  /* finding the archive */
    ADD_DEST, /* where it goes */
    ADD_WAIT, /* unpacking */
    ADD_HERE, /* finding the folder it is already in */
    ADD_FORM  /* what the machine made of it, for correcting */
};

/* the form, and the menu, laid out in the 640x400 */
enum {
    EX = 34,
    EY = 26,
    EW = 572,
    EH = 348,
    E_HEAD = 40, /* the name of the panel, over its rule */
    E_ROW = 21,  /* a field */
    E_LABEL = 116,
    E_FOOT = 30, /* the keys, under their rule */
    /* a row short of what would fit: the last line above the keys is kept
       for the fault, so it never lands on the bottom field */
    E_ROWS = (EH - E_HEAD - E_FOOT - CV_LINE - 14) / E_ROW,
    E_VALUE_X = EX + 16 + E_LABEL,
    E_VALUE_W = EW - 32 - E_LABEL - 14,

    MENU_W = 300,
    MENU_ROW = 22,
    MENU_X = (SCR_W - MENU_W) / 2,

    ASK_W = 400,
    ASK_H = 148,
    ASK_X = (SCR_W - ASK_W) / 2,
    ASK_Y = (SCR_H - ASK_H) / 2
};

/* ---- a field on a form ------------------------------------------------ */

typedef enum {
    FLD_TEXT,   /* typed into */
    FLD_NUM,    /* typed into, digits only */
    FLD_CHOICE, /* stepped through with left and right */
    FLD_NOTE,   /* something to read: no cursor stops on it */
    FLD_RULE    /* a line, to group what is above and below it */
} fkind;

typedef struct {
    const char *label;
    fkind kind;
    char *text; /* FLD_TEXT, FLD_NUM, FLD_NOTE */
    int size;
    int *pick; /* FLD_CHOICE */
    const char *const *opts;
    int nopts;
    int upper;  /* a DOS name: what is typed goes in upper case */
    int browse; /* ENTER opens the browser to fill it in */
    const char *hint;
} field;

#define FIELDS 28

/* The categories, as the form offers them: the reader's own list rather
 * than a copy of it, so the form cannot come to disagree with what a
 * catalogue is allowed to say. */
#define CATEGORY_OPT cat_categories
static int categories(void) {
    static int n;
    if (!n)
        while (cat_categories[n])
            n++;
    return n;
}
static const char *const VIDEO_OPT[] = {"not said", "CGA", "EGA", "VGA", "SVGA"};
static const char *const PLAYERS_OPT[] = {"One player", "More than one"};
static const char *const YESNO_OPT[] = {"No", "Yes"};
static const char *const HOLDS_OPT[] = {"Internet", "Disk"};

/* ---- what the editor is holding --------------------------------------- */

/* what the pointer can land on, this frame */
typedef struct {
    int x, y, w, h, what;
} ehit;

static struct {
    int panel;
    shelf *shelf; /* the catalogue being worked on */
    int menu_row;
    int adding; /* the form is for something that is not in the catalogue yet */
    int at;     /* PANEL_TITLE: which title it is, or -1 when adding */

    /* the form */
    field f[FIELDS];
    int nf, row, scroll;
    char fault[200]; /* why it will not save yet, or empty */

    /* a title, taken apart into the things a person types */
    cat_title work;
    char year[8], size[16], sound[96], controls[96];
    int category, video, players, network;
    char found[200]; /* what the machine can say about where a disk title is */

    /* a catalogue's own particulars */
    char shelf_name[CAT_NAME], shelf_id[CAT_ID], shelf_about[CAT_DESC];
    int holds;

    /* where a title is going, asked before it is fetched - or where it
     * already is, when the machine is being pointed at it rather than
     * putting it anywhere */
    char dest[WHERE_PATH];
    char dest_id[CAT_ID]; /* the folder the title itself gets, inside whatever is chosen */
    const cat_title *dest_title;
    int dest_point;

    /* an add in progress */
    int add_step;
    char add_zip[CAT_PATH];    /* the archive it is coming out of */
    char add_root[WHERE_PATH]; /* where it was put, or the folder picked */
    char add_dir[WHERE_PATH];  /* and where inside that the program runs */
    int add_made;              /* the machine unpacked it there */
    int add_row;               /* which way in, on the panel that asks */

    /* the question */
    int ask_what; /* what a yes does */
    char ask_line[200], ask_small[200];

    ehit hit[48];
    int nhit;
} E;

enum { ASK_TITLE = 1, ASK_SHELF, ASK_RESET };

/* what the pointer can land on: a row of whatever panel is up, or a key */
enum { HIT_ROW = 1, HIT_OK = 900, HIT_CANCEL, HIT_DELETE, HIT_FOLDER, HIT_PANEL };

/* The sequence an add runs through, declared here because the menu that
 * starts it comes long before the code that carries it out. */
static void add_start(void);
static void add_chose_zip(void);
static void add_chose_dir(void);
static void browse_folder(void);

/* A path without its trailing separator, leaning either way.  A drive's
 * root keeps its own - C: is not a folder and C:\\ is. */
static void trim_sep(char *p) {
    size_t k = strlen(p);
    while (k > 3 && (p[k - 1] == '/' || p[k - 1] == '\\'))
        p[--k] = 0;
}

/* In place, since every DOS name the editor writes down is upper case. */
static void upper_in_place(char *p) {
    for (; *p; p++)
        *p = (char)toupper((unsigned char)*p);
}

int edit_up(void) {
    return E.panel != PANEL_NONE;
}

static void add_hit(int x, int y, int w, int h, int what) {
    if (E.nhit < (int)(sizeof E.hit / sizeof E.hit[0]))
        E.hit[E.nhit++] = (ehit){x, y, w, h, what};
}

static int hit_at(int x, int y) {
    for (int i = 0; i < E.nhit; i++)
        if (x >= E.hit[i].x && x < E.hit[i].x + E.hit[i].w && y >= E.hit[i].y &&
            y < E.hit[i].y + E.hit[i].h)
            return E.hit[i].what;
    return -1;
}

/* ---- the form's fields ------------------------------------------------- */

static void f_text(const char *label, char *text, int size, int upper, const char *hint) {
    if (E.nf >= FIELDS)
        return;
    E.f[E.nf++] = (field){label, FLD_TEXT, text, size, NULL, NULL, 0, upper, 0, hint};
}
static void f_num(const char *label, char *text, int size, const char *hint) {
    if (E.nf >= FIELDS)
        return;
    E.f[E.nf++] = (field){label, FLD_NUM, text, size, NULL, NULL, 0, 0, 0, hint};
}
static void f_choice(const char *label, int *pick, const char *const *opts, int n) {
    if (E.nf >= FIELDS)
        return;
    E.f[E.nf++] = (field){label, FLD_CHOICE, NULL, 0, pick, opts, n, 0, 0, NULL};
}
static void f_note(const char *label, char *text) {
    if (E.nf >= FIELDS)
        return;
    E.f[E.nf++] = (field){label, FLD_NOTE, text, 0, NULL, NULL, 0, 0, 0, NULL};
}
static void f_rule(void) {
    if (E.nf >= FIELDS)
        return;
    E.f[E.nf++] = (field){NULL, FLD_RULE, NULL, 0, NULL, NULL, 0, 0, 0, NULL};
}

/* The form for a title.  What it asks for after the particulars is the
 * catalogue's business rather than the title's: an internet catalogue's
 * titles each name a download and the hash it must turn out to be, and a
 * disk catalogue's have nothing to name - they are already here, and where
 * is kept elsewhere. */
/* What the machine can say about a disk title: where it has it, and
 * whether that is still true. */
static void say_where(void) {
    if (!E.shelf || !E.work.id[0] || E.adding) {
        snprintf(E.found, sizeof E.found, "settled when the title is pointed at");
        return;
    }
    cat_title t = E.work;
    snprintf(t.category, sizeof t.category, "%s", CATEGORY_OPT[E.category]);
    const char *at = where_get(E.shelf->cat.id, t.category, t.id);
    if (!at)
        snprintf(E.found, sizeof E.found, "nowhere yet");
    else
        snprintf(E.found, sizeof E.found, "%s   %s", at,
                 install_present(E.shelf, &t) ? "\x07 it is there" : "- not there any more");
}

static void build_title_form(void) {
    E.nf = 0;
    say_where();
    f_text("Name", E.work.name, CAT_NAME, 0, "what it is called");
    f_text("Id", E.work.id, CAT_ID, 1, "its folder on the drive: up to 8, A-Z 0-9");
    f_choice("Category", &E.category, CATEGORY_OPT, categories());
    f_num("Year", E.year, sizeof E.year, "1980 to 2100");
    f_text("Creator", E.work.creator, 80, 0, "who made it");
    f_text("Publisher", E.work.publisher, 80, 0, "who put it out, if anyone else");
    f_text("Genre", E.work.genre, 32, 0, NULL);
    f_text("Version", E.work.version, 16, 0, NULL);
    f_rule();
    f_choice("Video", &E.video, VIDEO_OPT, 5);
    f_choice("Players", &E.players, PLAYERS_OPT, 2);
    f_choice("Network", &E.network, YESNO_OPT, 2);
    f_text("Sound", E.sound, (int)sizeof E.sound, 0, "adlib, sb, pcspeaker, mt32 - commas between");
    f_text("Controls", E.controls, (int)sizeof E.controls, 0, "keyboard, mouse, joystick");
    f_rule();
    /* Both can be typed, and both can be picked: ENTER on the row shows
     * what is actually in the folder, which beats remembering it. */
    int pickable = E.shelf && E.shelf->cat.holds == CAT_DISK && (E.add_root[0] || E.add_dir[0]);
    if (E.nf + 2 > FIELDS)
        return; /* the two rows below are written out, so they need the guard */
    E.f[E.nf] =
        (field){"Run",      FLD_TEXT,
                E.work.run, 64,
                NULL,       NULL,
                0,          0,
                pickable,   pickable ? "ENTER to pick it from the folder" : "the file DOS starts"};
    E.nf++;
    E.f[E.nf] = (field){"Setup",
                        FLD_TEXT,
                        E.work.setup,
                        64,
                        NULL,
                        NULL,
                        0,
                        0,
                        pickable,
                        pickable ? "ENTER to pick it, if it has one" : "its own setup program"};
    E.nf++;
    f_text("About", E.work.description, CAT_DESC, 0, "a few lines for the right-hand side");
    f_rule();
    /* Where a title is kept is not asked here.  For an internet catalogue
     * it is asked at the moment it is installed, and for a disk one it was
     * settled when the title was pointed at; either way it is a fact about
     * this machine and lives in installed.cfg, not in the catalogue. */
    if (E.shelf && E.shelf->cat.holds == CAT_INTERNET) {
        f_text("Address", E.work.download.url, CAT_URL, 0, "http:// or https://");
        f_text("Sha256", E.work.download.sha256, 65, 0,
               "64 hex digits: what it must turn out to be");
        f_num("Bytes", E.size, sizeof E.size, "the size of the download, exactly");
        f_text("Inside", E.work.archive, 64, 0,
               "an archive within the archive, if it is an installer");
    } else {
        f_note("Where it is", E.found);
    }
    f_text("Artwork", E.work.artwork.url, CAT_URL, 0, "a picture for the right-hand side");
}

static void build_shelf_form(void) {
    E.nf = 0;
    f_text("Name", E.shelf_name, CAT_NAME, 0, "what goes on its tab");
    f_text("Id", E.shelf_id, CAT_ID, 1,
           "up to 8, A-Z 0-9: names its file, and the folder\n"
           "its titles install under");
    f_choice("Titles come from", &E.holds, HOLDS_OPT, 2);
    f_note("", (char *)(E.holds == CAT_DISK
                            ? "things already on this computer; where each one is stays here"
                            : "each one names an address and the hash it must turn out to be"));
    f_text("About", E.shelf_about, CAT_DESC, 0, "a line about what is in it");
}

/* ---- opening a form ---------------------------------------------------- */

static int index_of(const char *const *opts, int n, const char *word) {
    for (int i = 0; i < n; i++)
        if (!strcmp(opts[i], word))
            return i;
    return 0;
}

static void words_out(char *out, int n, const char (*list)[CAT_WORD]) {
    out[0] = 0;
    for (int i = 0; i < CAT_WORDS && list[i][0]; i++) {
        int k = (int)strlen(out);
        snprintf(out + k, (size_t)(n - k), "%s%s", i ? ", " : "", list[i]);
    }
}

/* the other way: a line of words a person typed, back into the table */
static void words_in(const char *in, char (*list)[CAT_WORD]) {
    memset(list, 0, (size_t)CAT_WORDS * CAT_WORD);
    int at = 0;
    for (const char *p = in; *p && at < CAT_WORDS;) {
        while (*p == ' ' || *p == ',')
            p++;
        int k = 0;
        while (*p && *p != ',' && k < CAT_WORD - 1)
            list[at][k++] = (char)tolower((unsigned char)*p++);
        while (k > 0 && list[at][k - 1] == ' ')
            k--; /* a space before the comma is not part of the word */
        list[at][k] = 0;
        while (*p && *p != ',')
            p++;
        if (k)
            at++;
    }
}

static void open_title_form(int adding, int at) {
    const cat_title *from = (!adding && E.shelf && at >= 0) ? &E.shelf->cat.titles[at] : NULL;
    memset(&E.work, 0, sizeof E.work);
    if (from)
        E.work = *from;
    else {
        E.work.multiplayer = 0;
        E.work.network = 0;
        snprintf(E.work.category, sizeof E.work.category, "GAMES");
        E.work.year = 1990;
    }
    snprintf(E.year, sizeof E.year, "%d", E.work.year);
    if (E.work.download.size > 0)
        snprintf(E.size, sizeof E.size, "%ld", E.work.download.size);
    else
        E.size[0] = 0;
    words_out(E.sound, (int)sizeof E.sound, E.work.sound);
    words_out(E.controls, (int)sizeof E.controls, E.work.controls);
    E.category = index_of(CATEGORY_OPT, categories(), E.work.category);
    E.video = E.work.video[0] ? index_of(VIDEO_OPT, 5, E.work.video) : 0;
    E.players = E.work.multiplayer > 0;
    E.network = E.work.network > 0;
    E.adding = adding;
    E.at = adding ? -1 : at;
    E.row = 0;
    E.scroll = 0;
    E.fault[0] = 0;
    E.panel = PANEL_TITLE;
    build_title_form();
}

static void open_shelf_form(void) {
    E.shelf_name[0] = 0;
    E.shelf_id[0] = 0;
    E.shelf_about[0] = 0;
    E.holds = CAT_DISK; /* what somebody making one by hand almost always wants */
    E.adding = 1;
    E.row = 0;
    E.scroll = 0;
    E.fault[0] = 0;
    E.panel = PANEL_SHELF;
    build_shelf_form();
}

void edit_open(shelf *s) {
    E.shelf = s;
    E.panel = PANEL_MENU;
    E.menu_row = 0;
    E.fault[0] = 0;
}

/* ---- saving ------------------------------------------------------------ */

/* Everything this machine writes goes to its own folder, whatever the
 * catalogue was read from. */
static int save_shelf(const shelf *s, const char *what_changed) {
    if (cat_write(&s->cat, s->my_path) != 0) {
        snprintf(E.fault, sizeof E.fault, "cannot write %s", s->my_path);
        dxm_log("catalog: cannot write %s", s->my_path);
        return 0;
    }
    dxm_log("catalog: %s in %s, written to %s", what_changed, s->cat.id, s->my_path);
    return 1;
}

static void save_title(void) {
    shelf *s = E.shelf;
    if (!s)
        return;
    cat_title t = E.work;
    snprintf(t.category, sizeof t.category, "%s", CATEGORY_OPT[E.category]);
    t.year = atoi(E.year);
    t.multiplayer = E.players;
    t.network = E.network;
    snprintf(t.video, sizeof t.video, "%s", E.video ? VIDEO_OPT[E.video] : "");
    t.download.size = atol(E.size);
    words_in(E.sound, t.sound);
    words_in(E.controls, t.controls);
    upper_in_place(t.id);
    /* A disk catalogue holds none of this, and a field typed and then
     * thought better of does not stay in the file to confuse somebody. */
    if (s->cat.holds == CAT_DISK) {
        memset(&t.download, 0, sizeof t.download);
        t.archive[0] = 0;
    }
    const char *why = cat_title_fault(&t, s->cat.holds);
    if (why) {
        snprintf(E.fault, sizeof E.fault, "%s", why);
        return;
    }
    for (int i = 0; i < s->cat.n; i++)
        if (i != E.at && !strcmp(s->cat.titles[i].id, t.id) &&
            !strcmp(s->cat.titles[i].category, t.category)) {
            snprintf(E.fault, sizeof E.fault, "%s already has a %s called %s", t.category, "title",
                     t.id);
            return;
        }
    if (E.adding && s->cat.n >= CAT_TITLES) {
        snprintf(E.fault, sizeof E.fault, "this catalogue is full at %d titles", CAT_TITLES);
        return;
    }
    /* An add that came in by one of the two ways has a folder for it, and
     * that is written down as the title goes in: the catalogue says what it
     * is, the machine says where. */
    if (!E.adding && (E.at < 0 || E.at >= s->cat.n)) {
        snprintf(E.fault, sizeof E.fault, "that title is not in this catalogue any more");
        return;
    }
    int at = E.adding ? s->cat.n++ : E.at;
    s->cat.titles[at] = t;
    if (E.add_dir[0])
        where_set(s->cat.id, t.category, t.id, E.add_dir, E.add_made);
    if (!save_shelf(s, E.adding ? "a title added" : "a title changed")) {
        /* the table here now says something the disk does not: throw it
         * away and read the files again, rather than work on from it */
        char failed[200];
        snprintf(failed, sizeof failed, "%s", E.fault);
        E.panel = PANEL_NONE;
        cat_reload(NULL, NULL, failed, 1);
        return;
    }
    char keep_shelf[CAT_ID], keep_title[CAT_ID], note[120];
    snprintf(keep_shelf, sizeof keep_shelf, "%s", s->cat.id);
    snprintf(keep_title, sizeof keep_title, "%s", t.id);
    snprintf(note, sizeof note, "%s.", E.adding ? "Added" : "Saved");
    E.add_step = ADD_OFF;
    E.add_dir[0] = 0;
    E.panel = PANEL_NONE;
    cat_reload(keep_shelf, keep_title, note, 0);
}

static void save_new_shelf(void) {
    static cat_catalogue c; /* too big for the stack, and wanted only here */
    memset(&c, 0, sizeof c);
    c.format = 1;
    snprintf(c.id, sizeof c.id, "%s", E.shelf_id);
    upper_in_place(c.id);
    snprintf(c.name, sizeof c.name, "%s", E.shelf_name);
    snprintf(c.about, sizeof c.about, "%s", E.shelf_about);
    c.holds = E.holds;
    c.community = 1; /* it did not come with the machine */
    c.n = 0;
    const char *why = cat_catalogue_fault(&c);
    if (why) {
        snprintf(E.fault, sizeof E.fault, "%s", why);
        return;
    }
    int n = 0;
    const shelf *all = cat_shelves(&n);
    for (int i = 0; i < n; i++) {
        if (!strcmp(all[i].cat.id, c.id)) {
            snprintf(E.fault, sizeof E.fault, "there is already a catalogue called %s", c.id);
            return;
        }
    }
    if (n >= CAT_LIST) {
        snprintf(E.fault, sizeof E.fault, "the machine holds %d catalogues at once", CAT_LIST);
        return;
    }
    char path[1200], dir[1100];
    snprintf(dir, sizeof dir, "%s", cat_my_dir());
    snprintf(path, sizeof path, "%s%s.cat", dir, c.id);
    if (cat_write(&c, path) != 0) {
        snprintf(E.fault, sizeof E.fault, "cannot write %s", path);
        return;
    }
    dxm_log("catalog: new catalogue %s at %s", c.id, path);
    char keep[CAT_ID];
    snprintf(keep, sizeof keep, "%s", c.id);
    E.panel = PANEL_NONE;
    cat_reload(keep, NULL, "Catalogue made.", 0);
}

/* ---- taking something out ---------------------------------------------- */

static void ask(int what, const char *line, const char *small) {
    E.ask_what = what;
    snprintf(E.ask_line, sizeof E.ask_line, "%s", line);
    snprintf(E.ask_small, sizeof E.ask_small, "%s", small);
    E.panel = PANEL_CONFIRM;
}

static void ask_delete_title(int at) {
    shelf *s = E.shelf;
    if (!s || at < 0 || at >= s->cat.n)
        return;
    E.at = at;
    char line[200];
    snprintf(line, sizeof line, "Take %s out of %s?", s->cat.titles[at].name, s->cat.name);
    ask(ASK_TITLE, line,
        "It goes from the catalogue only. Whatever is on the drive stays where it is.");
}

static void do_delete_title(void) {
    shelf *s = E.shelf;
    if (!s || E.at < 0 || E.at >= s->cat.n)
        return;
    char gone[CAT_NAME];
    snprintf(gone, sizeof gone, "%s", s->cat.titles[E.at].name);
    /* the list loses it; what it installed stays exactly where it is, and
     * only the note of where that was goes */
    where_forget(s->cat.id, s->cat.titles[E.at].category, s->cat.titles[E.at].id);
    for (int i = E.at; i < s->cat.n - 1; i++)
        s->cat.titles[i] = s->cat.titles[i + 1];
    s->cat.n--;
    if (!save_shelf(s, "a title taken out")) {
        char why[200];
        snprintf(why, sizeof why, "%s", E.fault);
        E.panel = PANEL_NONE;
        cat_reload(NULL, NULL, why, 1);
        return;
    }
    char keep[CAT_ID], note[160];
    snprintf(keep, sizeof keep, "%s", s->cat.id);
    snprintf(note, sizeof note, "%s taken out.", gone);
    E.panel = PANEL_NONE;
    cat_reload(keep, NULL, note, 0);
}

static void do_delete_shelf(void) {
    shelf *s = E.shelf;
    if (!s || s->shipped)
        return; /* one that came with the machine is reset, never deleted */
    char note[160];
    snprintf(note, sizeof note, "%s deleted.", s->cat.name);
    dxm_log("catalog: deleting the catalogue %s - %s", s->cat.id, s->my_path);
    where_forget_catalogue(s->cat.id); /* nothing left keyed to what is gone */
    remove(s->my_path);
    E.panel = PANEL_NONE;
    cat_reload(NULL, NULL, note, 0);
}

static void do_reset_shelf(void) {
    shelf *s = E.shelf;
    if (!s || !s->shipped || !s->edited)
        return;
    char keep[CAT_ID], note[160];
    snprintf(keep, sizeof keep, "%s", s->cat.id);
    snprintf(note, sizeof note, "%s is back to the one that shipped.", s->cat.name);
    dxm_log("catalog: resetting %s - dropping %s", s->cat.id, s->my_path);
    remove(s->my_path);
    E.panel = PANEL_NONE;
    cat_reload(keep, NULL, note, 0);
}

/* ---- the menu ----------------------------------------------------------- */

enum {
    M_NEW,
    M_EDIT,
    M_POINT,
    M_DELETE,
    M_RULE1,
    M_NEW_SHELF,
    M_RESET_SHELF,
    M_DELETE_SHELF,
    MENU_ROWS
};

/* Why a row cannot be taken, or NULL when it can. */
static const char *menu_off(int row) {
    const shelf *s = E.shelf;
    switch (row) {
    /* These three act on the title under the cursor, and there may not be
     * one: a catalogue with titles in it still shows none while a filter
     * or a search is hiding them all. */
    case M_EDIT:
    case M_POINT:
    case M_DELETE:
        if (!s || !s->cat.n)
            return "nothing in this catalogue yet";
        return cat_picked_index() >= 0 ? NULL : "no title is picked";
    case M_RESET_SHELF:
        return (s && s->shipped && s->edited) ? NULL : "it has not been changed here";
    case M_DELETE_SHELF:
        return (s && !s->shipped) ? NULL : "this one came with the machine";
    default:
        return NULL;
    }
}

static const char *menu_label(int row) {
    switch (row) {
    case M_NEW:
        return "Add a title";
    case M_EDIT:
        return "Change this title";
    case M_POINT:
        return "Say where this title is";
    case M_DELETE:
        return "Take this title out";
    case M_NEW_SHELF:
        return "Make a catalogue";
    case M_RESET_SHELF:
        return "Reset this catalogue";
    case M_DELETE_SHELF:
        return "Delete this catalogue";
    default:
        return "";
    }
}

/* which title the menu works on: the one the cursor is on out on the screen */
static int menu_target(void) {
    return cat_picked_index();
}

static void menu_take(int row) {
    shelf *s = E.shelf;
    if (!s || menu_off(row))
        return;
    switch (row) {
    case M_NEW:
        /* An internet catalogue's title is typed out; one on a drive is
         * found, and the machine asks which way round that is. */
        if (s->cat.holds == CAT_DISK)
            add_start();
        else {
            E.add_step = ADD_OFF;
            open_title_form(1, -1);
        }
        break;
    case M_EDIT:
        open_title_form(0, menu_target());
        break;
    case M_POINT: {
        int at = menu_target();
        if (at >= 0)
            edit_point(s, &s->cat.titles[at]);
        break;
    }
    case M_DELETE:
        ask_delete_title(menu_target());
        break;
    case M_NEW_SHELF:
        open_shelf_form();
        break;
    case M_RESET_SHELF: {
        char line[200];
        snprintf(line, sizeof line, "Put %s back as it shipped?", s->cat.name);
        ask(ASK_RESET, line,
            "Everything added or changed here is dropped. What is on the drive stays.");
        break;
    }
    case M_DELETE_SHELF: {
        char line[200];
        snprintf(line, sizeof line, "Delete the catalogue %s?", s->cat.name);
        ask(ASK_SHELF, line, "Its file goes. Whatever it installed stays where it is.");
        break;
    }
    default:
        break;
    }
}

/* ---- typing ------------------------------------------------------------- */

/* What a key puts in a field.  The scancode is where the key sits, not what
 * is printed on it, so a board that is not American gives its own marks for
 * the punctuation - the same bargain the Find field already makes. */
static char typed(int sc, int shift) {
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
        return (char)((shift ? 'A' : 'a') + (sc - SDL_SCANCODE_A));
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
        static const char UPPER[] = "!@#$%^&*(";
        return shift ? UPPER[sc - SDL_SCANCODE_1] : (char)('1' + (sc - SDL_SCANCODE_1));
    }
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9)
        return (char)('1' + (sc - SDL_SCANCODE_KP_1));
    switch (sc) {
    case SDL_SCANCODE_0:
        return shift ? ')' : '0';
    case SDL_SCANCODE_KP_0:
        return '0';
    case SDL_SCANCODE_SPACE:
        return ' ';
    case SDL_SCANCODE_MINUS:
        return shift ? '_' : '-';
    case SDL_SCANCODE_EQUALS:
        return shift ? '+' : '=';
    case SDL_SCANCODE_PERIOD:
    case SDL_SCANCODE_KP_PERIOD:
        return shift ? '>' : '.';
    case SDL_SCANCODE_COMMA:
        return shift ? '<' : ',';
    case SDL_SCANCODE_SLASH:
    case SDL_SCANCODE_KP_DIVIDE:
        return shift ? '?' : '/';
    case SDL_SCANCODE_BACKSLASH:
        return shift ? '|' : '\\';
    case SDL_SCANCODE_SEMICOLON:
        return shift ? ':' : ';';
    case SDL_SCANCODE_APOSTROPHE:
        return shift ? '"' : '\'';
    case SDL_SCANCODE_LEFTBRACKET:
        return shift ? '{' : '[';
    case SDL_SCANCODE_RIGHTBRACKET:
        return shift ? '}' : ']';
    case SDL_SCANCODE_GRAVE:
        return shift ? '~' : '`';
    default:
        return 0;
    }
}

static void type_into(field *f, char c) {
    if (!f->text || f->kind == FLD_NOTE)
        return;
    if (f->kind == FLD_NUM && !isdigit((unsigned char)c))
        return;
    if (f->upper)
        c = (char)toupper((unsigned char)c);
    size_t n = strlen(f->text);
    if ((int)n + 1 < f->size) {
        f->text[n] = c;
        f->text[n + 1] = 0;
    }
    E.fault[0] = 0; /* what was wrong may not be any more */
}

/* What DOS will start: the three it can, whatever case they are in. */
static int is_exe(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot && (!SDL_strcasecmp(dot, ".exe") || !SDL_strcasecmp(dot, ".com") ||
                   !SDL_strcasecmp(dot, ".bat"));
}

/* a host path, ending in the separator SDL wants for enumerating it */
static void ensure_host_sep(char *p, size_t n) {
    size_t k = strlen(p);
    if (k && p[k - 1] != '/' && k + 1 < n) {
        p[k] = '/';
        p[k + 1] = 0;
    }
}

/* The name of the last part of a path, whichever way its separators lean. */
static const char *leaf(const char *path) {
    /* Either may be absent, and comparing two pointers that are not into
     * the same object is not something C defines. */
    const char *slash = strrchr(path, '/'), *back = strrchr(path, '\\');
    const char *at = (back && (!slash || back > slash)) ? back : slash;
    return at ? at + 1 : path;
}

/* Every program under a folder, with where it sits relative to that folder
 * - the whole tree of it, not just the top, because an archive that brought
 * its own folders keeps them and the program is then one level down.
 *
 * Collected once and used twice: the machine reads it to work out what to
 * run, and the same list is what the browser shows when somebody would
 * rather pick than accept the guess. */
enum { EXES_MAX = 128, EXE_DEPTH = 3 };

typedef struct {
    char rel[160]; /* under the folder, with DOS separators */
    long size;
} exe_entry;

typedef struct {
    exe_entry *out;
    int n, max, depth;
    char prefix[128]; /* how deep this walk is, as a DOS path */
} exe_walk;

static SDL_EnumerationResult exe_cb(void *ud, const char *dirname, const char *fname);

static void walk_exes(const char *host_dir, exe_walk *w) {
    char at[1400];
    snprintf(at, sizeof at, "%s", host_dir);
    ensure_host_sep(at, sizeof at);
    SDL_EnumerateDirectory(at, exe_cb, w);
}

static SDL_EnumerationResult exe_cb(void *ud, const char *dirname, const char *fname) {
    exe_walk *w = ud;
    if (fname[0] == '.' || w->n >= w->max)
        return SDL_ENUM_CONTINUE;
    char path[1400];
    snprintf(path, sizeof path, "%s%s", dirname, fname);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info))
        return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        if (w->depth >= EXE_DEPTH)
            return SDL_ENUM_CONTINUE;
        char was[128];
        snprintf(was, sizeof was, "%s", w->prefix);
        snprintf(w->prefix, sizeof w->prefix, "%s%s\\", was, fname);
        w->depth++;
        walk_exes(path, w);
        w->depth--;
        snprintf(w->prefix, sizeof w->prefix, "%s", was);
        return SDL_ENUM_CONTINUE;
    }
    if (!is_exe(fname))
        return SDL_ENUM_CONTINUE;
    exe_entry *e = &w->out[w->n++];
    snprintf(e->rel, sizeof e->rel, "%s%s", w->prefix, fname);
    upper_in_place(e->rel);
    e->size = (long)info.size;
    return SDL_ENUM_CONTINUE;
}

/* The one scratch list they share: read_folder runs while an add is being
 * put together and browse_exes while the form is up, so the two are never
 * wanting it at once. */
static exe_entry found[EXES_MAX];

static int collect_exes(const char *dos_dir, exe_entry *out, int max) {
    char host[1400];
    if (!install_host_path(dos_dir, host, sizeof host))
        return 0;
    exe_walk w = {out, 0, max, 0, ""};
    walk_exes(host, &w);
    return w.n;
}

/* The ones that set a thing up rather than run it. */
static int is_setup_name(const char *rel) {
    const char *name = leaf(rel);
    return !SDL_strncasecmp(name, "SETUP", 5) || !SDL_strncasecmp(name, "INSTALL", 7) ||
           !SDL_strncasecmp(name, "CONFIG", 6) || !SDL_strncasecmp(name, "SOUND", 5);
}

/* The directory part of a relative path, and the name at the end of it. */
static void split_rel(const char *rel, char *dir, size_t dn, char *name, size_t nn) {
    const char *at = strrchr(rel, '\\');
    if (!at) {
        dir[0] = 0;
        snprintf(name, nn, "%s", rel);
        return;
    }
    snprintf(dir, dn, "%.*s", (int)(at - rel), rel);
    snprintf(name, nn, "%s", at + 1);
}

/* ---- the file browser ---------------------------------------------------
 *
 * The machine's own, not the host's.  Asking the operating system to put up
 * its file dialog would work and would be less code, but it would also be
 * the one moment in the whole program that admits there is an operating
 * system: a 2020s sheet sliding down over a CRT.  So this is a panel like
 * the others, laid out the way the file managers of the period laid one out
 * - the folder along the top, `..` first, directories before files with
 * <DIR> where the size goes, sizes in bytes and right aligned, and a letter
 * typed jumping to the next name that starts with it. */

enum {
    BROWSE_MAX = 1024,
    B_ROW = 16,
    /* the list sits between the folder's well and the rule over the keys,
     * and the count of rows comes from that gap rather than the other way
     * round, so the last row cannot land on the rule */
    B_LIST_Y = EY + E_HEAD + 18 + CV_LINE + 5,
    B_LIST_BOTTOM = EY + EH - E_FOOT - 4
};

typedef struct {
    char name[176]; /* a name, or a program's place under a folder */
    int dir;
    long size;
} bentry;

/* What is being looked for.  On the DOS side only folders and programs are
 * shown, because those are the only two things that can be chosen there:
 * walk into a folder, or take the program and the folder holding it. */
enum { B_HOST, B_DOS, B_EXE };
enum { BI_ZIP = 1, BI_DIR, BI_DEST, BI_RUN, BI_SETUP }; /* what a choice is for */

static struct {
    int mode, intent;
    char at[CAT_PATH]; /* the folder on show, ending in a separator */
    bentry e[BROWSE_MAX];
    int n, row, scroll;
    char find[24]; /* typed to jump; forgotten after a moment */
    Uint64 find_t0, click_t0;
    int click_row;
    char note[120]; /* when a folder will not open */
} B;

static char sep(void) {
    return B.mode == B_DOS ? '\\' : '/';
}

/* The folder on show, as a place on this computer.  On the DOS side that
 * means going through the drive it names. */
static int browse_host(char *out, size_t n) {
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

/* The folder above this one, or 0 when there is nothing above it - the root
 * of a disk on one system, a drive letter on another. */
static int go_up(const char *at, char *out, size_t n) {
    char p[CAT_PATH];
    snprintf(p, sizeof p, "%s", at);
    size_t k = strlen(p);
    while (k > 0 && (p[k - 1] == '/' || p[k - 1] == '\\'))
        k--; /* its own trailing separator */
    while (k > 0 && p[k - 1] != '/' && p[k - 1] != '\\')
        k--; /* and the name it ends with */
    if (k == 0)
        return 0;
    p[k] = 0; /* the separator stays: a folder is named with one */
    if (!strcmp(p, at))
        return 0; /* a root is its own parent */
    snprintf(out, n, "%s", p);
    return 1;
}

static SDL_EnumerationResult browse_add(void *ud, const char *dirname, const char *fname) {
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
    if (B.mode == B_DOS && !dir && !is_exe(fname))
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
static void browse_list(const char *dir, const char *keep) {
    snprintf(B.at, sizeof B.at, "%s", dir);
    ensure_sep(B.at, sizeof B.at);
    B.n = 0;
    B.note[0] = 0;
    B.find[0] = 0;
    char host[1400];
    if (!browse_host(host, sizeof host))
        snprintf(B.note, sizeof B.note, "that drive is not mounted");
    else if (!SDL_EnumerateDirectory(host, browse_add, NULL))
        snprintf(B.note, sizeof B.note, "this folder will not open");
    qsort(B.e, (size_t)B.n, sizeof B.e[0], by_entry);
    char up[CAT_PATH];
    if (go_up(B.at, up, sizeof up)) { /* `..`, first, as the period put it */
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

/* The folder on show is the one: used by the key that takes it, and by a
 * program being chosen inside it. */
static void browse_folder(void) {
    char at[WHERE_PATH];
    snprintf(at, sizeof at, "%s", B.at);
    trim_sep(at);
    if (B.intent == BI_DEST) {
        /* Not where it goes, but what it goes *in*: the title still gets a
         * folder of its own inside whatever was picked, because that is
         * what a folder full of loose games looks like otherwise. */
        if (E.dest_point)
            snprintf(E.dest, sizeof E.dest, "%s", at);
        else
            snprintf(E.dest, sizeof E.dest, "%s\\%s", at, E.dest_id);
        E.fault[0] = 0;
        E.panel = PANEL_DEST;
        return;
    }
    snprintf(E.add_dir, sizeof E.add_dir, "%s", at);
    add_chose_dir();
}

/* The rows that fit, which the drawing and the keys both need. */
static int browse_rows(void) {
    return (B_LIST_BOTTOM - B_LIST_Y) / B_ROW;
}

static void browse_show(int row) {
    int rows = browse_rows();
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

/* The programs under the title's folder, as a list to choose from.  No
 * walking about: the whole tree is already in front of you, each one shown
 * where it sits, so picking the one in a subfolder says both what to run
 * and where it runs. */
static void browse_exes(int intent) {
    const char *root = E.add_root[0] ? E.add_root : E.add_dir;
    /* SETUP only makes sense beside the thing it sets up, so that list is
     * the one folder rather than the tree. */
    if (intent == BI_SETUP)
        root = E.add_dir[0] ? E.add_dir : E.add_root;
    B.mode = B_EXE;
    B.intent = intent;
    B.n = 0;
    B.note[0] = 0;
    B.find[0] = 0;
    snprintf(B.at, sizeof B.at, "%s", root);
    int n = collect_exes(root, found, EXES_MAX);
    for (int i = 0; i < n && B.n < BROWSE_MAX; i++) {
        if (intent == BI_SETUP && strchr(found[i].rel, '\\'))
            continue; /* not in this folder */
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
    E.panel = PANEL_BROWSE;
}

/* Opened on one side or the other, for one purpose or another. */
/* The host side, looking for the archive an add starts from. */
static void browse_host_for(char *into, int intent) {
    B.mode = B_HOST;
    B.intent = intent;
    char start[CAT_PATH] = "";
    if (!into[0] || !go_up(into, start, sizeof start)) {
        const char *dl = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
        if (!dl)
            dl = SDL_GetUserFolder(SDL_FOLDER_HOME);
        snprintf(start, sizeof start, "%s", dl ? dl : "/");
    }
    browse_list(start, NULL);
    E.panel = PANEL_BROWSE;
}

static void browse_dos_for(const char *start, int intent) {
    B.mode = B_DOS;
    B.intent = intent;
    char at[CAT_PATH];
    snprintf(at, sizeof at, "%s", start);
    browse_list(at, NULL);
    E.panel = PANEL_BROWSE;
}

/* ENTER: into a folder, or out with the file. */
static void browse_take(void) {
    if (B.row < 0 || B.row >= B.n)
        return;
    const bentry *e = &B.e[B.row];
    if (e->dir) {
        char to[CAT_PATH], was[96];
        if (!strcmp(e->name, "..")) {
            /* the name being left, so the cursor lands back on it */
            char here[CAT_PATH];
            snprintf(here, sizeof here, "%s", B.at);
            size_t k = strlen(here);
            while (k > 0 && (here[k - 1] == '/' || here[k - 1] == '\\'))
                here[--k] = 0;
            const char *last = strrchr(here, '/'), *back = strrchr(here, '\\');
            if (back && (!last || back > last))
                last = back;
            snprintf(was, sizeof was, "%s", last ? last + 1 : here);
            if (go_up(B.at, to, sizeof to))
                browse_list(to, was);
            return;
        }
        snprintf(to, sizeof to, "%s%s%c", B.at, e->name, sep());
        browse_list(to, NULL);
        return;
    }
    /* A program is a way of saying "this folder, and that is the one to
     * start": the folder is taken and the choice is kept as the answer the
     * machine would otherwise have had to guess. */
    if (B.intent == BI_DIR) {
        snprintf(E.work.run, sizeof E.work.run, "%s", e->name);
        upper_in_place(E.work.run);
        browse_folder();
        return;
    }
    if (B.intent == BI_DEST)
        return; /* a program is not a place to put one */
    if (B.intent == BI_RUN || B.intent == BI_SETUP) {
        char dir[128], base[64];
        split_rel(e->name, dir, sizeof dir, base, sizeof base);
        if (B.intent == BI_SETUP)
            snprintf(E.work.setup, sizeof E.work.setup, "%s", base);
        else {
            snprintf(E.work.run, sizeof E.work.run, "%s", base);
            /* where it sits is where it runs, and its setup went with the
             * folder it was found in */
            if (dir[0])
                snprintf(E.add_dir, sizeof E.add_dir, "%s\\%s", B.at, dir);
            else
                snprintf(E.add_dir, sizeof E.add_dir, "%s", B.at);
            E.work.setup[0] = 0;
        }
        E.fault[0] = 0;
        E.panel = PANEL_TITLE;
        build_title_form();
        return;
    }
    /* the only thing left is the archive an add is starting from */
    snprintf(E.add_zip, sizeof E.add_zip, "%s%s", B.at, e->name);
    E.fault[0] = 0;
    add_chose_zip();
}

/* ---- what the machine can work out for itself --------------------------- */

/* An id worth offering, out of the name of a file or a folder: its stem,
 * upper case, cut to the eight DOS holds, with anything DOS would not take
 * dropped.  Only a suggestion - it is on the form to be changed. */
static void guess_id(const char *name, char *out, size_t n) {
    char id[CAT_ID] = "";
    size_t k = 0;
    for (const char *p = name; *p && k + 1 < sizeof id; p++) {
        if (*p == '.')
            break; /* the stem only */
        char c = (char)toupper((unsigned char)*p);
        if (isupper((unsigned char)c) || isdigit((unsigned char)c) || c == '-' || c == '_')
            id[k++] = c;
    }
    id[k] = 0;
    snprintf(out, n, "%s", id);
}

/* Read the folder and fill in everything that can be had from it: the id
 * and a name from what it is called, and what to run and what sets it up
 * from what is in it.  Anything already chosen by hand is left alone -
 * somebody said so, which beats any of this.
 *
 * `add_root` is where the archive went; `add_dir` is where the program
 * turned out to be, which is the same place unless the archive brought
 * folders of its own. */
static void read_folder(const char *dos_dir) {
    snprintf(E.add_root, sizeof E.add_root, "%s", dos_dir);
    snprintf(E.add_dir, sizeof E.add_dir, "%s", dos_dir);

    /* The folder above it, when it is one of the categories, is what the
     * title is: the path was chosen knowing that, so believe it rather
     * than whatever the panel happened to be showing. */
    char up[WHERE_PATH];
    if (go_up(dos_dir, up, sizeof up)) {
        trim_sep(up);
        const char *parent = leaf(up);
        for (int i = 0; i < categories(); i++)
            if (!SDL_strcasecmp(parent, CATEGORY_OPT[i]))
                E.category = i;
    }

    const char *name = leaf(dos_dir);
    guess_id(name, E.work.id, sizeof E.work.id);
    if (!E.work.name[0])
        snprintf(E.work.name, sizeof E.work.name, "%s", name);
    if (E.work.run[0])
        return; /* already chosen, in the browser or by hand */

    int n = collect_exes(dos_dir, found, EXES_MAX);
    /* One named after its folder is nearly always the one; failing that the
     * biggest, since the game is the largest file in a game folder far more
     * often than it is the first alphabetically. */
    int best = -1, named = -1;
    for (int i = 0; i < n; i++) {
        if (is_setup_name(found[i].rel))
            continue;
        char dir[128], base[64];
        split_rel(found[i].rel, dir, sizeof dir, base, sizeof base);
        size_t stem = strcspn(base, ".");
        if (stem == strlen(E.work.id) && !SDL_strncasecmp(base, E.work.id, stem))
            named = i;
        if (best < 0 || found[i].size > found[best].size)
            best = i;
    }
    int pick = named >= 0 ? named : best;
    if (pick < 0)
        return; /* nothing to run: the form says so when it is saved */

    char dir[128], base[64];
    split_rel(found[pick].rel, dir, sizeof dir, base, sizeof base);
    snprintf(E.work.run, sizeof E.work.run, "%s", base);
    if (dir[0]) /* it came with a tree of its own: it runs inside it */
        snprintf(E.add_dir, sizeof E.add_dir, "%s\\%s", E.add_root, dir);

    /* A setup program is only useful beside the thing it sets up. */
    if (!E.work.setup[0])
        for (int i = 0; i < n; i++) {
            char sdir[128], sbase[64];
            split_rel(found[i].rel, sdir, sizeof sdir, sbase, sizeof sbase);
            if (is_setup_name(found[i].rel) && !strcmp(sdir, dir)) {
                snprintf(E.work.setup, sizeof E.work.setup, "%s", sbase);
                break;
            }
        }
}

/* ---- adding a title to a disk catalogue --------------------------------- */

/* Both ways in arrive here: a folder, and whatever the machine could make
 * of it, on the form to be corrected. */
static void add_chose_dir(void) {
    read_folder(E.add_dir);
    E.add_step = ADD_FORM;
    E.adding = 1;
    E.at = -1;
    E.row = 0;
    E.scroll = 0;
    E.fault[0] = 0;
    snprintf(E.year, sizeof E.year, "%d", E.work.year ? E.work.year : 1990);
    E.category = index_of(CATEGORY_OPT, categories(), CATEGORY_OPT[E.category]);
    E.panel = PANEL_TITLE;
    build_title_form();
}

/* The archive is chosen.  Where it goes is asked before anything is
 * unpacked, with the archive's own name standing in for an id nobody has
 * given yet - it is only a folder name, and the form can change it. */
static void add_chose_zip(void) {
    cat_title t;
    memset(&t, 0, sizeof t);
    guess_id(leaf(E.add_zip), t.id, sizeof t.id);
    if (!t.id[0])
        snprintf(t.id, sizeof t.id, "TITLE");
    snprintf(t.category, sizeof t.category, "%s", CATEGORY_OPT[0]);
    memset(&E.work, 0, sizeof E.work);
    E.category = 0;
    E.dest_title = NULL;
    E.dest_point = 0;
    E.fault[0] = 0;
    snprintf(E.dest_id, sizeof E.dest_id, "%s", t.id);
    install_default_dir(E.shelf, &t, E.dest, sizeof E.dest);
    E.add_step = ADD_DEST;
    E.panel = PANEL_DEST;
}

/* The two ways in. */
enum { ADD_FROM_ZIP, ADD_FROM_DRIVE, ADD_WAYS };

static const char *add_way(int i) {
    return i == ADD_FROM_ZIP ? "Install it from an archive" : "It is already on a drive";
}
static const char *add_way_under(int i) {
    return i == ADD_FROM_ZIP ? "a .zip on this computer, unpacked onto a drive"
                             : "find the program and the machine remembers where it is";
}

static void add_take(int way) {
    memset(&E.work, 0, sizeof E.work);
    E.add_dir[0] = 0;
    E.add_made = 0;
    if (way == ADD_FROM_ZIP) {
        E.add_zip[0] = 0;
        E.add_step = ADD_ZIP;
        browse_host_for(E.add_zip, BI_ZIP);
    } else {
        char root[8];
        snprintf(root, sizeof root, "%c:\\", cat_default_drive());
        E.add_step = ADD_HERE;
        browse_dos_for(root, BI_DIR);
    }
}

static void add_start(void) {
    E.add_step = ADD_OFF;
    E.add_row = ADD_FROM_ZIP;
    E.add_zip[0] = 0;
    E.add_dir[0] = 0;
    E.add_made = 0;
    E.fault[0] = 0;
    E.panel = PANEL_ADD;
}

/* The installer has finished with what an add was waiting on. */
void edit_installed(int ok, const char *err) {
    if (E.add_step != ADD_WAIT)
        return;
    if (!ok) {
        snprintf(E.fault, sizeof E.fault, "%s", err && err[0] ? err : "it would not install");
        E.add_step = ADD_DEST;
        E.panel = PANEL_DEST;
        return;
    }
    /* Unpacked.  Now read what landed and put it on the form. */
    E.add_made = 1;
    snprintf(E.add_dir, sizeof E.add_dir, "%s", E.dest);
    add_chose_dir();
}

/* ---- the keys ------------------------------------------------------------ */

static int row_stops(int i) {
    return i >= 0 && i < E.nf && E.f[i].kind != FLD_RULE && E.f[i].kind != FLD_NOTE;
}

static void move_row(int by) {
    int at = E.row;
    for (int k = 0; k < E.nf; k++) {
        at += by;
        if (at < 0)
            at = E.nf - 1;
        if (at >= E.nf)
            at = 0;
        if (row_stops(at)) {
            E.row = at;
            break;
        }
    }
    if (E.row < E.scroll)
        E.scroll = E.row;
    if (E.row >= E.scroll + E_ROWS)
        E.scroll = E.row - E_ROWS + 1;
}

static void dest_key(int sc, int shift);
static void dest_take(void);
static void dest_browse(void);
static void dest_category(int by);

/* The keys in the browser.  A letter does not type into anything here, so
 * it jumps to the next name that starts with what has been typed - the
 * quick search every file manager of the period had, forgotten after a
 * second so the next letter starts again. */
static void browse_key(int sc, int shift) {
    int rows = browse_rows();
    switch (sc) {
    case SDL_SCANCODE_ESCAPE:
        E.panel = B.intent == BI_DEST ? PANEL_DEST : PANEL_TITLE;
        return;
    case SDL_SCANCODE_UP:
        browse_show(B.row - 1);
        return;
    case SDL_SCANCODE_DOWN:
        browse_show(B.row + 1);
        return;
    case SDL_SCANCODE_PAGEUP:
        browse_show(B.row - rows);
        return;
    case SDL_SCANCODE_PAGEDOWN:
        browse_show(B.row + rows);
        return;
    case SDL_SCANCODE_HOME:
        browse_show(0);
        return;
    case SDL_SCANCODE_END:
        browse_show(B.n - 1);
        return;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        browse_take();
        return;
    case SDL_SCANCODE_F10:
        if (B.intent == BI_DIR || B.intent == BI_DEST)
            browse_folder();
        return;
    case SDL_SCANCODE_BACKSPACE:
    case SDL_SCANCODE_LEFT: {
        char up[CAT_PATH];
        if (B.mode != B_EXE && go_up(B.at, up, sizeof up))
            browse_list(up, NULL);
        return;
    }
    default:
        break;
    }
    char c = typed(sc, shift);
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
            browse_show(i);
            return;
        }
    B.find[k] = 0; /* nothing starts with that: keep what did match */
}

static void form_key(int sc, int shift) {
    field *f = (E.row >= 0 && E.row < E.nf) ? &E.f[E.row] : NULL;
    switch (sc) {
    case SDL_SCANCODE_ESCAPE:
        E.panel = PANEL_MENU;
        return;
    case SDL_SCANCODE_UP:
        move_row(-1);
        return;
    case SDL_SCANCODE_DOWN:
    case SDL_SCANCODE_TAB:
        move_row(1);
        return;
    case SDL_SCANCODE_LEFT:
        if (f && f->kind == FLD_CHOICE)
            *f->pick = (*f->pick + f->nopts - 1) % f->nopts;
        break;
    case SDL_SCANCODE_RIGHT:
        if (f && f->kind == FLD_CHOICE)
            *f->pick = (*f->pick + 1) % f->nopts;
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        if (f && f->browse) {
            browse_exes(f->text == E.work.setup ? BI_SETUP : BI_RUN);
            return;
        }
        if (f && f->kind == FLD_CHOICE) {
            *f->pick = (*f->pick + 1) % f->nopts;
            break;
        }
        move_row(1);
        return;
    case SDL_SCANCODE_BACKSPACE:
        if (f && f->text && f->kind != FLD_NOTE) {
            size_t n = strlen(f->text);
            if (n)
                f->text[n - 1] = 0;
            E.fault[0] = 0;
        }
        return;
    case SDL_SCANCODE_F10:
        if (E.panel == PANEL_SHELF)
            save_new_shelf();
        else
            save_title();
        return;
    case SDL_SCANCODE_DELETE:
        if (E.panel == PANEL_TITLE && !E.adding)
            ask_delete_title(E.at);
        return;
    default: {
        char c = typed(sc, shift);
        if (c && f)
            type_into(f, c);
        return;
    }
    }
    /* a choice was stepped: what the form asks for may have changed with it */
    E.fault[0] = 0;
    if (E.panel == PANEL_TITLE)
        build_title_form();
    else if (E.panel == PANEL_SHELF) {
        build_shelf_form();
        if (E.row >= E.nf)
            E.row = E.nf - 1;
        if (!row_stops(E.row))
            move_row(-1);
    }
}

void edit_key(int sc, int shift) {
    switch (E.panel) {
    case PANEL_MENU:
        switch (sc) {
        case SDL_SCANCODE_ESCAPE:
            E.panel = PANEL_NONE;
            break;
        case SDL_SCANCODE_UP:
            do {
                E.menu_row = (E.menu_row + MENU_ROWS - 1) % MENU_ROWS;
            } while (E.menu_row == M_RULE1);
            break;
        case SDL_SCANCODE_DOWN:
            do {
                E.menu_row = (E.menu_row + 1) % MENU_ROWS;
            } while (E.menu_row == M_RULE1);
            break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_SPACE:
            menu_take(E.menu_row);
            break;
        default:
            break;
        }
        return;
    case PANEL_TITLE:
    case PANEL_SHELF:
        form_key(sc, shift);
        return;
    case PANEL_BROWSE:
        browse_key(sc, shift);
        return;
    case PANEL_DEST:
        dest_key(sc, shift);
        return;
    case PANEL_ADD:
        if (sc == SDL_SCANCODE_ESCAPE)
            E.panel = PANEL_MENU;
        else if (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN)
            E.add_row = (E.add_row + 1) % ADD_WAYS;
        else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER ||
                 sc == SDL_SCANCODE_SPACE)
            add_take(E.add_row);
        return;
    case PANEL_CONFIRM:
        if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) {
            if (E.ask_what == ASK_TITLE)
                do_delete_title();
            else if (E.ask_what == ASK_SHELF)
                do_delete_shelf();
            else
                do_reset_shelf();
        } else if (sc == SDL_SCANCODE_ESCAPE)
            E.panel = PANEL_MENU;
        return;
    default:
        return;
    }
}

void edit_click(int mx, int my) {
    int what = hit_at(mx, my);
    if (what < 0) {
        if (E.panel == PANEL_MENU)
            E.panel = PANEL_NONE; /* off the menu: it goes */
        return;
    }
    if (E.panel == PANEL_ADD) {
        if (what == HIT_CANCEL)
            E.panel = PANEL_MENU;
        else if (what >= HIT_ROW && what < HIT_ROW + ADD_WAYS) {
            E.add_row = what - HIT_ROW;
            add_take(E.add_row);
        }
        return;
    }
    if (E.panel == PANEL_DEST) {
        if (what == HIT_OK)
            dest_take();
        else if (what == HIT_FOLDER)
            dest_browse();
        else if (what == HIT_ROW && !E.dest_point)
            dest_category(1);
        else if (what == HIT_CANCEL)
            dest_key(SDL_SCANCODE_ESCAPE, 0);
        return;
    }
    if (E.panel == PANEL_BROWSE) {
        Uint64 now = SDL_GetTicks();
        if (what == HIT_CANCEL) {
            E.panel = B.intent == BI_DEST ? PANEL_DEST : PANEL_TITLE;
        } else if (what == HIT_OK) {
            browse_take();
        } else if (what == HIT_DELETE) {
            char up[CAT_PATH];
            if (go_up(B.at, up, sizeof up))
                browse_list(up, NULL);
        } else if (what == HIT_FOLDER) {
            if (B.intent == BI_DIR || B.intent == BI_DEST)
                browse_folder();
        } else if (what >= HIT_ROW && what < HIT_OK) {
            int row = what - HIT_ROW;
            int again = row == B.click_row && now - B.click_t0 < 400;
            browse_show(row);
            B.click_row = row;
            B.click_t0 = now;
            if (again)
                browse_take();
        }
        return;
    }
    switch (what) {
    case HIT_CANCEL:
        E.panel = E.panel == PANEL_MENU ? PANEL_NONE : PANEL_MENU;
        return;
    case HIT_OK:
        if (E.panel == PANEL_CONFIRM)
            edit_key(SDL_SCANCODE_RETURN, 0);
        else if (E.panel == PANEL_SHELF)
            save_new_shelf();
        else if (E.panel == PANEL_TITLE)
            save_title();
        return;
    case HIT_DELETE:
        if (E.panel == PANEL_TITLE && !E.adding)
            ask_delete_title(E.at);
        return;
    case HIT_PANEL:
        return;
    default:
        break;
    }
    if (what >= HIT_ROW && what < HIT_OK) {
        int row = what - HIT_ROW;
        if (E.panel == PANEL_MENU) {
            E.menu_row = row;
            menu_take(row);
        } else if (row_stops(row)) {
            field *f = &E.f[row];
            E.row = row;
            if (f->browse)
                browse_exes(f->text == E.work.setup ? BI_SETUP : BI_RUN);
            else if (f->kind == FLD_CHOICE) {
                *f->pick = (*f->pick + 1) % f->nopts;
                if (E.panel == PANEL_TITLE)
                    build_title_form();
            }
        }
    }
}

/* ---- drawing -------------------------------------------------------------- */

static void draw_menu(void) {
    int rows = MENU_ROWS;
    int h = 40 + rows * MENU_ROW + 34;
    int y0 = (SCR_H - h) / 2;
    gui_panel(MENU_X, y0, MENU_W, h);
    cv_text_bold(MENU_X + 16, y0 + 12, "Catalogue", G_WHITE, -1);
    if (E.shelf)
        cv_text_small(MENU_X + MENU_W - 16 - cv_width_small(E.shelf->cat.name), y0 + 14,
                      E.shelf->cat.name, G_TEXT2);
    cv_rect(MENU_X + 12, y0 + 34, MENU_W - 24, 1, G_LINE);
    int y = y0 + 40;
    for (int i = 0; i < rows; i++) {
        if (i == M_RULE1) {
            cv_rect(MENU_X + 16, y + MENU_ROW / 2, MENU_W - 32, 1, G_LINE);
            y += MENU_ROW;
            continue;
        }
        const char *off = menu_off(i);
        int on = E.menu_row == i;
        if (on) {
            cv_rect(MENU_X + 8, y - 1, MENU_W - 16, MENU_ROW, G_BAR);
            cv_rect(MENU_X + 8, y - 1, 3, MENU_ROW, G_GOLD);
        }
        cv_text(MENU_X + 16, y + 1, menu_label(i), off ? G_OFF : (on ? G_WHITE : G_TEXT), -1);
        if (on && off)
            cv_text_small(MENU_X + MENU_W - 16 - cv_width_small(off), y + 3, off, G_OFF);
        add_hit(MENU_X + 8, y - 1, MENU_W - 16, MENU_ROW, HIT_ROW + i);
        y += MENU_ROW;
    }
    int fy = y0 + h - 26;
    cv_rect(MENU_X + 12, fy - 8, MENU_W - 24, 1, G_LINE);
    int w = gui_hint(MENU_X + 16, fy, "ESC", "Close", 0, 0);
    add_hit(MENU_X + 16, fy - 2, w, CV_LINE + 4, HIT_CANCEL);
    add_hit(MENU_X, y0, MENU_W, h, HIT_PANEL);
}

/* One field: its name on the left, what it says on the right, and under the
 * cursor the line of help that says what the machine wants there. */
static void draw_field(const field *f, int y, int on, int caret) {
    if (f->kind == FLD_RULE) {
        cv_rect(EX + 16, y + E_ROW / 2, EW - 32, 1, G_LINE);
        return;
    }
    if (on) {
        cv_rect(EX + 8, y - 1, EW - 16, E_ROW, G_BAR);
        cv_rect(EX + 8, y - 1, 3, E_ROW, G_GOLD);
    }
    cv_text_small(EX + 16, y + 3, f->label, on ? G_TEXT : G_TEXT2);
    if (f->kind == FLD_CHOICE) {
        cv_text(E_VALUE_X, y + 1, f->opts[*f->pick], on ? G_WHITE : G_TEXT, -1);
        if (on) { /* which way it steps */
            int w = cv_width(f->opts[*f->pick]);
            cv_text(E_VALUE_X - 12, y + 1, "\x1B", G_GOLD, -1);
            cv_text(E_VALUE_X + w + 6, y + 1, "\x1A", G_GOLD, -1);
        }
        return;
    }
    if (f->kind == FLD_NOTE) {
        cv_text(E_VALUE_X, y + 1, f->text, G_TEXT2, -1);
        return;
    }
    /* Text: what there is room for, from the end, so what is being typed
     * stays in sight however long the whole of it is. */
    const char *s = f->text;
    int w = cv_width(s);
    while (w > E_VALUE_W && *s) {
        s++;
        w = cv_width(s);
    }
    int tw = 0;
    if (*s)
        tw = cv_text(E_VALUE_X, y + 1, s, on ? G_WHITE : G_TEXT, -1);
    else if (f->hint && on)
        cv_text(E_VALUE_X, y + 1, f->hint, G_OFF, -1);
    if (on && caret)
        cv_rect(E_VALUE_X + tw + 1, y + 2, 1, CV_LINE - 3, G_GOLD);
}

static void draw_form(void) {
    if (E.panel == PANEL_TITLE && E.shelf && E.shelf->cat.holds == CAT_DISK)
        say_where(); /* the folder may have come or gone while this was open */
    const char *head =
        E.panel == PANEL_SHELF ? "A new catalogue" : (E.adding ? "A new title" : "This title");
    gui_panel(EX, EY, EW, EH);
    cv_text_bold(EX + 16, EY + 12, head, G_WHITE, -1);
    /* which catalogue this is about - not on the form for a new one, which
     * is about no catalogue yet */
    if (E.shelf && E.panel != PANEL_SHELF) {
        char where[120];
        snprintf(where, sizeof where, "%s  \x07  %s", E.shelf->cat.name,
                 E.shelf->cat.holds == CAT_DISK ? "on this computer" : "to download");
        cv_text_small(EX + EW - 16 - cv_width_small(where), EY + 14, where, G_TEXT2);
    }
    cv_rect(EX + 12, EY + E_HEAD - 6, EW - 24, 1, G_LINE);

    int blink = (SDL_GetTicks() % 1060) < 530;
    int y = EY + E_HEAD;
    for (int i = E.scroll; i < E.nf && i < E.scroll + E_ROWS; i++) {
        draw_field(&E.f[i], y, i == E.row, blink);
        if (E.f[i].kind != FLD_RULE)
            add_hit(EX + 8, y - 1, EW - 16, E_ROW, HIT_ROW + i);
        y += E_ROW;
    }
    if (E.nf > E_ROWS) {
        int sx = EX + EW - 16;
        float shown = (float)E_ROWS / (float)E.nf;
        float at = (float)E.scroll / (float)(E.nf - E_ROWS);
        gui_scrollbar(sx, EY + E_HEAD - 2, 12, E_ROWS * E_ROW, at, shown);
    }

    int fy = EY + EH - 26;
    cv_rect(EX + 12, fy - 8, EW - 24, 1, G_LINE);
    /* what is stopping it, where the eye goes when F10 does nothing */
    if (E.fault[0])
        cv_text(EX + 16, fy - 8 - CV_LINE - 4, E.fault, G_RED, -1);
    int pen = EX + 16;
    int w = gui_hint(pen, fy, "F10", "Save", 0, 0);
    add_hit(pen, fy - 2, w, CV_LINE + 4, HIT_OK);
    pen += w + 20;
    if (E.panel == PANEL_TITLE && !E.adding) {
        w = gui_hint(pen, fy, "DEL", "Take out", 0, 0);
        add_hit(pen, fy - 2, w, CV_LINE + 4, HIT_DELETE);
        pen += w + 20;
    }
    w = gui_hint(0, -100, "ESC", "Cancel", 0, 0);
    pen = EX + EW - 16 - w;
    gui_hint(pen, fy, "ESC", "Cancel", 0, 0);
    add_hit(pen, fy - 2, w, CV_LINE + 4, HIT_CANCEL);
    add_hit(EX, EY, EW, EH, HIT_PANEL);
}

/* `s` cut down to `w` pixels, with three dots where it was cut.  A name is
 * cut at its end and a path at its front: the end of a path is the part
 * that says where you are. */
static void fit(const char *s, int w, int from_front, char *out, int n) {
    snprintf(out, (size_t)n, "%s", s);
    if (cv_width(out) <= w)
        return;
    char tmp[CAT_PATH + 8] = "";
    if (from_front) {
        for (const char *p = s; *p; p++) {
            snprintf(tmp, sizeof tmp, "...%s", p);
            if (cv_width(tmp) <= w)
                break;
        }
    } else {
        for (size_t k = strlen(s); k > 0;) {
            snprintf(tmp, sizeof tmp, "%.*s...", (int)--k, s);
            if (cv_width(tmp) <= w)
                break;
        }
    }
    snprintf(out, (size_t)n, "%s", tmp);
}

static void draw_browse(void) {
    enum { SIZE_W = 86, NAME_X = EX + 18 };
    int rows = browse_rows(), head_y = EY + E_HEAD + 18;
    gui_panel(EX, EY, EW, EH);
    cv_text_bold(EX + 16, EY + 12,
                 B.intent == BI_ZIP     ? "Find the archive"
                 : B.intent == BI_DEST  ? "Where to put it"
                 : B.intent == BI_RUN   ? "Which one does it run"
                 : B.intent == BI_SETUP ? "Which one sets it up"
                                        : "Find the folder it is in",
                 G_WHITE, -1);
    /* what has been typed to jump, where a file manager put it */
    if (B.find[0] && SDL_GetTicks() - B.find_t0 < 1000) {
        char q[40];
        snprintf(q, sizeof q, "%s_", B.find);
        cv_text_small(EX + EW - 16 - cv_width_small(q), EY + 14, q, G_GOLD);
    }
    /* the folder on show, in a well of its own across the panel */
    char line[CAT_PATH + 8];
    gui_well(EX + 14, EY + E_HEAD - 8, EW - 28, 20);
    fit(B.at, EW - 52, 1, line, sizeof line);
    cv_text(EX + 22, EY + E_HEAD - 6, line, G_TEXT, -1);

    /* the columns, named once */
    cv_text_small(NAME_X, head_y, "NAME", G_TEXT2);
    cv_text_small(EX + EW - 38 - cv_width_small("SIZE"), head_y, "SIZE", G_TEXT2);
    cv_rect(EX + 14, head_y + CV_LINE, EW - 28, 1, G_LINE);

    for (int i = B.scroll; i < B.n && i < B.scroll + rows; i++) {
        const bentry *e = &B.e[i];
        int y = B_LIST_Y + (i - B.scroll) * B_ROW, on = i == B.row;
        if (on) {
            cv_rect(EX + 14, y - 1, EW - 44, B_ROW, G_BAR);
            cv_rect(EX + 14, y - 1, 3, B_ROW, G_GOLD);
        }
        uint8_t ink = e->dir ? G_WHITE : (on ? G_WHITE : G_TEXT);
        fit(e->name, EW - 62 - SIZE_W, 0, line, sizeof line);
        cv_text(NAME_X, y, line, ink, -1);
        /* <DIR> where a folder's size would be, as DOS itself printed it */
        char sz[24];
        if (e->dir)
            snprintf(sz, sizeof sz, "%s", !strcmp(e->name, "..") ? "UP--DIR" : "<DIR>");
        else
            snprintf(sz, sizeof sz, "%ld", e->size);
        cv_text_small(EX + EW - 38 - cv_width_small(sz), y + 2, sz,
                      e->dir ? G_TEXT2 : (on ? G_WHITE : G_TEXT2));
        add_hit(EX + 14, y - 1, EW - 44, B_ROW, HIT_ROW + i);
    }
    if (!B.n)
        cv_text(NAME_X, B_LIST_Y + 4, B.note[0] ? B.note : "nothing in here",
                B.note[0] ? G_RED : G_TEXT2, -1);

    if (B.n > rows) {
        float shown = (float)rows / (float)B.n;
        float at = (float)B.scroll / (float)(B.n - rows);
        gui_scrollbar(EX + EW - 26, B_LIST_Y - 2, 12, rows * B_ROW, at, shown);
    }

    int fy = EY + EH - 26;
    cv_rect(EX + 12, fy - 8, EW - 24, 1, G_LINE);
    const bentry *on = (B.row >= 0 && B.row < B.n) ? &B.e[B.row] : NULL;
    int pen = EX + 16;
    int w = gui_hint(pen, fy, "ENTER", on && on->dir ? "Open" : "Choose", 0, !on);
    add_hit(pen, fy - 2, w, CV_LINE + 4, HIT_OK);
    pen += w + 20;
    if (B.mode != B_EXE) { /* a list of programs is not somewhere to walk */
        w = gui_hint(pen, fy, "\x1B", "Up a folder", 0, 0);
        add_hit(pen, fy - 2, w, CV_LINE + 4, HIT_DELETE);
        pen += w + 20;
    }
    if (B.mode != B_EXE &&
        (B.intent == BI_DIR || B.intent == BI_DEST)) { /* the folder on show can be taken */
        w = gui_hint(pen, fy, "F10", "This folder", 0, 0);
        add_hit(pen, fy - 2, w, CV_LINE + 4, HIT_FOLDER);
    }
    w = gui_hint(0, -100, "ESC", "Cancel", 0, 0);
    pen = EX + EW - 16 - w;
    gui_hint(pen, fy, "ESC", "Cancel", 0, 0);
    add_hit(pen, fy - 2, w, CV_LINE + 4, HIT_CANCEL);
    add_hit(EX, EY, EW, EH, HIT_PANEL);
}

/* Where to put it.  Every installer of the period opened by saying where it
 * was going and letting you say otherwise, and this is that: the default
 * filled in, ENTER to take it.  What is chosen is not written to the
 * catalogue - it is a fact about this machine, and goes to installed.cfg
 * when the install finishes. */
/* The path the template makes, for the category given.  Comparing against
 * it is how the panel knows whether the path is still its own suggestion or
 * something somebody typed. */
static void dest_build(int cat, char *out, size_t n) {
    cat_title t;
    memset(&t, 0, sizeof t);
    snprintf(t.id, sizeof t.id, "%s", E.dest_id);
    snprintf(t.category, sizeof t.category, "%s", CATEGORY_OPT[cat]);
    install_default_dir(E.shelf, &t, out, n);
}

/* Stepping the category moves the path with it - but only while the path is
 * still the one the machine suggested.  Once it has been typed over or
 * browsed to, it is the user's and is left alone. */
static void dest_category(int by) {
    char was[WHERE_PATH];
    dest_build(E.category, was, sizeof was);
    E.category = (E.category + by + categories()) % categories();
    if (!strcmp(E.dest, was))
        dest_build(E.category, E.dest, sizeof E.dest);
    E.fault[0] = 0;
}

static void draw_dest(void) {
    enum { DW = 470 };
    /* the category row is only there when the machine is putting the title
     * somewhere; pointing at one that is already down has no say in it */
    int rows = E.dest_point ? 0 : 1;
    int DH = 176 + rows * 30;
    int x = (SCR_W - DW) / 2, y = (SCR_H - DH) / 2;
    gui_panel(x, y, DW, DH);
    cv_text_bold(x + 20, y + 16, E.dest_point ? "Point at it" : "Install", G_WHITE, -1);
    const char *who = E.dest_title ? E.dest_title->name : E.work.name;
    if (who && who[0])
        cv_text_small(x + DW - 20 - cv_width_small(who), y + 18, who, G_TEXT2);
    cv_rect(x + 16, y + 40, DW - 32, 1, G_LINE);

    int ty = y + 52;
    if (!E.dest_point) {
        cv_text_small(x + 20, ty, "CATEGORY", G_TEXT2);
        cv_text(x + 130, ty - 2, CATEGORY_OPT[E.category], G_WHITE, -1);
        int cw = cv_width(CATEGORY_OPT[E.category]);
        cv_text(x + 118, ty - 2, "\x1B", G_GOLD, -1);
        cv_text(x + 136 + cw, ty - 2, "\x1A", G_GOLD, -1);
        add_hit(x + 118, ty - 4, cw + 34, CV_LINE + 4, HIT_ROW);
        ty += 30;
    }
    cv_text_small(x + 20, ty, E.dest_point ? "IT IS IN" : "INSTALL IT IN", G_TEXT2);
    int fy = ty + 18;
    gui_well(x + 18, fy, DW - 36, 26);
    char line[WHERE_PATH + 8];
    fit(E.dest, DW - 64, 1, line, sizeof line);
    int tw = cv_text(x + 26, fy + 5, line, G_WHITE, -1);
    if ((SDL_GetTicks() % 1060) < 530)
        cv_rect(x + 26 + tw + 1, fy + 7, 1, CV_LINE - 3, G_GOLD);
    if (E.fault[0])
        cv_text(x + 20, fy + 34, E.fault, G_RED, -1);
    else if (!E.dest_point && E.dest_title && E.dest_title->download.size > 0) {
        char note[120];
        snprintf(note, sizeof note, "%ld KB to fetch", E.dest_title->download.size / 1024);
        cv_text(x + 20, fy + 34, note, G_TEXT2, -1);
    }

    int ky = y + DH - 26;
    cv_rect(x + 16, ky - 8, DW - 32, 1, G_LINE);
    int pen = x + 20;
    int w = gui_hint(pen, ky, "ENTER", E.dest_point ? "That is it" : "Install", 0, 0);
    add_hit(pen, ky - 2, w, CV_LINE + 4, HIT_OK);
    pen += w + 22;
    w = gui_hint(pen, ky, "F2", "Browse", 0, 0);
    add_hit(pen, ky - 2, w, CV_LINE + 4, HIT_FOLDER);
    w = gui_hint(0, -100, "ESC", "Cancel", 0, 0);
    pen = x + DW - 20 - w;
    gui_hint(pen, ky, "ESC", "Cancel", 0, 0);
    add_hit(pen, ky - 2, w, CV_LINE + 4, HIT_CANCEL);
    add_hit(x, y, DW, DH, HIT_PANEL);
}

/* Open the browser on as much of the path as actually exists, so it starts
 * where you are going rather than at the top of the drive. */
static void dest_browse(void) {
    char at[WHERE_PATH], host[1400];
    snprintf(at, sizeof at, "%s", E.dest);
    for (int k = 0; k < 8; k++) {
        if (at[0] && at[1] == ':' && install_host_path(at, host, sizeof host)) {
            SDL_PathInfo info;
            if (SDL_GetPathInfo(host, &info) && info.type == SDL_PATHTYPE_DIRECTORY)
                break;
        }
        char up[WHERE_PATH];
        if (!go_up(at, up, sizeof up)) {
            snprintf(at, sizeof at, "%c:\\", cat_default_drive());
            break;
        }
        snprintf(at, sizeof at, "%s", up);
    }
    browse_dos_for(at, BI_DEST);
}

/* ---- where it goes, and where it already is ----------------------------- */

void edit_install(shelf *s, const cat_title *t) {
    if (!s || !t)
        return;
    E.shelf = s;
    E.dest_title = t;
    E.dest_point = 0;
    E.fault[0] = 0;
    E.add_step = ADD_OFF; /* whatever an abandoned add left behind is not this */
    E.add_zip[0] = 0;
    E.add_dir[0] = 0;
    E.add_made = 0;
    E.category = index_of(CATEGORY_OPT, categories(), t->category);
    snprintf(E.dest_id, sizeof E.dest_id, "%s", t->id);
    install_default_dir(s, t, E.dest, sizeof E.dest);
    E.panel = PANEL_DEST;
}

/* The same panel, asking the other question: not where to put it, but where
 * it already is.  Nothing is fetched or moved - the machine writes down the
 * folder and checks that what the title runs is in it. */
void edit_point(shelf *s, const cat_title *t) {
    if (!s || !t)
        return;
    E.shelf = s;
    E.dest_title = t;
    E.dest_point = 1;
    E.fault[0] = 0;
    snprintf(E.dest_id, sizeof E.dest_id, "%s", t->id);
    const char *at = where_get(s->cat.id, t->category, t->id);
    if (at)
        snprintf(E.dest, sizeof E.dest, "%s", at);
    else
        install_default_dir(s, t, E.dest, sizeof E.dest);
    E.panel = PANEL_DEST;
}

/* What is wrong with the destination, or NULL.  Checked here rather than
 * left to the installer so that it is said while it can still be fixed. */
/* Every step of a DOS path has to be a name DOS can reach, or nothing will
 * find it later however carefully it was written down. */
static const char *path_dos_fault(const char *dos, char *out, size_t n) {
    char part[128];
    size_t k = 0;
    for (const char *p = dos[1] == ':' ? dos + 2 : dos;; p++) {
        if (*p && *p != '\\' && *p != '/') {
            if (k + 1 < sizeof part)
                part[k++] = *p;
            continue;
        }
        part[k] = 0;
        if (k && !dos_reachable(part)) {
            char want[DOS_NAME];
            dos_name(part, want, sizeof want);
            snprintf(out, n, "DOS cannot reach \"%s\" - it would look for %s", part, want);
            return out;
        }
        k = 0;
        if (!*p)
            return NULL;
    }
}

static const char *dest_fault(void) {
    char host[1400];
    if (!E.dest[0])
        return "say where it goes";
    if (E.dest[1] != ':' || E.dest[0] < 'A' || E.dest[0] > 'Z')
        return "a path starts with a drive, as in C:\\DXM";
    if (strstr(E.dest, ".."))
        return "a path cannot go up out of its drive";
    if (strlen(E.dest) > DOS_PATH_MAX)
        return "longer than a path DOS will take";
    {
        static char bad[160];
        const char *why = path_dos_fault(E.dest, bad, sizeof bad);
        if (why)
            return why;
    }
    if (!install_host_path(E.dest, host, sizeof host))
        return "that drive is not mounted";
    return NULL;
}

static void dest_take(void) {
    const char *why = dest_fault();
    if (why) {
        snprintf(E.fault, sizeof E.fault, "%s", why);
        return;
    }
    const cat_title *t = E.dest_title;
    if (E.dest_point) {
        /* Pointed at, not installed: the folder has to be there already,
         * with what the title runs in it, or the machine would be writing
         * down something it cannot see. */
        char keep[CAT_ID], note[200];
        where_set(E.shelf->cat.id, t->category, t->id, E.dest, 0);
        if (!install_present(E.shelf, t)) {
            where_forget(E.shelf->cat.id, t->category, t->id);
            snprintf(E.fault, sizeof E.fault, "%s is not in that folder", t->run);
            return;
        }
        snprintf(keep, sizeof keep, "%s", E.shelf->cat.id);
        snprintf(note, sizeof note, "%s is at %s.", t->name, E.dest);
        E.panel = PANEL_NONE;
        cat_reload(keep, t->id, note, 0);
        return;
    }
    if (E.add_step == ADD_DEST) {
        /* an add: the archive is the one that was chosen, and when it is
         * unpacked the machine comes back to ask what to start */
        cat_title w = E.work;
        snprintf(w.category, sizeof w.category, "%s", CATEGORY_OPT[E.category]);
        if (!install_begin(E.shelf, &w, cat_pref_dir(), E.dest, E.add_zip)) {
            snprintf(E.fault, sizeof E.fault, "something else is installing");
            return;
        }
        E.add_step = ADD_WAIT;
        E.panel = PANEL_NONE;
        return;
    }
    if (!install_begin(E.shelf, t, cat_pref_dir(), E.dest, NULL)) {
        snprintf(E.fault, sizeof E.fault, "something else is installing");
        return;
    }
    E.panel = PANEL_NONE;
}

static void dest_key(int sc, int shift) {
    switch (sc) {
    case SDL_SCANCODE_ESCAPE:
        /* Back to the question that started it, or out - and either way the
         * archive that was chosen stops being the one to install. */
        if (E.add_step == ADD_DEST) {
            E.add_step = ADD_OFF;
            E.panel = PANEL_ADD;
        } else
            E.panel = PANEL_NONE;
        return;
    case SDL_SCANCODE_LEFT:
        if (!E.dest_point)
            dest_category(-1);
        return;
    case SDL_SCANCODE_RIGHT:
        if (!E.dest_point)
            dest_category(1);
        return;
    case SDL_SCANCODE_F2:
    case SDL_SCANCODE_TAB:
        dest_browse();
        return;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        dest_take();
        return;
    case SDL_SCANCODE_BACKSPACE: {
        size_t n = strlen(E.dest);
        if (n)
            E.dest[n - 1] = 0;
        E.fault[0] = 0;
        return;
    }
    default: {
        char c = typed(sc, shift);
        size_t n = strlen(E.dest);
        if (c && n + 1 < sizeof E.dest) {
            E.dest[n] = (char)toupper((unsigned char)c);
            E.dest[n + 1] = 0;
            E.fault[0] = 0;
        }
        return;
    }
    }
}

static void draw_add(void) {
    enum { AW = 440, AH = 168, AROW = 40 };
    int x = (SCR_W - AW) / 2, y = (SCR_H - AH) / 2;
    gui_panel(x, y, AW, AH);
    cv_text_bold(x + 20, y + 16, "A new title", G_WHITE, -1);
    if (E.shelf)
        cv_text_small(x + AW - 20 - cv_width_small(E.shelf->cat.name), y + 18, E.shelf->cat.name,
                      G_TEXT2);
    cv_rect(x + 16, y + 40, AW - 32, 1, G_LINE);
    int ry = y + 52;
    for (int i = 0; i < ADD_WAYS; i++) {
        int on = E.add_row == i;
        if (on) {
            cv_rect(x + 12, ry - 2, AW - 24, AROW - 6, G_BAR);
            cv_rect(x + 12, ry - 2, 3, AROW - 6, G_GOLD);
        }
        cv_text(x + 20, ry, add_way(i), on ? G_WHITE : G_TEXT, -1);
        cv_text_small(x + 20, ry + CV_LINE + 1, add_way_under(i), G_TEXT2);
        add_hit(x + 12, ry - 2, AW - 24, AROW - 6, HIT_ROW + i);
        ry += AROW;
    }
    int ky = y + AH - 26;
    cv_rect(x + 16, ky - 8, AW - 32, 1, G_LINE);
    int w = gui_hint(0, -100, "ESC", "Cancel", 0, 0);
    int pen = x + AW - 20 - w;
    gui_hint(pen, ky, "ESC", "Cancel", 0, 0);
    add_hit(pen, ky - 2, w, CV_LINE + 4, HIT_CANCEL);
    add_hit(x, y, AW, AH, HIT_PANEL);
}

static void draw_ask(void) {
    gui_panel(ASK_X, ASK_Y, ASK_W, ASK_H);
    cv_text_bold(ASK_X + 20, ASK_Y + 18, "Are you sure?", G_WHITE, -1);
    cv_rect(ASK_X + 16, ASK_Y + 42, ASK_W - 32, 1, G_LINE);
    cv_text_wrap(ASK_X + 20, ASK_Y + 54, ASK_W - 40, E.ask_line, G_TEXT);
    cv_text_wrap(ASK_X + 20, ASK_Y + 54 + CV_LINE + 4, ASK_W - 40, E.ask_small, G_TEXT2);
    int fy = ASK_Y + ASK_H - 26;
    cv_rect(ASK_X + 16, fy - 8, ASK_W - 32, 1, G_LINE);
    int w = gui_hint(ASK_X + 20, fy, "ENTER", "Yes, do it", 0, 0);
    add_hit(ASK_X + 20, fy - 2, w, CV_LINE + 4, HIT_OK);
    w = gui_hint(0, -100, "ESC", "Leave it alone", 0, 0);
    int pen = ASK_X + ASK_W - 20 - w;
    gui_hint(pen, fy, "ESC", "Leave it alone", 0, 0);
    add_hit(pen, fy - 2, w, CV_LINE + 4, HIT_CANCEL);
    add_hit(ASK_X, ASK_Y, ASK_W, ASK_H, HIT_PANEL);
}

void edit_draw(void) {
    if (E.panel == PANEL_NONE)
        return;
    E.nhit = 0;
    cv_scrim(0, 0, SCR_W, SCR_H, 2, 0); /* everything behind it goes quiet */
    switch (E.panel) {
    case PANEL_MENU:
        draw_menu();
        break;
    case PANEL_TITLE:
    case PANEL_SHELF:
        draw_form();
        break;
    case PANEL_BROWSE:
        draw_browse();
        break;
    case PANEL_DEST:
        draw_dest();
        break;
    case PANEL_ADD:
        draw_add();
        break;
    case PANEL_CONFIRM:
        draw_ask();
        break;
    default:
        break;
    }
}
