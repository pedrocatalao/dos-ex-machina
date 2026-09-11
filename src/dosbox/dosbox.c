/* dosbox.c — see dosbox.h.
 *
 * The libretro side of the machine.  The core is opened from a shared
 * library and driven from one thread of its own: retro_run() once per
 * frame at whatever rate the emulated card is scanning, the keys typed
 * since the last one delivered just before it, the frame it hands back
 * converted to RGB8 for the tube, the audio it hands back queued for the
 * device to pull.  Nothing here touches the GPU or the window; those
 * belong to the loop in main.c, which asks for the frame when it wants
 * one. */
#include "dosbox.h"
#include "internal.h"
/* The header is vendored from libretro-common as it ships; it predates
 * -Wstrict-prototypes and is not ours to edit. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include "libretro.h"
#pragma GCC diagnostic pop
#include "log.h"
#include "dos.h" /* the overscan border the text screen wears */
#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#    include <windows.h>
#    define LIB_OPEN(p) ((void *)LoadLibraryA(p))
#    define LIB_SYM(h, n) ((void *)GetProcAddress((HMODULE)(h), (n)))
#    define LIB_CLOSE(h) FreeLibrary((HMODULE)(h))
#    define LIB_EXT ".dll"
#else
#    include <dlfcn.h>
#    define LIB_OPEN(p) dlopen((p), RTLD_NOW | RTLD_LOCAL)
#    define LIB_SYM(h, n) dlsym((h), (n))
#    define LIB_CLOSE(h) dlclose(h)
#    ifdef __APPLE__
#        define LIB_EXT ".dylib"
#    else
#        define LIB_EXT ".so"
#    endif
#endif

/* The entry points the core exports, resolved by name.  libretro.h
 * declares each as a function, so its pointer type comes for free. */
#define CORE_SYMS(X)                                                                               \
    X(retro_api_version)                                                                           \
    X(retro_set_environment)                                                                       \
    X(retro_set_video_refresh)                                                                     \
    X(retro_set_audio_sample)                                                                      \
    X(retro_set_audio_sample_batch)                                                                \
    X(retro_set_input_poll)                                                                        \
    X(retro_set_input_state)                                                                       \
    X(retro_init)                                                                                  \
    X(retro_deinit)                                                                                \
    X(retro_load_game)                                                                             \
    X(retro_unload_game)                                                                           \
    X(retro_get_system_av_info)                                                                    \
    X(retro_run)

/* What the core is told when it asks for a setting.  Anything not here
 * gets its own default.  The rate matches the device so nothing is
 * resampled; the start menu is off because the machine has its own
 * prompt and its own way of ending (EXIT powers it off); the software
 * Voodoo because the core must never want a GL context of its own. */
static const struct {
    const char *key, *value;
} OPTIONS[] = {
    {"dosbox_pure_audiorate", "44100"},
    {"dosbox_pure_menu_time", "0"},
    {"dosbox_pure_voodoo_perf", "0"},
    {"dosbox_pure_savestate", "disabled"},
    {"dosbox_pure_on_screen_keyboard", "false"},
    {"dosbox_pure_auto_mapping", "false"},
    {"dosbox_pure_perfstats", "none"},
#if defined(__APPLE__) && defined(__aarch64__)
    /* The interpreter, not the recompiler: the dynrec allocates its code
     * cache with malloc and mprotects it executable, which Apple Silicon
     * refuses (a JIT there needs MAP_JIT and W^X toggling), and the core
     * then jumps into memory it cannot run.  A 486 interpreted on a
     * machine of this decade is not the bottleneck anywhere yet. */
    {"dosbox_pure_cpu_core", "normal"},
#else
    {"dosbox_pure_cpu_core", "auto"},
#endif
};

/* What the DOS prints when it reaches its prompt: the ECHO lines of the
 * DOSBOX.BAT the machine writes into C:.  DOSBox Pure runs that file in
 * place of its own start menu when it finds one in the root. */
static const char *const GREETING[] = {
    "- For a list of available commands, type HELP.",
    "- If you know, you know.",
    "",
};

/* The one option that changes while the machine runs: the CPU speed, from
 * the turbo display's buttons.  The main thread sets it; the core thread
 * reads it through the variable callbacks, which is why it is atomic and
 * why the "variables updated" flag is too. */
