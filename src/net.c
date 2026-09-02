/* net.c — fetching things over HTTP.
 *
 * libcurl rather than sockets, for two reasons that are not negotiable:
 * every URL involved is TLS, and GitHub's release downloads redirect to a
 * different host. Writing either of those by hand would be a mistake. */
#include "net.h"
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int inited;

void net_init(void) {
    if (!inited) { curl_global_init(CURL_GLOBAL_DEFAULT); inited = 1; }
}
void net_quit(void) {
    if (inited) { curl_global_cleanup(); inited = 0; }
}

typedef struct { net_progress cb; void *ud; volatile int *cancel; } prog;

static int on_progress(void *p, curl_off_t dt, curl_off_t dn,
                       curl_off_t ut, curl_off_t un) {
    (void)ut; (void)un;
    prog *g = p;
    if (g->cancel && *g->cancel) return 1;          /* non-zero aborts */
    if (g->cb) g->cb(g->ud, (double)dn, (double)dt);
    return 0;
}

static CURL *setup(const char *url, prog *g, char *err, size_t errsz) {
    CURL *c = curl_easy_init();
    if (!c) { snprintf(err, errsz, "curl unavailable"); return NULL; }
    curl_easy_setopt(c, CURLOPT_URL, url);
    /* Release assets redirect to a different host - without this every
     * download returns a 302 body instead of the file. */
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);   /* 404 is a failure */
    curl_easy_setopt(c, CURLOPT_USERAGENT, "dos-ex-machina");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    /* No total timeout: a slow connection on a 6 MB download is not an
     * error.  Instead, give up if it stalls under 1 KB/s for 30s. */
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, on_progress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, g);
    return c;
}

static size_t to_file(void *p, size_t sz, size_t n, void *ud) {
    return fwrite(p, sz, n, (FILE *)ud);
}

int net_get_file(const char *url, const char *path, net_progress cb, void *ud,
                 volatile int *cancel, char *err, size_t errsz) {
    net_init();
    /* Download to a .part and rename on success, so an interrupted fetch
     * can never leave something that looks like a finished install. */
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s.part", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { snprintf(err, errsz, "cannot write %s", tmp); return -1; }

    prog g = { cb, ud, cancel };
    CURL *c = setup(url, &g, err, errsz);
    if (!c) { fclose(f); remove(tmp); return -1; }
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, to_file);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, f);

    CURLcode r = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    fclose(f);

    if (r != CURLE_OK) {
        if (r == CURLE_ABORTED_BY_CALLBACK) snprintf(err, errsz, "cancelled");
        else if (code >= 400)               snprintf(err, errsz, "HTTP %ld", code);
        else snprintf(err, errsz, "%s", curl_easy_strerror(r));
        remove(tmp);
        return -1;
    }
    remove(path);
    if (rename(tmp, path) != 0) {
        snprintf(err, errsz, "cannot move into place");
        remove(tmp);
        return -1;
    }
    return 0;
}

typedef struct { char *p; size_t n, cap; } membuf;

static size_t to_mem(void *p, size_t sz, size_t n, void *ud) {
    membuf *m = ud;
    size_t add = sz * n;
    if (m->n + add + 1 > m->cap) {
        size_t cap = m->cap ? m->cap * 2 : 8192;
        while (cap < m->n + add + 1) cap *= 2;
        if (cap > NET_MAX_MEM) return 0;             /* refuse to grow forever */
        char *q = realloc(m->p, cap);
        if (!q) return 0;
        m->p = q; m->cap = cap;
    }
    memcpy(m->p + m->n, p, add);
    m->n += add;
    m->p[m->n] = 0;
    return add;
}

int net_get_mem(const char *url, char **out, size_t *outn,
                char *err, size_t errsz) {
    net_init();
    membuf m = { NULL, 0, 0 };
    prog g = { NULL, NULL, NULL };
    CURL *c = setup(url, &g, err, errsz);
    if (!c) return -1;
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, to_mem);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
    CURLcode r = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    if (r != CURLE_OK) {
        if (code >= 400) snprintf(err, errsz, "HTTP %ld", code);
        else snprintf(err, errsz, "%s", curl_easy_strerror(r));
        free(m.p);
        return -1;
    }
    *out = m.p; if (outn) *outn = m.n;
    return 0;
}
