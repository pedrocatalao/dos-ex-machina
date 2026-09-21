/* catalogue.c — see catalogue.h.
 *
 * The JSON reader is small and dumb on purpose: it walks the text looking
 * for the keys this one schema uses, at the depth it uses them.  A general
 * parser would be more code and would still have to be told the schema.  It
 * ignores what it does not recognise, which is what an older machine wants
 * when a newer catalogue adds a field.  It came from version 1 and reads
 * the same way. */
#include "catalogue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Folder names on a DOS drive, so eight characters at the most and upper
 * case: a title installs under \DXM\<catalogue>\<category>\<id>, and every
 * part of that has to be a name DOS can hold. */
const char *const cat_categories[] = {"GAMES", "TOOLS", "LEARNING", "MUSIC", "DEMOS", "MISC", NULL};

int cat_category_ok(const char *word) {
    for (int i = 0; cat_categories[i]; i++)
        if (!strcmp(word, cat_categories[i]))
            return 1;
    return 0;
}

int cat_id_ok(const char *word) {
    size_t n = strlen(word);
    if (n < 1 || n > 8)
        return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)word[i];
        if (!(isupper(c) || isdigit(c) || strchr("_-!#$%&'()@^{}~", c)))
            return 0;
    }
    return 1;
}

/* ---- the small JSON reader ------------------------------------------- */

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    return p;
}

/* Copy a JSON string starting at the opening quote.  The escapes a name or
 * a URL can carry are handled; \u is passed through, which is wrong, and
 * no catalogue of ours writes one. */
static const char *read_str(const char *p, char *out, size_t outsz) {
    if (*p != '"')
        return NULL;
    p++;
    size_t k = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p) {
            char e = *p++;
            c = (e == 'n') ? '\n' : (e == 't') ? '\t' : e;
        }
        if (out && k + 1 < outsz)
            out[k++] = c;
    }
    if (out)
        out[k] = 0;
    return *p == '"' ? p + 1 : NULL;
}

/* Step over any value, so a map can be walked without knowing its contents. */
static const char *skip_value(const char *p) {
    p = skip_ws(p);
    if (*p == '"')
        return read_str(p, NULL, 0);
    if (*p == '{' || *p == '[') {
        char open = *p, close = (open == '{') ? '}' : ']';
        int depth = 0;
        while (*p) {
            if (*p == '"') {
                p = read_str(p, NULL, 0);
                if (!p)
                    return NULL;
                continue;
            }
            if (*p == open)
                depth++;
            else if (*p == close && --depth == 0)
                return p + 1;
            p++;
        }
        return NULL;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']')
        p++;
    return p;
}

/* "key" at the top level of the object at `obj`, or NULL.  Top level only,
 * which is what keeps this honest: it cannot match a key nested deeper. */
static const char *member(const char *obj, const char *key) {
    obj = skip_ws(obj);
    if (*obj != '{')
        return NULL;
    const char *p = obj + 1;
    for (;;) {
        p = skip_ws(p);
        if (*p == '}' || !*p)
            return NULL;
        char name[64];
        const char *q = read_str(p, name, sizeof name);
        if (!q)
            return NULL;
        q = skip_ws(q);
        if (*q != ':')
            return NULL;
        q = skip_ws(q + 1);
        if (!strcmp(name, key))
            return q;
        q = skip_value(q);
        if (!q)
            return NULL;
        q = skip_ws(q);
        if (*q == ',')
            q++;
        p = q;
    }
}

/* Whether a string member is present, and its text.  Present and too long
 * is a fault the caller wants to know about, so the length is reported
 * rather than the copy silently cut. */