static SDL_AtomicInt g_cycles, g_cycles_dirty;
static char g_cycles_str[16];

void dosbox_set_cycles(int cycles) {
    SDL_SetAtomicInt(&g_cycles, cycles);
    SDL_SetAtomicInt(&g_cycles_dirty, 1);
}

#define FRAME_MAX_W 1280
#define FRAME_MAX_H 1024
#define NFRAMES 3 /* one being read, one being written, one waiting */
#define KEYQ 256
#define RING_FRAMES (DOSBOX_AUDIO_HZ / 4) /* a quarter second of audio */

/* The emulator: its library, its thread, and everything crossing between
 * that thread, the main loop and the audio callback. */
static struct {
    void *lib;
    struct {
#define X(n) __typeof__(&n) n;
        CORE_SYMS(X)
#undef X
    } fn;
    SDL_Thread *th;
    SDL_Mutex *mu;  /* frames, the key queue, the mouse */
    SDL_Mutex *amu; /* the audio ring */
    volatile int running, quit_req, exited, shown;
    char c_drive[1024], sys_dir[1024], save_dir[1024];
    retro_keyboard_event_t key_cb;
    double fps; /* what the core says the picture refreshes at */

    uint8_t *fb[NFRAMES];
    int fw[NFRAMES], fh[NFRAMES]; /* the texture, border included */
    int sw[NFRAMES], sh[NFRAMES]; /* what the card actually drew */
    volatile int front, reading;  /* -1 for none */

    struct {
        unsigned key;
        int down;
    } kq[KEYQ];
    int kq_r, kq_w;
    unsigned char keystate[RETROK_LAST];
    unsigned short mods; /* the lock keys, as RETROKMOD_* */

    char typing[512]; /* what --type still has to type, and the key it holds */
    int typing_n, held_key, held_shift;
    int mdx, mdy; /* mouse motion since the core last polled */
    int pdx, pdy; /* what it polled */
    int mbtn[3];  /* left, middle, right */

    int16_t *ring; /* stereo frames */
    int ring_r, ring_w;
} db = {.front = -1, .reading = -1};

/* ---- what the core calls -------------------------------------------- */

static void RETRO_CALLCONV core_log(enum retro_log_level lvl, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
static void RETRO_CALLCONV core_log(enum retro_log_level lvl, const char *fmt, ...) {
    if (lvl < RETRO_LOG_INFO)
        return; /* debug chatter is DOSBox's own business */
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = 0;
    dxm_log("dosbox: %s", buf);
}

static void av_apply(const struct retro_system_av_info *av) {
    if (av->timing.fps > 1.0 && av->timing.fps != db.fps) {
        db.fps = av->timing.fps;
        dxm_log("dosbox: %ux%u at %.2f Hz", av->geometry.base_width, av->geometry.base_height,
                db.fps);
    }
    if ((int)av->timing.sample_rate != DOSBOX_AUDIO_HZ)
        dxm_log("dosbox: core mixes at %.0f Hz, the device wants %d - expect the pitch off",
                av->timing.sample_rate, DOSBOX_AUDIO_HZ);
}

static bool RETRO_CALLCONV env_cb(unsigned cmd, void *data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return *(const enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = core_log;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = db.sys_dir;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = db.save_dir;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        v->value = NULL;
        if (!strcmp(v->key, "dosbox_pure_cycles")) {
            int cy = SDL_GetAtomicInt(&g_cycles);
            if (cy > 0) {
                snprintf(g_cycles_str, sizeof g_cycles_str, "%d", cy);
                v->value = g_cycles_str;
                dxm_log("dosbox: cycles %d", cy); /* the core took the new speed */
            }
            return v->value != NULL;
        }
        for (size_t i = 0; i < sizeof OPTIONS / sizeof OPTIONS[0]; i++)
            if (!strcmp(OPTIONS[i].key, v->key))
                v->value = OPTIONS[i].value;
        return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        /* the core asks every frame, and re-reads its options when told */
        *(bool *)data = SDL_SetAtomicInt(&g_cycles_dirty, 0) != 0;
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned *)data = 2;
        return true;
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        db.key_cb = ((const struct retro_keyboard_callback *)data)->callback;
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        av_apply(data);
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
        dxm_log("dosbox: %s", ((const struct retro_message_ext *)data)->msg);
        return true;
    case RETRO_ENVIRONMENT_SHUTDOWN:
        /* DOS was told EXIT.  The core wants to stop; the machine wants
         * to power off. */
        db.exited = 1;
        db.quit_req = 1;
        return true;
    /* Accepted and ignored: the option lists (we answer from OPTIONS), the
     * disk-swap and memory-map interfaces, the flags. */
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
        /* Declined on purpose.  A frontend that accepts this promises to
         * call the core back when an option changes, and the core then
         * stops asking GET_VARIABLE_UPDATE each frame.  Saying yes and
         * never calling is how the cycles from the turbo display's
         * buttons were taken at boot and never again. */
        return false;
    default:
        /* hardware rendering, VFS, MIDI, perf counters, throttle state,
         * fast-forward: none of it, and the core copes */
        return false;
    }
}

