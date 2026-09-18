/* keygrab.c — see keygrab.h. */
#include "keygrab.h"
#include "log.h"

#ifdef __APPLE__
#    include <dlfcn.h>
/* The window server's switch for its global hot keys: 0 lets them act, 1
 * stands them down for this connection.  Private, which is why they are
 * looked up and not declared: present on every macOS this runs on, but
 * nothing here may depend on that to start. */
typedef int (*cgs_connection_fn)(void);
typedef int (*cgs_hotkey_mode_fn)(int connection, int mode);

static void desktop_hotkeys(int off) {
    static int looked;
    static cgs_connection_fn connection;
    static cgs_hotkey_mode_fn set_mode;
    if (!looked) {
        looked = 1;
        connection = (cgs_connection_fn)dlsym(RTLD_DEFAULT, "_CGSDefaultConnection");
        if (!connection)
            connection = (cgs_connection_fn)dlsym(RTLD_DEFAULT, "CGSMainConnectionID");
        set_mode = (cgs_hotkey_mode_fn)dlsym(RTLD_DEFAULT, "CGSSetGlobalHotKeyOperatingMode");
        if (!connection || !set_mode)
            dxm_log("keys: this macOS will not stand its hot keys down; the desktop keeps them");
    }
    if (connection && set_mode && set_mode(connection(), off ? 1 : 0) != 0)
        dxm_log("keys: the window server refused to %s its hot keys",
                off ? "stand down" : "restore");
}
#else
/* SDL's keyboard grab is the whole of it here */
static void desktop_hotkeys(int off) {
    (void)off;
}
#endif

void keygrab_sync(SDL_Window *win, int machine) {
    static int held = -1; /* what was last asked for; -1 nothing yet */
    int want = machine && (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS) ? 1 : 0;
    if (want == held)
        return;
    if (held < 0 && !want) { /* nothing was ever taken: nothing to give back */
        held = 0;
        return;
    }
    held = want;
    if (!SDL_SetWindowKeyboardGrab(win, want ? true : false))
        dxm_log("keys: SDL would not %s the keyboard: %s", want ? "take" : "release",
                SDL_GetError());
    desktop_hotkeys(want);
    dxm_log("keys: the desktop's shortcuts are %s", want ? "the machine's" : "the desktop's again");
}