static int get_str(const char *obj, const char *key, char *out, size_t n, size_t *len) {
    const char *v = member(obj, key);
    if (!v || *v != '"')
        return 0;
    const char *end = read_str(v, out, n);
    if (len)
        *len = end ? (size_t)(end - v - 2) : n; /* the quotes are not counted */
    return end != NULL;
}
static long get_num(const char *obj, const char *key, int *present) {
    const char *v = member(obj, key);
    if (present)
        *present = (v && (isdigit((unsigned char)*v) || *v == '-'));
    return v ? strtol(v, NULL, 10) : 0;
}
/* -1 for absent or not a boolean */
static int get_bool(const char *obj, const char *key) {
    const char *v = member(obj, key);
    if (!v)
        return -1;
    if (!strncmp(v, "true", 4))
        return 1;
    if (!strncmp(v, "false", 5))
        return 0;
    return -1;
}
static int get_words(const char *obj, const char *key, char out[][CAT_WORD]) {
    const char *v = member(obj, key);
    int n = 0;
    if (!v || *v != '[')
        return 0;
    v++;
    while (n < CAT_WORDS) {
        v = skip_ws(v);
        if (*v != '"')
            break;
        v = read_str(v, out[n++], CAT_WORD);
        if (!v)
            break;
        v = skip_ws(v);
        if (*v != ',')
            break;
        v++;
    }
    return n;
}
static int get_file(const char *obj, const char *key, cat_file *f) {
    const char *v = member(obj, key);
    if (!v || *v != '{')
        return 0;
    get_str(v, "url", f->url, sizeof f->url, NULL);
    get_str(v, "sha256", f->sha256, sizeof f->sha256, NULL);
    f->size = get_num(v, "size", NULL);
    return 1;
}

static int hex64(const char *s) {
    if (strlen(s) != 64)
        return 0;
    for (int i = 0; i < 64; i++)
        if (!isxdigit((unsigned char)s[i]))
            return 0;
    return 1;
}

/* Walk a JSON array of objects, calling `each` on every one until it says
 * stop.  Returns -1 when `arr` is not an array. */
static int each_object(const char *arr, int (*each)(const char *obj, void *ud), void *ud) {
    arr = skip_ws(arr);
    if (*arr != '[')
        return -1;
    const char *p = arr + 1;
    for (;;) {
        p = skip_ws(p);
        if (*p == ']' || !*p)
            return 0;
        if (*p != '{')
            return -1;
        if (!each(p, ud))
            return 0;
        p = skip_value(p);
        if (!p)
            return -1;
        p = skip_ws(p);
        if (*p == ',')
            p++;
    }
}

/* ---- the catalogue ---------------------------------------------------- */

static void note(char notes[][160], int *n, const char *who, const char *what) {
    if (*n < CAT_NOTES)
        snprintf(notes[(*n)++], 160, "%s: %s", who[0] ? who : "(no id)", what);
}

/* What a title has to be.  Kept apart from the reading of it so that the
 * one set of rules holds whatever the title came from: a curator's file, or
 * the editor on this machine, which will not save a title this rejects.
 * The first fault is the one reported; it is fixed and the next shows. */
const char *cat_title_fault(const cat_title *t, int holds) {
    if (!t->id[0])
        return "no id";
    if (!cat_id_ok(t->id))
        return "id is not a DOS name (1-8 of A-Z 0-9, upper case)";
    if (!t->name[0])
        return "no name";
    if (!t->creator[0])
        return "no creator";
    if (t->year < 1980 || t->year > 2100)
        return "year missing or not a year";
    if (!cat_category_ok(t->category))
        return "category missing or not one of GAMES TOOLS LEARNING MUSIC DEMOS MISC";
    if (t->multiplayer < 0 || t->network < 0)
        return "multiplayer and network must both be true or false";
    if (!t->run[0])
        return "no run: the machine has to be told what to start";
    if (t->video[0] && strcmp(t->video, "CGA") && strcmp(t->video, "EGA") &&
        strcmp(t->video, "VGA") && strcmp(t->video, "SVGA"))
        return "video is not CGA, EGA, VGA or SVGA";
    /* An archive that is an installer carries the title as an archive of its
     * own - a zip, or a zip that extracts itself - and this names it, as a
     * path inside it.  Relative, and going nowhere above it. */
    if (t->archive[0] && (t->archive[0] == '/' || t->archive[0] == '\\' ||
                          strstr(t->archive, "..") || strchr(t->archive, ':')))
        return "archive is not a relative path inside the download";
    /* The catalogue's kind decides this, and decides it both ways: an
     * internet catalogue's title is worth nothing without the address and
     * the hash, and a disk catalogue's title has no business carrying one -
     * whatever is on this computer got here some other way. */
    if (holds == CAT_DISK) {
        if (t->download.url[0])
            return "a catalogue of what is on this computer cannot hold a download";
    } else {
        if (!t->download.url[0])
            return "no download";
        if (strncmp(t->download.url, "https://", 8) && strncmp(t->download.url, "http://", 7))
            return "download.url is not http(s)";
        if (!hex64(t->download.sha256))
            return "download.sha256 is not 64 hex digits";
        if (t->download.size <= 0)
            return "download.size missing";
    }
    /* Artwork may be an address, or a hash with no address at all: a
     * picture chosen off the machine's own disk has nowhere to be fetched
     * from and is known by its content.  So the hash is checked either
     * way, and the address only when there is one. */
    if (t->artwork.url[0] && strncmp(t->artwork.url, "https://", 8) &&
        strncmp(t->artwork.url, "http://", 7))
        return "artwork.url is not http(s)";
    if (t->artwork.sha256[0] && !hex64(t->artwork.sha256))
        return "artwork.sha256 is not 64 hex digits";
    return NULL;
}

