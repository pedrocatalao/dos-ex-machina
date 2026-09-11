/* Synthetic DXM module used by test_unwind.c. It exercises the contract's
 * non-local exit boundary without depending on a real game. */
#include "dxm_core.h"
#include <string.h>

static const dxm_core_info INFO = {
    .abi = DXM_ABI,
    .id = "unwind-test",
    .exe_name = "UNWIND.COM",
    .title = "DXM unwind test",
    .publisher = "DXM",
    .year = 2026,
    .modes = NULL,
    .n_modes = 0,
    .data_probe = NULL,
};
static dxm_exit_buf exit_target;

static void leave_core(void) {
    DXM_EXIT_LONGJMP(exit_target);
}

DXM_EXPORT const dxm_core_info *dxm_core_get_info(void) {
    return &INFO;
}

DXM_EXPORT int dxm_core_main(const dxm_host *host, const char *data_dir) {
    (void)host;
    (void)data_dir;
    if (DXM_EXIT_SETJMP(&exit_target))
        return 73;
    leave_core();
    return -1;
}

DXM_EXPORT void dxm_core_audio(int16_t *out, int frames) {
    if (out && frames > 0)
        memset(out, 0, (size_t)frames * 2U * sizeof *out);
}