static void RETRO_CALLCONV video_cb(const void *px, unsigned w, unsigned h, size_t pitch) {
    if (!px)
        return; /* a duplicated frame: the tube keeps the last one */
    if (w > FRAME_MAX_W || h > FRAME_MAX_H) {
        static int said;
        if (!said++)
            dxm_log("dosbox: %ux%u is larger than the tube takes", w, h);
        return;
    }
    /* The picture wears the border the machine's own text screen wears -
     * the card's overscan, lit but empty - in the same proportion, so a
     * 640x400 text mode lands on exactly the canvas the BIOS drew on and
     * the handover does not move a pixel. */
    int padx = (int)((w * DOS_PAD_X + 320) / 640), pady = (int)((h * DOS_PAD_Y + 200) / 400);
    int fw = (int)w + 2 * padx, fh = (int)h + 2 * pady;
    if (fw > FRAME_MAX_W || fh > FRAME_MAX_H)
        return;
    {
        /* every change of picture size, the way the card would have
         * re-synced the monitor: this is where a mode switch shows */
        static unsigned lw, lh;
        if (w != lw || h != lh) {
            lw = w;
            lh = h;
            dxm_log("dosbox: picture %ux%u, on a %dx%d canvas", w, h, fw, fh);
        }
    }
    SDL_LockMutex(db.mu);
    int b = 0;
    while (b == db.front || b == db.reading)
        b++; /* the buffer nobody holds */
    SDL_UnlockMutex(db.mu);
    uint8_t *fb = db.fb[b];
    memset(fb, 0, (size_t)fw * fh * 3);
    /* XRGB8888 in memory is B, G, R, X */
    for (unsigned y = 0; y < h; y++) {
        const uint8_t *s = (const uint8_t *)px + y * pitch;
        uint8_t *d = fb + ((size_t)(pady + (int)y) * fw + padx) * 3;
        for (unsigned x = 0; x < w; x++, s += 4, d += 3) {
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
        }
    }
    db.fw[b] = fw;
    db.fh[b] = fh;
    db.sw[b] = (int)w;
    db.sh[b] = (int)h;
    SDL_LockMutex(db.mu);
    db.front = b;
    SDL_UnlockMutex(db.mu);
}

static size_t RETRO_CALLCONV audio_batch_cb(const int16_t *data, size_t frames) {
    if (!db.shown)
        return frames; /* hidden: the sound goes nowhere, and nothing queues up */
    SDL_LockMutex(db.amu);
    if (db.ring) {
        int used = (db.ring_w - db.ring_r + RING_FRAMES) % RING_FRAMES;
        int room = RING_FRAMES - 1 - used;
        int n = (int)frames < room ? (int)frames : room; /* overrun: drop the tail */
        for (int i = 0; i < n; i++) {
            db.ring[db.ring_w * 2] = data[i * 2];
            db.ring[db.ring_w * 2 + 1] = data[i * 2 + 1];
            db.ring_w = (db.ring_w + 1) % RING_FRAMES;
        }
    }
    SDL_UnlockMutex(db.amu);
    return frames;
}
static void RETRO_CALLCONV audio_sample_cb(int16_t l, int16_t r) {
    int16_t s[2] = {l, r};
    audio_batch_cb(s, 1);
}