const char *cat_catalogue_fault(const cat_catalogue *c) {
    if (c->format != 1)
        return "a format this machine does not read";
    if (!c->id[0] || !cat_id_ok(c->id))
        return "the catalogue's id is missing or not a DOS name";
    if (!c->name[0])
        return "the catalogue has no name";
    if (c->holds != CAT_INTERNET && c->holds != CAT_DISK)
        return "the catalogue holds neither internet nor disk titles";
    return NULL;
}

/* One title out of the text.  Everything present is taken; a field longer
 * than the table holds is a fault of its own, since the copy would be a
 * different string and not what the curator wrote.  Whether what came out
 * is a usable title is cat_title_fault's business. */
static const char *read_title(const char *obj, cat_title *t, int holds) {
    size_t len;
    memset(t, 0, sizeof *t);
    if (get_str(obj, "id", t->id, sizeof t->id, &len) && len > 8)
        return "id longer than 8";
    if (get_str(obj, "name", t->name, sizeof t->name, &len) && len > CAT_NAME - 1)
        return "name longer than 64";
    if (get_str(obj, "creator", t->creator, sizeof t->creator, &len) && len > 79)
        return "creator longer than 79";
    t->year = (int)get_num(obj, "year", NULL);
    if (get_str(obj, "category", t->category, sizeof t->category, &len) && len > 8)
        return "category longer than 8";
    t->multiplayer = get_bool(obj, "multiplayer");
    t->network = get_bool(obj, "network");
    if (get_str(obj, "run", t->run, sizeof t->run, &len) && len > 63)
        return "run longer than 63";
    get_file(obj, "download", &t->download);
    if (get_str(obj, "publisher", t->publisher, sizeof t->publisher, &len) && len > 79)
        return "publisher longer than 79";
    if (get_str(obj, "version", t->version, sizeof t->version, &len) && len > 15)
        return "version longer than 15";
    if (get_str(obj, "genre", t->genre, sizeof t->genre, &len) && len > 31)
        return "genre longer than 31";
    if (get_str(obj, "description", t->description, sizeof t->description, &len) &&
        len > CAT_DESC - 1)
        return "description longer than 400";
    get_str(obj, "video", t->video, sizeof t->video, NULL);
    get_words(obj, "sound", t->sound);
    get_words(obj, "controls", t->controls);
    if (get_str(obj, "setup", t->setup, sizeof t->setup, &len) && len > 63)
        return "setup longer than 63";
    if (get_str(obj, "archive", t->archive, sizeof t->archive, &len)) {
        if (len > 63)
            return "archive longer than 63";
        /* written, and written empty: the curator meant something by the
         * key and it says nothing.  Absent is what "no inner archive"
         * looks like, and only the reader can tell the two apart. */
        if (!t->archive[0])
            return "archive is not a relative path inside the download";
    }
    get_file(obj, "artwork", &t->artwork);
    return cat_title_fault(t, holds);
}

