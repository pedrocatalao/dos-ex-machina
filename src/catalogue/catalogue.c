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

const char *const cat_categories[] = {"GAMES", "TOOLS", "EDUCATION", "MUSIC",
                                      "DEMOS", "MISC",  NULL};

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

/* One title, checked field by field.  The first fault is the one reported;
 * a curator fixes it and sees the next. */
static const char *check_title(const char *obj, cat_title *t) {
    size_t len;
    memset(t, 0, sizeof *t);
    if (!get_str(obj, "id", t->id, sizeof t->id, &len))
        return "no id";
    if (len > 8 || !cat_id_ok(t->id))
        return "id is not a DOS name (1-8 of A-Z 0-9, upper case)";
    if (!get_str(obj, "name", t->name, sizeof t->name, &len))
        return "no name";
    if (len > CAT_NAME - 1)
        return "name longer than 64";
    if (!get_str(obj, "creator", t->creator, sizeof t->creator, &len) || len > 79)
        return "no creator, or longer than 79";
    int present;
    t->year = (int)get_num(obj, "year", &present);
    if (!present || t->year < 1980 || t->year > 2100)
        return "year missing or not a year";
    if (!get_str(obj, "category", t->category, sizeof t->category, &len) || len > 8 ||
        !cat_category_ok(t->category))
        return "category missing or not one of GAMES TOOLS EDUCATION MUSIC DEMOS MISC";
    t->multiplayer = get_bool(obj, "multiplayer");
    t->network = get_bool(obj, "network");
    if (t->multiplayer < 0 || t->network < 0)
        return "multiplayer and network must both be true or false";
    if (!get_str(obj, "run", t->run, sizeof t->run, &len) || len > 63 || !t->run[0])
        return "no run, or longer than 63";
    if (!get_file(obj, "download", &t->download))
        return "no download";
    if (strncmp(t->download.url, "https://", 8) && strncmp(t->download.url, "http://", 7))
        return "download.url is not http(s)";
    if (!hex64(t->download.sha256))
        return "download.sha256 is not 64 hex digits";
    if (t->download.size <= 0)
        return "download.size missing";

    /* the optional ones: absent is fine, present and wrong is not */
    if (get_str(obj, "publisher", t->publisher, sizeof t->publisher, &len) && len > 79)
        return "publisher longer than 79";
    if (get_str(obj, "version", t->version, sizeof t->version, &len) && len > 15)
        return "version longer than 15";
    if (get_str(obj, "genre", t->genre, sizeof t->genre, &len) && len > 31)
        return "genre longer than 31";
    if (get_str(obj, "description", t->description, sizeof t->description, &len) &&
        len > CAT_DESC - 1)
        return "description longer than 400";
    if (get_str(obj, "video", t->video, sizeof t->video, &len) &&
        (strcmp(t->video, "CGA") && strcmp(t->video, "EGA") && strcmp(t->video, "VGA") &&
         strcmp(t->video, "SVGA")))
        return "video is not CGA, EGA, VGA or SVGA";
    get_words(obj, "sound", t->sound);
    get_words(obj, "controls", t->controls);
    if (get_str(obj, "setup", t->setup, sizeof t->setup, &len) && len > 63)
        return "setup longer than 63";
    /* A download that is an installer carries the title as an archive of its
     * own - a zip, or a zip that extracts itself - and this names it, as a
     * path inside the download.  Relative, and going nowhere above it. */
    if (get_str(obj, "archive", t->archive, sizeof t->archive, &len) &&
        (len > 63 || !t->archive[0] || t->archive[0] == '/' || t->archive[0] == '\\' ||
         strstr(t->archive, "..") || strchr(t->archive, ':')))
        return "archive is not a relative path inside the download";
    if (get_file(obj, "artwork", &t->artwork)) {
        if (strncmp(t->artwork.url, "https://", 8) && strncmp(t->artwork.url, "http://", 7))
            return "artwork.url is not http(s)";
        if (t->artwork.sha256[0] && !hex64(t->artwork.sha256))
            return "artwork.sha256 is not 64 hex digits";
    }
    return NULL;
}

static int each_title(const char *obj, void *ud) {
    cat_catalogue *c = ud;
    if (c->n >= CAT_TITLES) {
        note(c->notes, &c->n_notes, "", "more titles than the machine holds; the rest dropped");
        return 0;
    }
    cat_title *t = &c->titles[c->n];
    const char *why = check_title(obj, t);
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
    if (c->format != 1) {
        note(c->notes, &c->n_notes, "", "a format this machine does not read");
        return -1;
    }
    /* where it goes: without these it is a list of titles with no home */
    size_t len;
    char origin[16] = "", drive[4] = "";
    const char *why = NULL;
    if (!get_str(json, "id", c->id, sizeof c->id, &len) || len > 8 || !cat_id_ok(c->id))
        why = "the catalogue's id is missing or not a DOS name";
    else if (!get_str(json, "drive", drive, sizeof drive, NULL) || strlen(drive) != 1 ||
             drive[0] < 'C' || drive[0] > 'Z')
        why = "the catalogue's drive is not one letter C..Z";
    else if (!get_str(json, "origin", origin, sizeof origin, NULL) ||
             (strcmp(origin, "bundled") && strcmp(origin, "community")))
        why = "the catalogue's origin is not bundled or community";
    if (why) {
        note(c->notes, &c->n_notes, c->id, why);
        return -1;
    }
    c->drive = drive[0];
    c->community = !strcmp(origin, "community");
    get_str(json, "name", c->name, sizeof c->name, NULL);
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