static void RETRO_CALLCONV input_poll_cb(void) {
    /* the motion since last time, taken whole so X and Y agree */
    SDL_LockMutex(db.mu);
    db.pdx = db.mdx;
    db.pdy = db.mdy;
    db.mdx = db.mdy = 0;
    SDL_UnlockMutex(db.mu);
}
static int16_t RETRO_CALLCONV input_state_cb(unsigned port, unsigned device, unsigned index,
                                             unsigned id) {
    (void)index;
    if (port != 0)
        return 0;
    if (device == RETRO_DEVICE_KEYBOARD)
        return id < RETROK_LAST ? db.keystate[id] : 0;
    if (device == RETRO_DEVICE_MOUSE) {
        switch (id) {
        case RETRO_DEVICE_ID_MOUSE_X:
            return (int16_t)db.pdx;
        case RETRO_DEVICE_ID_MOUSE_Y:
            return (int16_t)db.pdy;
        case RETRO_DEVICE_ID_MOUSE_LEFT:
            return (int16_t)db.mbtn[0];
        case RETRO_DEVICE_ID_MOUSE_MIDDLE:
            return (int16_t)db.mbtn[1];
        case RETRO_DEVICE_ID_MOUSE_RIGHT:
            return (int16_t)db.mbtn[2];
        default:
            return 0;
        }
    }
    return 0; /* no joypad on this machine */
}

/* ---- the core's thread ---------------------------------------------- */

/* Keys are delivered from the thread that runs the core, just before each
 * frame, the way a libretro front end does it.  The lock keys toggle
 * here so the LEDs the core sets match. */
static void deliver_keys(void) {
    for (;;) {
        SDL_LockMutex(db.mu);
        if (db.kq_r == db.kq_w) {
            SDL_UnlockMutex(db.mu);
            return;
        }
        unsigned key = db.kq[db.kq_r].key;
        int down = db.kq[db.kq_r].down;
        db.kq_r = (db.kq_r + 1) % KEYQ;
        SDL_UnlockMutex(db.mu);
        if (down) {
            if (key == RETROK_NUMLOCK)
                db.mods ^= RETROKMOD_NUMLOCK;
            else if (key == RETROK_CAPSLOCK)
                db.mods ^= RETROKMOD_CAPSLOCK;
            else if (key == RETROK_SCROLLOCK)
                db.mods ^= RETROKMOD_SCROLLOCK;
        }
        if (db.key_cb)
            db.key_cb(down ? true : false, key, 0, db.mods);
    }
}

/* The typist: --type's text, a key per frame - down one frame, up the
 * next, the way the BIOS expects to see them - through the same queue the
 * keyboard uses.  ASCII on a US layout; the shifted symbols are the ones a
 * DOS command line needs. */
static unsigned typist_key(int ch, int *shift) {
    static const char *const SHIFTED = "!@#$%^&*()_+{}|:\"<>?~";
    static const char *const PLAIN = "1234567890-=[]\\;',./`";
    *shift = 0;
    if (ch == '\r')
        return RETROK_RETURN;
    if (ch >= 'A' && ch <= 'Z') {
        *shift = 1;
        return (unsigned)(ch - 'A' + 'a');
    }
    const char *p = strchr(SHIFTED, ch);
    if (ch && p) {
        *shift = 1;
        return (unsigned)PLAIN[p - SHIFTED];
    }
    return (ch >= 32 && ch < 127) ? (unsigned)ch : 0;
}
static void typist(void) {
    SDL_LockMutex(db.mu);
    if (db.held_key) {
        /* let go of what was pressed last frame */
        int n = (db.kq_w + 1) % KEYQ;
        if (n != db.kq_r) {
            db.kq[db.kq_w].key = (unsigned)db.held_key;
            db.kq[db.kq_w].down = 0;
            db.kq_w = n;
        }
        if (db.held_shift) {
            n = (db.kq_w + 1) % KEYQ;
            if (n != db.kq_r) {
                db.kq[db.kq_w].key = RETROK_LSHIFT;
                db.kq[db.kq_w].down = 0;
                db.kq_w = n;
            }
        }
        db.held_key = db.held_shift = 0;
    } else if (db.typing_n > 0) {
        int shift;
        unsigned key = typist_key(db.typing[0], &shift);
        memmove(db.typing, db.typing + 1, (size_t)--db.typing_n);
        if (key) {
            if (shift) {
                int n = (db.kq_w + 1) % KEYQ;
                if (n != db.kq_r) {
                    db.kq[db.kq_w].key = RETROK_LSHIFT;
                    db.kq[db.kq_w].down = 1;
                    db.kq_w = n;
                }
            }
            int n = (db.kq_w + 1) % KEYQ;
            if (n != db.kq_r) {
                db.kq[db.kq_w].key = key;
                db.kq[db.kq_w].down = 1;
                db.kq_w = n;
            }
            db.held_key = (int)key;
            db.held_shift = shift;
        }
    }
    SDL_UnlockMutex(db.mu);
}

