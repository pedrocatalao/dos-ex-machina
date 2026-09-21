/* dosname.c — see dosname.h. */
#include "dosname.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* what DOS will keep in a name, once it is upper case */
static int dos_legal(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("_-!#$%&'()@^{}~", c) != NULL;
}

void dos_name(const char *in, char *out, size_t n) {
    char stem[16] = "", ext[8] = "";
    size_t sn = 0, en = 0;
    int after_dot = 0;
    for (const char *p = in; *p; p++) {
        char c = (char)toupper((unsigned char)*p);
        if (c == ' ')
            continue; /* DOS drops these outright */
        if (c == '.') {
            if (after_dot)
                continue; /* a second dot makes the whole name unreachable */
            after_dot = 1;
            continue;
        }
        if (!dos_legal(c))
            continue;
        if (after_dot) {
            if (en < 3)
                ext[en++] = c;
        } else if (sn < 8)
            stem[sn++] = c;
    }
    stem[sn] = 0;
    ext[en] = 0;
    if (!sn) /* nothing of it survived: it still needs to be called something */
        snprintf(stem, sizeof stem, "X");
    if (en)
        snprintf(out, n, "%s.%s", stem, ext);
    else
        snprintf(out, n, "%s", stem);
}

int dos_reachable(const char *name) {
    char made[DOS_NAME];
    dos_name(name, made, sizeof made);
    /* strcasecmp is POSIX and _stricmp is Windows; the comparison is over
     * names this has already reduced to A-Z, 0-9 and a little punctuation,
     * so doing it here keeps the module free of both. */
    const char *a = made, *b = name;
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return 0;
    return *a == *b;
}

/* ---- paths made of those names ---------------------------------------- */

const char *dos_leaf(const char *path) {
    /* Either may be absent, and comparing two pointers that are not into
     * the same object is not something C defines. */
    const char *slash = strrchr(path, '/'), *back = strrchr(path, '\\');
    const char *at = (back && (!slash || back > slash)) ? back : slash;
    return at ? at + 1 : path;
}

void dos_trim_sep(char *p) {
    size_t k = strlen(p);
    while (k > 3 && (p[k - 1] == '/' || p[k - 1] == '\\'))
        p[--k] = 0;
}

int dos_up(const char *at, char *out, size_t n) {
    char p[512];
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

void dos_upper(char *p) {
    for (; *p; p++)
        *p = (char)toupper((unsigned char)*p);
}

int dos_is_program(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot)
        return 0;
    const char *ext[] = {".EXE", ".COM", ".BAT"};
    for (int i = 0; i < 3; i++) {
        const char *a = dot, *b = ext[i];
        while (*a && *b && toupper((unsigned char)*a) == (unsigned char)*b)
            a++, b++;
        if (!*a && !*b)
            return 1;
    }
    return 0;
}
