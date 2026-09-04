/* catalog.c — see catalog.h.
 *
 * The JSON reader here is deliberately small and deliberately dumb: it walks
 * the text looking for the keys this one schema uses, at the nesting depth it
 * uses them.  A general parser would be more code and would still have to be
 * told the schema.  It is strict about what it accepts and silently ignores
 * what it does not recognise, which is the behaviour you want when a newer
 * catalogue adds a field an older DXM has never heard of. */
#include "catalog.h"
#include "net.h"
#include "library.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#define CAT_URL "https://raw.githubusercontent.com/pedrocatalao/" \
                "dos-ex-machina/main/catalogue.json"

static cat_game games[CAT_MAX];
static int      n_games;

const char *cat_platform(void) {
#if defined(_WIN32)
#  if defined(__aarch64__) || defined(_M_ARM64)
    return "windows-arm64";
#  else
    return "windows-x86_64";
#  endif
#elif defined(__APPLE__)
    /* macOS modules are shipped universal, so there is one key, not two. */
    return "macos-universal";
#elif defined(__aarch64__)
    return "linux-arm64";
#else
    return "linux-x86_64";
#endif
}

/* ---- the small JSON reader ------------------------------------------- */

static const char *skip_ws(const char *p) {
    while (*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++;
    return p;
}

/* Copy a JSON string starting at the opening quote.  Handles the escapes
 * that can actually appear in a URL or a title; anything else is passed
 * through, which is wrong for \u but no catalogue of ours will carry one. */
static const char *read_str(const char *p, char *out, size_t outsz) {
    if (*p != '"') return NULL;
    p++;
    size_t k = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p) {
            char e = *p++;
            c = (e=='n')?'\n' : (e=='t')?'\t' : e;
        }
        if (out && k + 1 < outsz) out[k++] = c;
    }
    if (out) out[k] = 0;
    return *p == '"' ? p + 1 : NULL;
}

/* Step over any value, so we can walk a map without knowing its contents. */
static const char *skip_value(const char *p) {
    p = skip_ws(p);
    if (*p == '"') return read_str(p, NULL, 0);
    if (*p == '{' || *p == '[') {
        char open = *p, close = (open=='{') ? '}' : ']';
        int depth = 0;
        while (*p) {
            if (*p == '"') { p = read_str(p, NULL, 0); if (!p) return NULL; continue; }
            if (*p == open) depth++;
            else if (*p == close && --depth == 0) return p + 1;
            p++;
        }
        return NULL;
    }
    while (*p && *p!=',' && *p!='}' && *p!=']') p++;
    return p;
}

/* Find "key" at the top level of the object starting at `obj`, and return a
 * pointer to its value.  Top level only - that is what keeps this honest:
 * it cannot accidentally match a key nested deeper. */
static const char *member(const char *obj, const char *key) {
    obj = skip_ws(obj);
    if (*obj != '{') return NULL;
    const char *p = obj + 1;
    for (;;) {
        p = skip_ws(p);
        if (*p == '}' || !*p) return NULL;
        char name[64];
        const char *q = read_str(p, name, sizeof name);
        if (!q) return NULL;
        q = skip_ws(q);
        if (*q != ':') return NULL;
        q = skip_ws(q + 1);
        if (!strcmp(name, key)) return q;
        q = skip_value(q);
        if (!q) return NULL;
        q = skip_ws(q);
        if (*q == ',') q++;
        p = q;
    }
}

static void get_str(const char *obj, const char *key, char *out, size_t n) {
    const char *v = member(obj, key);
    if (v && *v == '"') read_str(v, out, n);
}
static long get_num(const char *obj, const char *key) {
    const char *v = member(obj, key);
    return v ? strtol(v, NULL, 10) : 0;
}
static void get_file(const char *obj, const char *key, cat_file *f) {
    const char *v = member(obj, key);
    if (!v || *v != '{') return;
    get_str(v, "url",    f->url,    sizeof f->url);
    get_str(v, "sha256", f->sha256, sizeof f->sha256);
    f->size = get_num(v, "size");
}