static int SDLCALL thread_main(void *ud) {
    (void)ud;
    db.fn.retro_set_environment(env_cb);
    db.fn.retro_set_video_refresh(video_cb);
    db.fn.retro_set_audio_sample(audio_sample_cb);
    db.fn.retro_set_audio_sample_batch(audio_batch_cb);
    db.fn.retro_set_input_poll(input_poll_cb);
    db.fn.retro_set_input_state(input_state_cb);
    db.fn.retro_init();
    struct retro_game_info gi = {db.c_drive, NULL, 0, NULL};
    if (!db.fn.retro_load_game(&gi)) {
        dxm_log("dosbox: the core would not mount %s", db.c_drive);
        db.fn.retro_deinit();
        db.exited = 1;
        db.running = 0;
        return 0;
    }
    struct retro_system_av_info av;
    db.fn.retro_get_system_av_info(&av);
    av_apply(&av);
    dxm_log("dosbox: booted, C: is %s", db.c_drive);

    /* One frame per emulated refresh.  If the machine falls far behind -
     * a debugger, a sleeping laptop - it picks up from now rather than
     * running flat out to catch up. */
    Uint64 next = SDL_GetTicksNS();
    while (!db.quit_req) {
        typist();
        deliver_keys();
        db.fn.retro_run();
        next += (Uint64)(1e9 / (db.fps > 1.0 ? db.fps : 60.0));
        Uint64 now = SDL_GetTicksNS();
        if (next > now)
            SDL_DelayPrecise(next - now);
        else if (now - next > 250000000ull)
            next = now;
    }
    db.fn.retro_unload_game();
    db.fn.retro_deinit();
    db.running = 0;
    dxm_log("dosbox: down%s", db.exited ? " (DOS said EXIT)" : "");
    return 0;
}

/* ---- the public face ------------------------------------------------- */

/* The autoexec of the mounted drive.  DOSBox Pure runs a DOSBOX.BAT it
 * finds in the root instead of its own start menu, and it is the DOS
 * side of the handover: it clears the screen the BIOS drew on and prints
 * what the machine's own AUTOEXEC.BAT prints - the ECHO lines, nothing
 * that names a driver this drive does not have - so the prompt arrives
 * the way it always has.  A file the machine wrote is rewritten each
 * boot; one somebody else put there is theirs and is kept. */
/* DOSBOX.BAT in the root of C:.  Written the first time, and again only
 * while it still starts with the marker - a file the user has replaced
 * with their own is theirs and is left alone. */
static void write_autoexec(const char *c_drive) {
    static const char MARK[] = "@REM DOS ex Machina writes this file; "
                               "replace it with your own to keep it.";
    char path[1200], head[128] = "";
    snprintf(path, sizeof path, "%s/DOSBOX.BAT", c_drive);
    FILE *f = fopen(path, "rb");
    if (f) {
        size_t n = fread(head, 1, sizeof head - 1, f);
        head[n] = 0;
        fclose(f);
        if (strncmp(head, MARK, 8) != 0)
            return;
    }
    f = fopen(path, "wb");
    if (!f) {
        dxm_log("dosbox: cannot write %s", path);
        return;
    }
    fprintf(f, "%s\r\n@ECHO OFF\r\nCLS\r\n", MARK);
    for (size_t i = 0; i < sizeof GREETING / sizeof GREETING[0]; i++)
        fprintf(f, GREETING[i][0] ? "ECHO %s\r\n" : "ECHO.\r\n", GREETING[i]);
    fclose(f);
    dxm_log("dosbox: wrote %s", path);
}

