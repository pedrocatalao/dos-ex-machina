/* Load, run, unload, reload, and run a synthetic module. This catches the
 * Windows CRT/SEH unwind failure that a build-only module check cannot see. */
#include "check.h"
#include "coreload.h"
#include <stdio.h>

static void run_once(const char *path) {
    dxm_module m;
    char err[256] = {0};
    CHECK(coreload_open(&m, path, err, sizeof err) == 0);
    if (!m.handle) {
        fprintf(stderr, "coreload_open: %s\n", err);
        return;
    }
    CHECK(m.info != NULL);
    CHECK(m.info->abi == DXM_ABI);
    CHECK(m.main_fn(NULL, "") == 73);
    coreload_close(&m);
    CHECK(m.handle == NULL);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (argc != 2)
        return check_done("unwind");
    run_once(argv[1]);
    run_once(argv[1]);
    return check_done("unwind");
}