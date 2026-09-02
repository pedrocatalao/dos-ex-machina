/* coreload.h — opening a game core that was shipped as a loadable module.
 *
 * A core reaches DXM one of two ways: linked in at build time (what the dev
 * build does with skyroads_core), or downloaded as a .dxm module and opened
 * here.  Both end up as the same three function pointers, so nothing past
 * this file needs to know which happened. */
#ifndef DXM_CORELOAD_H
#define DXM_CORELOAD_H
#include <stddef.h>
#include "dxm_core.h"

typedef struct {
    void                 *handle;      /* dlopen/LoadLibrary handle       */
    const dxm_core_info  *info;
    dxm_core_get_info_fn  get_info;
    dxm_core_main_fn      main_fn;
    dxm_core_audio_fn     audio_fn;
} dxm_module;

/* Open a module and resolve its three entry points.  Returns 0 on success;
 * on failure fills `err` with something worth showing a user and leaves the
 * module untouched.  Refuses a module whose ABI this build does not speak -
 * that check is the whole reason dxm_core_info carries `abi` first. */
int  coreload_open(dxm_module *m, const char *path, char *err, size_t errsz);
void coreload_close(dxm_module *m);

#endif