/* Where the core is looked for: beside the program, which is where every
 * release and every build puts it; in a macOS bundle, whose base path SDL
 * gives as Contents/Resources, in Contents/Frameworks beside SDL3; and
 * last in the preferences, for a core put there by hand. */
static int find_core(char *out, size_t n, const char *pref_dir) {
    const char *base = SDL_GetBasePath();
    char frameworks[1100] = "";
#ifdef __APPLE__
    if (base)
        snprintf(frameworks, sizeof frameworks, "%s../Frameworks/", base);
#endif
    const char *dirs[3] = {base ? base : "", frameworks, pref_dir ? pref_dir : ""};
    for (int i = 0; i < 3; i++) {
        if (!dirs[i][0] && i == 1)
            continue;
        snprintf(out, n, "%sdosbox_pure_libretro" LIB_EXT, dirs[i]);
        FILE *f = fopen(out, "rb");
        if (f) {
            fclose(f);
            return 0;
        }
    }
    return -1;
}

int dosbox_start(const char *core_path, const char *c_drive, const char *pref_dir) {
    if (db.running)
        return -1;
    char found[1200];
    if (!core_path) {
        if (find_core(found, sizeof found, pref_dir) != 0) {
            dxm_log("dosbox: no dosbox_pure_libretro" LIB_EXT " beside the program or in %s",
                    pref_dir ? pref_dir : "the preferences");
            return -1;
        }
        core_path = found;
    }
    db.lib = LIB_OPEN(core_path);
    if (!db.lib) {
#ifdef _WIN32
        dxm_log("dosbox: cannot open %s (error %lu)", core_path, GetLastError());
#else
        dxm_log("dosbox: cannot open %s: %s", core_path, dlerror());
#endif
        return -1;
    }
#define X(sym)                                                                                     \
    db.fn.sym = (__typeof__(db.fn.sym))LIB_SYM(db.lib, #sym);                                      \
    if (!db.fn.sym) {                                                                              \
        dxm_log("dosbox: %s is not a libretro core: no %s", core_path, #sym);                      \
        LIB_CLOSE(db.lib);                                                                         \
        db.lib = NULL;                                                                             \
        return -1;                                                                                 \
    }
    CORE_SYMS(X)
#undef X
    unsigned api = db.fn.retro_api_version();
    if (api != RETRO_API_VERSION) {
        dxm_log("dosbox: core speaks libretro API %u, this build speaks %u", api,
                (unsigned)RETRO_API_VERSION);
        LIB_CLOSE(db.lib);
        db.lib = NULL;
        return -1;
    }
    snprintf(db.c_drive, sizeof db.c_drive, "%s", c_drive);
    snprintf(db.sys_dir, sizeof db.sys_dir, "%sdosbox%csystem", pref_dir, '/');
    snprintf(db.save_dir, sizeof db.save_dir, "%sdosbox%csave", pref_dir, '/');
    SDL_CreateDirectory(db.sys_dir);
    SDL_CreateDirectory(db.save_dir);
    write_autoexec(c_drive);

    for (int i = 0; i < NFRAMES; i++)
        if (!db.fb[i] && !(db.fb[i] = malloc((size_t)FRAME_MAX_W * FRAME_MAX_H * 3))) {
            dxm_log("dosbox: out of memory for the frame buffers");
            return -1;
        }
    if (!db.ring && !(db.ring = calloc((size_t)RING_FRAMES * 2, sizeof(int16_t)))) {
        dxm_log("dosbox: out of memory for the audio ring");
        return -1;
    }
    if (!db.mu)
        db.mu = SDL_CreateMutex();
    if (!db.amu)
        db.amu = SDL_CreateMutex();
    db.front = db.reading = -1;
    db.kq_r = db.kq_w = 0;
    db.ring_r = db.ring_w = 0;
    db.quit_req = db.exited = db.shown = 0;
    db.fps = 0.0;
    db.running = 1;
    dxm_log("dosbox: core %s", core_path);
    db.th = SDL_CreateThread(thread_main, "dosbox", NULL);
    if (!db.th) {
        db.running = 0;
        return -1;
    }
    return 0;
}

void dosbox_stop(void) {
    if (db.th) {
        db.quit_req = 1;
        SDL_WaitThread(db.th, NULL);
        db.th = NULL;
    }
    db.running = 0;
    if (db.lib) {
        LIB_CLOSE(db.lib);
        db.lib = NULL;
    }
    if (db.amu) {
        /* under the ring's lock, so a callback in flight sees NULL, not freed memory */
        SDL_LockMutex(db.amu);
        free(db.ring);
        db.ring = NULL;
        SDL_UnlockMutex(db.amu);
    }
}

int dosbox_running(void) {
    return db.running;
}
int dosbox_exited(void) {
    return db.exited;
}
void dosbox_show(void) {
    db.shown = 1;
}
int dosbox_shown(void) {
    return db.shown;
}

const uint8_t *dosbox_frame(int *w, int *h, int *crt_lines) {
    if (!db.mu)
        return NULL;
    SDL_LockMutex(db.mu);
    int f = db.front;
    db.reading = f;
    SDL_UnlockMutex(db.mu);
    if (f < 0)
        return NULL;
    *w = db.fw[f];
    *h = db.fh[f];
    /* The tube locks its scanlines to the texture's rows, so the count is
     * of the rows as delivered, border included: the beam swept the
     * border at the same pitch as the picture.  Under 300 rows the card
     * was double-scanning - 200 rows of mode 13h drove 400 lines, and so
     * did CGA on a VGA - and the count doubles with it. */
    *crt_lines = db.sh[f] < 300 ? db.fh[f] * 2 : db.fh[f];
    return db.fb[f];
}
/* Whether the frame being shown is a text mode, by its geometry: the
 * 8- or 9-dot 80-column modes at 350 or 400 lines.  A guess a graphics
 * mode of the same size would fool; reading the card's mode register
 * is the honest answer, and a patch to the core away. */
int dosbox_text_mode(void) {
    int f = db.reading;
    if (f < 0)
        return 0;
    int w = db.sw[f], h = db.sh[f];
    return (w == 640 || w == 720) && (h == 400 || h == 350);
}
void dosbox_frame_done(void) {
    if (!db.mu)
        return;
    SDL_LockMutex(db.mu);
    db.reading = -1;
    SDL_UnlockMutex(db.mu);
}

void dosbox_key(int sc, int down) {
    unsigned key = dosbox_retrok_of_sdl(sc);
    if (!key || !db.mu)
        return;
    SDL_LockMutex(db.mu);
    int n = (db.kq_w + 1) % KEYQ;
    if (n != db.kq_r) {
        db.kq[db.kq_w].key = key;
        db.kq[db.kq_w].down = down;
        db.kq_w = n;
    }
    db.keystate[key] = (unsigned char)down;
    SDL_UnlockMutex(db.mu);
}
void dosbox_type(const char *s) {
    if (!db.mu)
        return;
    SDL_LockMutex(db.mu);
    for (; *s && db.typing_n < (int)sizeof db.typing; s++)
        db.typing[db.typing_n++] = *s;
    SDL_UnlockMutex(db.mu);
}
void dosbox_mouse_move(int dx, int dy) {
    if (!db.mu)
        return;
    SDL_LockMutex(db.mu);
    db.mdx += dx;
    db.mdy += dy;
    SDL_UnlockMutex(db.mu);
}
void dosbox_mouse_button(int b, int down) {
    if (b == SDL_BUTTON_LEFT)
        db.mbtn[0] = down;
    else if (b == SDL_BUTTON_MIDDLE)
        db.mbtn[1] = down;
    else if (b == SDL_BUTTON_RIGHT)
        db.mbtn[2] = down;
}

void dosbox_audio(int16_t *out, int frames) {
    memset(out, 0, (size_t)frames * 4);
    if (!db.amu)
        return;
    SDL_LockMutex(db.amu);
    if (db.ring && db.shown) {
        int used = (db.ring_w - db.ring_r + RING_FRAMES) % RING_FRAMES;
        int n = frames < used ? frames : used; /* underrun: the rest stays silent */
        for (int i = 0; i < n; i++) {
            out[i * 2] = db.ring[db.ring_r * 2];
            out[i * 2 + 1] = db.ring[db.ring_r * 2 + 1];
            db.ring_r = (db.ring_r + 1) % RING_FRAMES;
        }
    }
    SDL_UnlockMutex(db.amu);
}