static int each_title(const char *obj, void *ud) {
    cat_catalogue *c = ud;
    if (c->n >= CAT_TITLES) {
        note(c->notes, &c->n_notes, "", "more titles than the machine holds; the rest dropped");
        return 0;
    }
    cat_title *t = &c->titles[c->n];
    const char *why = read_title(obj, t, c->holds);
    if (why) {
        note(c->notes, &c->n_notes, t->id, why);
        return 1;
    }
    for (int i = 0; i < c->n; i++)
        if (!strcmp(c->titles[i].id, t->id) && !strcmp(c->titles[i].category, t->category)) {
            note(c->notes, &c->n_notes, t->id, "same id twice in one category");
            return 1;
        }
    c->n++;
    return 1;
}

int cat_parse(cat_catalogue *c, const char *json) {
    memset(c, 0, sizeof *c);
    int present;
    c->format = (int)get_num(json, "format", &present);
    const char *arr = member(json, "titles");
    if (!present || !arr)
        return -1;
    /* what it is: without these it is a list of titles and nothing else */
    size_t len = 0;
    char origin[16] = "", holds[16] = "";
    get_str(json, "id", c->id, sizeof c->id, &len);
    if (len > 8)
        c->id[0] = 0; /* not the id that was written: treat it as missing */
    /* What kind of titles are in it.  Said nothing, it is the kind there
     * used to be only one of. */
    if (get_str(json, "holds", holds, sizeof holds, NULL)) {
        if (!strcmp(holds, "disk"))
            c->holds = CAT_DISK;
        else if (strcmp(holds, "internet")) {
            note(c->notes, &c->n_notes, c->id, "holds is neither internet nor disk");
            return -1;
        }
    }
    /* A catalogue with no name of its own is labelled with its id rather
     * than turned away: the tab needs something on it, and the id is a
     * word somebody chose. */
    if (!get_str(json, "name", c->name, sizeof c->name, NULL) || !c->name[0])
        snprintf(c->name, sizeof c->name, "%s", c->id);
    const char *why = cat_catalogue_fault(c);
    if (!why && (!get_str(json, "origin", origin, sizeof origin, NULL) ||
                 (strcmp(origin, "bundled") && strcmp(origin, "community"))))
        why = "the catalogue's origin is not bundled or community";
    if (why) {
        note(c->notes, &c->n_notes, c->id, why);
        return -1;
    }
    c->community = !strcmp(origin, "community");
    get_str(json, "about", c->about, sizeof c->about, NULL);
    get_str(json, "updated", c->updated, sizeof c->updated, NULL);
    if (each_object(arr, each_title, c) < 0)
        return -1;
    return c->n;
}

/* ---- files ------------------------------------------------------------ */

/* A catalogue is text; a megabyte of it is more than the tables hold. */
#define CAT_FILE_MAX (1 << 20)

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (n >= 0 && n <= CAT_FILE_MAX) ? malloc((size_t)n + 1) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n)
        buf[n] = 0;
    else {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

int cat_read(cat_catalogue *c, const char *path) {
    char *json = slurp(path);
    if (!json) {
        memset(c, 0, sizeof *c);
        return -1;
    }
    int n = cat_parse(c, json);
    free(json);
    return n;
}

/* ---- writing one back out --------------------------------------------- */

/* The same JSON a person would write: two spaces an level, one field a
 * line, and nothing emitted that was not set - so a catalogue this machine
 * saves reads the same anywhere else, and a field it does not know about
 * was never there rather than having been dropped. */
static void w_str(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            fputc('\\', f);
            fputc((int)c, f);
        } else if (c == '\n')
            fputs("\\n", f);
        else if (c == '\t')
            fputs("\\t", f);
        else if (c < 0x20)
            fprintf(f, "\\u%04x",
                    c); /* the reader passes \u through, and none of ours writes one */
        else
            fputc((int)c, f);
    }
    fputc('"', f);
}

/* a field, with the comma that goes before it rather than after: what makes
 * the last one in an object come out without a trailing comma */
static void w_field(FILE *f, int *first, int indent, const char *key) {
    fputs(*first ? "\n" : ",\n", f);
    *first = 0;
    for (int i = 0; i < indent; i++)
        fputc(' ', f);
    w_str(f, key);
    fputs(": ", f);
}

static void w_text(FILE *f, int *first, int indent, const char *key, const char *value) {
    if (!value[0])
        return;
    w_field(f, first, indent, key);
    w_str(f, value);
}

static void w_num(FILE *f, int *first, int indent, const char *key, long value) {
    w_field(f, first, indent, key);
    fprintf(f, "%ld", value);
}

