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