static int parse(const char *json) {
    n_games = 0;
    const char *arr = member(json, "games");
    if (!arr || *arr != '[') return -1;
    const char *p = arr + 1;
    while (n_games < CAT_MAX) {
        p = skip_ws(p);
        if (*p == ']' || !*p) break;
        if (*p != '{') return -1;
        const char *g = p;
        cat_game *e = &games[n_games];
        memset(e, 0, sizeof *e);
        get_str(g, "id",    e->id,    sizeof e->id);
        get_str(g, "title", e->title, sizeof e->title);
        get_str(g, "by",    e->by,    sizeof e->by);
        get_str(g, "version", e->version, sizeof e->version);
        e->year = (int)get_num(g, "year");
        e->abi  = (int)get_num(g, "abi");
        get_file(g, "art", &e->art);

        /* the description array */
        { const char *d = member(g, "desc");
          if (d && *d == '[') {
              d++;
              for (int k = 0; k < CAT_DESC; k++) {
                  d = skip_ws(d);
                  if (*d != '"') break;
                  d = read_str(d, e->desc[k], sizeof e->desc[k]);
                  if (!d) break;
                  d = skip_ws(d);
                  if (*d == ',') d++; else break;
              }
          } }

        /* the module for THIS platform.  Absent is not an error - it means
         * the game has no build for this machine yet, which the navigator
         * shows rather than hides. */
        { const char *m = member(g, "module");
          if (m && *m == '{') {
              const char *mine = member(m, cat_platform());
              if (mine && *mine == '{') {
                  get_str(mine, "url",    e->module.url,    sizeof e->module.url);
                  get_str(mine, "sha256", e->module.sha256, sizeof e->module.sha256);
                  e->module.size = get_num(mine, "size");
                  e->have_module = e->module.url[0] != 0;
              }
          } }

        { const char *d = member(g, "data");
          if (d && *d == '{') {
              get_str(d, "url",    e->data.url,    sizeof e->data.url);
              get_str(d, "sha256", e->data.sha256, sizeof e->data.sha256);
              e->data.size = get_num(d, "size");
              get_str(d, "kind",   e->data_kind,   sizeof e->data_kind);
              get_str(d, "format", e->data_format, sizeof e->data_format);
              get_str(d, "probe",  e->data_probe,  sizeof e->data_probe);
          } }

        if (e->id[0]) n_games++;
        p = skip_value(p);
        if (!p) break;
        p = skip_ws(p);
        if (*p == ',') p++;
    }
    return n_games;
}

/* ---- disk cache ------------------------------------------------------- */

static void cache_path(char *out, size_t n) {
    snprintf(out, n, "%scatalogue.json", lib_root());
}

int cat_load_cached(void) {
    char path[LIB_PATH];
    cache_path(path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > (long)NET_MAX_MEM) { fclose(f); return -1; }
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    int r = parse(buf);
    free(buf);
    return r;
}

int cat_refresh(char *err, size_t errsz) {
    char *buf = NULL; size_t n = 0;
    if (net_get_mem(CAT_URL, &buf, &n, err, errsz) != 0) return -1;
    if (parse(buf) < 0) {
        snprintf(err, errsz, "catalogue is not readable");
        free(buf);
        return -1;
    }
    /* Only cache what parsed.  Writing first would let one bad publish
     * poison every future offline start. */
    char path[LIB_PATH];
    cache_path(path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(buf, 1, n, f); fclose(f); }
    free(buf);
    return n_games;
}

int             cat_count(void)  { return n_games; }
const cat_game *cat_at(int i)    { return (i>=0 && i<n_games) ? &games[i] : NULL; }

const cat_game *cat_find(const char *id) {
    for (int i = 0; i < n_games; i++)
        if (!strcmp(games[i].id, id)) return &games[i];
    return NULL;
}

/* ---- background refresh ------------------------------------------------
 * The fetch happens on a thread; the PARSE happens on the main thread when
 * it collects the result.  That way `games` is only ever written by one
 * thread and no lock is needed around the accessors the navigator uses on
 * every frame. */
static SDL_Thread  *rth;
static char        *pending;
static volatile int pending_ready;
static char         rerr[160];

static int SDLCALL refresh_thread(void *ud) {
    (void)ud;
    char *buf = NULL; size_t n = 0;
    if (net_get_mem(CAT_URL, &buf, &n, rerr, sizeof rerr) == 0) pending = buf;
    pending_ready = 1;
    return 0;
}

void cat_refresh_begin(void) {
    if (rth || pending_ready) return;
    rerr[0] = 0;
    rth = SDL_CreateThread(refresh_thread, "catalogue", NULL);
}

int cat_refresh_collect(void) {
    if (!pending_ready) return 0;
    if (rth) { SDL_WaitThread(rth, NULL); rth = NULL; }
    pending_ready = 0;
    if (!pending) return -1;                 /* offline; the cache still stands */
    int r = parse(pending);
    if (r >= 0) {
        char path[LIB_PATH];
        cache_path(path, sizeof path);
        FILE *f = fopen(path, "wb");
        if (f) { fwrite(pending, 1, strlen(pending), f); fclose(f); }
    }
    free(pending); pending = NULL;
    return r >= 0 ? 1 : -1;
}