static void w_bool(FILE *f, int *first, int indent, const char *key, int value) {
    w_field(f, first, indent, key);
    fputs(value ? "true" : "false", f);
}

static void w_words(FILE *f, int *first, int indent, const char *key,
                    const char (*list)[CAT_WORD]) {
    if (!list[0][0])
        return;
    w_field(f, first, indent, key);
    fputc('[', f);
    for (int i = 0; i < CAT_WORDS && list[i][0]; i++) {
        fputs(i ? ",\n" : "\n", f);
        for (int k = 0; k < indent + 2; k++)
            fputc(' ', f);
        w_str(f, list[i]);
    }
    fputc('\n', f);
    for (int k = 0; k < indent; k++)
        fputc(' ', f);
    fputc(']', f);
}

static void w_file(FILE *f, int *first, int indent, const char *key, const cat_file *file,
                   int with_size) {
    /* A hash on its own is a whole file object: artwork taken off this
     * machine's disk has one and no address (§8.5). */
    if (!file->url[0] && !file->sha256[0])
        return;
    w_field(f, first, indent, key);
    fputs("{", f);
    int inner = 1;
    w_text(f, &inner, indent + 2, "url", file->url);
    if (with_size && file->size > 0)
        w_num(f, &inner, indent + 2, "size", file->size);
    w_text(f, &inner, indent + 2, "sha256", file->sha256);
    fputc('\n', f);
    for (int k = 0; k < indent; k++)
        fputc(' ', f);
    fputc('}', f);
}

static void w_title(FILE *f, const cat_title *t) {
    int first = 1;
    fputs("    {", f);
    w_text(f, &first, 6, "id", t->id);
    w_text(f, &first, 6, "name", t->name);
    w_text(f, &first, 6, "creator", t->creator);
    w_text(f, &first, 6, "publisher", t->publisher);
    w_num(f, &first, 6, "year", t->year);
    w_text(f, &first, 6, "version", t->version);
    w_text(f, &first, 6, "category", t->category);
    w_text(f, &first, 6, "genre", t->genre);
    w_text(f, &first, 6, "description", t->description);
    w_bool(f, &first, 6, "multiplayer", t->multiplayer > 0);
    w_bool(f, &first, 6, "network", t->network > 0);
    w_text(f, &first, 6, "video", t->video);
    w_words(f, &first, 6, "sound", t->sound);
    w_words(f, &first, 6, "controls", t->controls);
    w_text(f, &first, 6, "run", t->run);
    w_text(f, &first, 6, "setup", t->setup);
    w_text(f, &first, 6, "archive", t->archive);
    /* A disk catalogue's titles have none of this, and the field is simply
     * not written rather than written empty. */
    w_file(f, &first, 6, "download", &t->download, 1);
    w_file(f, &first, 6, "artwork", &t->artwork, 0);
    fputs("\n    }", f);
}

int cat_write(const cat_catalogue *c, const char *path) {
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s.new", path);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return -1;
    int first = 1;
    fputc('{', f);
    w_num(f, &first, 2, "format", 1);
    w_text(f, &first, 2, "id", c->id);
    w_text(f, &first, 2, "name", c->name);
    w_text(f, &first, 2, "origin", c->community ? "community" : "bundled");
    if (c->holds == CAT_DISK) /* the other kind is what saying nothing means */
        w_text(f, &first, 2, "holds", "disk");
    w_text(f, &first, 2, "about", c->about);
    w_text(f, &first, 2, "updated", c->updated);
    w_field(f, &first, 2, "titles");
    fputc('[', f);
    for (int i = 0; i < c->n; i++) {
        fputs(i ? ",\n" : "\n", f);
        w_title(f, &c->titles[i]);
    }
    fputs(c->n ? "\n  ]\n}\n" : "]\n}\n", f);
    int bad = ferror(f);
    if (fclose(f) != 0 || bad) {
        remove(tmp);
        return -1;
    }
    /* The old one is replaced only once the new one is whole on the disk.
     * rename() will not write over an existing file on Windows, so the old
     * one goes first; a machine that stops in that instant is left with no
     * catalogue rather than with half of one, and a bundled catalogue can
     * always be got back by resetting it. */
    remove(path);
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}
