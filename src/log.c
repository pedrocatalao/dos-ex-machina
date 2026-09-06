/* log.c — see log.h. */
#include "log.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *g_log;

void log_open(const char *pref_dir) {
    char lp[1024];
    snprintf(lp, sizeof lp, "%sdxm.log", pref_dir ? pref_dir : "./");
    g_log = fopen(lp, "w");
}
void log_close(void) {
    if (g_log)
        fclose(g_log);
    g_log = NULL;
}
void dxm_log(const char *fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    unsigned long long ms = SDL_GetTicksNS() / 1000000ull;
    fprintf(stderr, "[dxm] %6llu ms  %s\n", ms, line);
    if (g_log) {
        fprintf(g_log, "%6llu ms  %s\n", ms, line);
        fflush(g_log);
    }
}
