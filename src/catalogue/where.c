/* where.c — see where.h. */
#include "where.h"
#include "catalogue.h"
#include "log.h"
#include <stdio.h>
#include <string.h>

/* Every title of every catalogue the machine holds could be installed at
 * once, which is the most this can ever need. */
#define WHERE_MAX (CAT_LIST * 64)

typedef struct {
    char key[CAT_ID * 3]; /* CATALOGUE/CATEGORY/ID, which is DOS names throughout */
    char dos[WHERE_PATH];
    int made;
} entry;

static entry E[WHERE_MAX];
static int n;
static char file_path[1200];

static void make_key(const char *catalogue, const char *category, const char *id, char *out,
                     size_t n_out) {
    snprintf(out, n_out, "%s/%s/%s", catalogue, category, id);
}

static int find(const char *key) {
    for (int i = 0; i < n; i++)
        if (!strcmp(E[i].key, key))
            return i;
    return -1;
}

static void save(void) {
    if (!file_path[0])
        return;
    FILE *f = fopen(file_path, "w");
    if (!f) {
        dxm_log("catalog: cannot write %s", file_path);
        return;
    }
    fprintf(f, "# DOS ex Machina: where each title ended up on this machine.\n"
               "# The catalogues say what a title is; this says where it is.\n"
               "# `made` is a directory the machine created, `found` one it was\n"
               "# pointed at.  Safe to delete: nothing here cannot be pointed again.\n");
    for (int i = 0; i < n; i++)
        fprintf(f, "%s = %s %s\n", E[i].key, E[i].made ? "made" : "found", E[i].dos);
    fclose(f);
}

void where_load(const char *path) {
    n = 0;
    snprintf(file_path, sizeof file_path, "%s", path ? path : "");
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f)
        return; /* nothing installed yet, which is not a fault */
    char line[400];
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');
        if (hash)
            *hash = 0;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        char *key = line, *value = eq + 1;
        while (*key == ' ' || *key == '\t')
            key++;
        for (char *p = key + strlen(key); p > key && (p[-1] == ' ' || p[-1] == '\t');)
            *--p = 0;
        while (*value == ' ' || *value == '\t')
            value++;
        int made = 0;
        if (!strncmp(value, "made ", 5)) {
            made = 1;
            value += 5;
        } else if (!strncmp(value, "found ", 6))
            value += 6;
        else
            continue; /* a line in a shape this does not write */
        while (*value == ' ')
            value++;
        for (char *p = value + strlen(value);
             p > value && (p[-1] == '\n' || p[-1] == '\r' || p[-1] == ' ' || p[-1] == '\t');)
            *--p = 0;
        if (!*key || !*value || n >= WHERE_MAX)
            continue;
        snprintf(E[n].key, sizeof E[n].key, "%s", key);
        snprintf(E[n].dos, sizeof E[n].dos, "%s", value);
        E[n].made = made;
        n++;
    }
    fclose(f);
    dxm_log("catalog: %d titles are somewhere, per %s", n, file_path);
}

const char *where_get(const char *catalogue, const char *category, const char *id) {
    char key[CAT_ID * 3];
    make_key(catalogue, category, id, key, sizeof key);
    int at = find(key);
    return at < 0 ? NULL : E[at].dos;
}

int where_made(const char *catalogue, const char *category, const char *id) {
    char key[CAT_ID * 3];
    make_key(catalogue, category, id, key, sizeof key);
    int at = find(key);
    return at >= 0 && E[at].made;
}

void where_set(const char *catalogue, const char *category, const char *id, const char *dos,
               int made) {
    char key[CAT_ID * 3];
    make_key(catalogue, category, id, key, sizeof key);
    int at = find(key);
    if (at < 0) {
        if (n >= WHERE_MAX) {
            dxm_log("catalog: no room to remember where %s went", key);
            return;
        }
        at = n++;
        snprintf(E[at].key, sizeof E[at].key, "%s", key);
    }
    snprintf(E[at].dos, sizeof E[at].dos, "%s", dos);
    E[at].made = made != 0;
    save();
}

static void drop(int at) {
    for (int i = at; i < n - 1; i++)
        E[i] = E[i + 1];
    n--;
}

void where_forget(const char *catalogue, const char *category, const char *id) {
    char key[CAT_ID * 3];
    make_key(catalogue, category, id, key, sizeof key);
    int at = find(key);
    if (at < 0)
        return;
    drop(at);
    save();
}

void where_forget_catalogue(const char *catalogue) {
    char head[CAT_ID + 2];
    snprintf(head, sizeof head, "%s/", catalogue);
    size_t k = strlen(head);
    int went = 0;
    for (int i = n - 1; i >= 0; i--)
        if (!strncmp(E[i].key, head, k)) {
            drop(i);
            went++;
        }
    if (went)
        save();
}
