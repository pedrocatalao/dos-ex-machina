/* coreload.c — see coreload.h.
 *
 * Everything platform-specific about loading a module is confined here: two
 * calls on POSIX, two on Windows, and one place that knows the difference. */
#include "coreload.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#  define MOD_OPEN(p)     ((void *)LoadLibraryA(p))
#  define MOD_SYM(h,n)    ((void *)GetProcAddress((HMODULE)(h),(n)))
#  define MOD_CLOSE(h)    FreeLibrary((HMODULE)(h))
#else
#  include <dlfcn.h>
/* RTLD_LOCAL matters: two game modules will both have a `Road_Dat` or an
 * `Time`, and the second one loaded must not bind to the first one's copy.
 * The modules are built with hidden visibility as well - this is the belt
 * to that pair of braces. */
#  define MOD_OPEN(p)     dlopen((p), RTLD_NOW | RTLD_LOCAL)
#  define MOD_SYM(h,n)    dlsym((h),(n))
#  define MOD_CLOSE(h)    dlclose(h)
#endif

static void oops(char *err, size_t n, const char *fmt, const char *a) {
    if (err && n) snprintf(err, n, fmt, a ? a : "");
}

int coreload_open(dxm_module *m, const char *path, char *err, size_t errsz) {
    memset(m, 0, sizeof *m);
    void *h = MOD_OPEN(path);
    if (!h) {
#ifdef _WIN32
        char buf[32]; snprintf(buf, sizeof buf, "error %lu", GetLastError());
        oops(err, errsz, "cannot open module: %s", buf);
#else
        oops(err, errsz, "cannot open module: %s", dlerror());
#endif
        return -1;
    }
    m->handle   = h;
    m->get_info = (dxm_core_get_info_fn)MOD_SYM(h, DXM_SYM_INFO);
    m->main_fn  = (dxm_core_main_fn)    MOD_SYM(h, DXM_SYM_MAIN);
    m->audio_fn = (dxm_core_audio_fn)   MOD_SYM(h, DXM_SYM_AUDIO);
    if (!m->get_info || !m->main_fn || !m->audio_fn) {
        oops(err, errsz, "not a DXM core: missing %s",
             !m->get_info ? DXM_SYM_INFO :
             !m->main_fn  ? DXM_SYM_MAIN : DXM_SYM_AUDIO);
        MOD_CLOSE(h); memset(m, 0, sizeof *m);
        return -1;
    }
    m->info = m->get_info();
    if (!m->info) {
        oops(err, errsz, "%s returned nothing", DXM_SYM_INFO);
        MOD_CLOSE(h); memset(m, 0, sizeof *m);
        return -1;
    }
    /* The point of the version field.  A module is built somewhere else, at
     * some other time, against some other copy of dxm_core.h - so say which
     * way the mismatch runs rather than just refusing. */
    if (m->info->abi != DXM_ABI) {
        char buf[96];
        snprintf(buf, sizeof buf, "module speaks ABI %d, this DXM speaks %d - %s",
                 m->info->abi, DXM_ABI,
                 m->info->abi > DXM_ABI ? "the machine needs updating"
                                        : "the game needs rebuilding");
        oops(err, errsz, "%s", buf);
        MOD_CLOSE(h); memset(m, 0, sizeof *m);
        return -1;
    }
    return 0;
}

void coreload_close(dxm_module *m) {
    if (m && m->handle) MOD_CLOSE(m->handle);
    if (m) memset(m, 0, sizeof *m);
}
