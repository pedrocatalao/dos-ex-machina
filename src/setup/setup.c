/* setup.c — the SETUP program: what it holds, what the keys do, and the
 * screen it draws.
 *
 * The shape is a configuration program of its day: artwork across the top
 * with the screen fading under it, the sections down the left, the chosen
 * one's settings in the pane beside them, and the keys it answers along the
 * bottom.  The mouse is the machine's own, so the pointer is drawn here
 * rather than by the desktop.
 *
 * The eye is either on the list of sections or in the pane, and the same
 * keys mean the obvious thing in both: up and down move, left and right
 * change, ESC steps back out.  A setting is a pointer to the live value
 * (MONITOR writes the CRT parameters the shader is reading this frame), so
 * there is nothing to apply - only something to keep, which SAVE does. */
#include "setup.h"
#include "internal.h"
#include "log.h"
#include "ui.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static const struct {
    const char *name, *about;
} SECTION[] = {
    {"MONITOR", "The tube and its glass."},    {"KEYBOARD", "The layout DOS types in."},
    {"MACHINE", "Memory and processor core."}, {"SOUND", "The MIDI device, and what it costs."},
    {"BOOT", "Where the boot ends up."},       {"SAVE & EXIT", "Keep it, or leave it be."},
};
#define SECTIONS ((int)(sizeof SECTION / sizeof SECTION[0]))
enum { SEC_MONITOR = 0, SEC_SAVE = SECTIONS - 1 };

/* One thing that can be changed.  For now every one of them is a float
 * somewhere in the machine that takes effect the moment it moves. */
typedef struct {
    const char *name;
    float *val, lo, hi;
} setting;
#define MAXSET 24
static setting SET[MAXSET];
static int nset;

enum {
    WIN_X = 16,
    WIN_Y = 152,
    WIN_W = SCR_W - 2 * WIN_X,
    WIN_H = SCR_H - WIN_Y - 12,
    WIN_R = 10,
    BAR_H = 20,
    LIST_X = WIN_X + 12,
    LIST_Y = WIN_Y + BAR_H + 16,
    LIST_W = 160,
    ROW_H = 18,
    PANE_X = LIST_X + LIST_W + 16,
    PANE_Y = LIST_Y,
    PANE_W = WIN_X + WIN_W - 12 - PANE_X,
    PANE_H = WIN_Y + WIN_H - 34 - PANE_Y,
    FOOT_Y = WIN_Y + WIN_H - 26,
    ROWS = PANE_H / ROW_H,  /* settings on screen at once */
    TRACK_X = PANE_X + 148, /* where a slider's groove starts */
    TRACK_W = 150
};

/* A change of picture comes in through the blinds: the new one appears in
 * slats that open downward over the old.  Both are on the glass at once,
 * which on an indexed screen means they have to share the 240 colours -
 * fitted to the pair when the change starts (banner_open_pair), at the cost
 * of a palette that is nobody's best while the slats are moving. */
#define SLATS 14
#define BLIND_MS 620

/* What the machine does while SETUP is coming up: the program says what it
 * is, works (the artwork really is being opened and fitted behind this),
 * and then the screen comes up out of black on the palette. */
#define LOAD_MS 750
#define FADE_MS 350

static struct {
    int shown, section, banner;
    int next_banner;          /* what the blinds are opening on */
    Uint64 blind_t0;          /* when they started, or 0 */
    const banner *from, *to;  /* the pair, while they are moving */
    Uint64 open_t0;           /* when it was asked for */
    int in_pane, row, scroll; /* where the eye is, and what it can see */
    int mx, my, held;         /* the pointer, and the button */
    gpu_knobs *knobs;
    const char *crt_cfg;
} S = {.mx = SCR_W / 2, .my = SCR_H / 2};

void setup_bind(gpu_knobs *knobs, const char *crt_cfg) {
    S.knobs = knobs;
    S.crt_cfg = crt_cfg;
}

static void add(const char *name, float *val, float lo, float hi) {
    if (nset < MAXSET)
        SET[nset++] = (setting){name, val, lo, hi};
}

/* What the chosen section has to offer.  MONITOR is the tube, and its
 * values are the ones the panel has always had; the rest are still to
 * come. */
static void fill(int section) {
    nset = 0;
    if (section != SEC_MONITOR || !S.knobs)
        return;
    gpu_knobs *k = S.knobs;
    add("Brightness", &k->brightness, 0.0f, 1.0f);
    add("Contrast", &k->contrast, 0.4f, 1.8f);
    add("Bloom", &k->bloom, 0.0f, 1.5f);
    add("Persistence", &k->persistence, 0.0f, 1.0f);
    add("Scanlines", &k->scan, 0.0f, 1.0f);
    add("Pixel grid", &k->vgrid, 0.0f, 1.0f);
    add("Curvature", &k->warp, 0.0f, 0.45f);
    add("Burn in", &k->burn_in, 0.0f, 1.0f);
    add("Static noise", &k->noise, 0.0f, 1.0f);
    add("Flickering", &k->flicker, 0.0f, 1.0f);
    add("Jitter", &k->jitter, 0.0f, 1.0f);
    add("Horizontal sync", &k->hsync, 0.0f, 1.0f);
    add("RGB shift", &k->rgb_shift, 0.0f, 1.0f);
    add("Glow line", &k->glow_line, 0.0f, 1.0f);
    add("Chassis glow", &k->chassis_glow, 0.0f, 1.5f);
    add("Ambient light", &k->ambient, 0.0f, 1.0f);
}

/* Which artwork this time: drawn afresh at each opening, off the counter
 * rather than the clock, so opening it twice in a second is twice. */
static int draw_banner(void) {
    Uint64 s = SDL_GetPerformanceCounter();
    s ^= s >> 33;
    s *= 0xff51afd7ed558ccdULL;
    s ^= s >> 29;
    int n = cv_banner_count();
    return n > 0 ? (int)(s % (Uint64)n) : 0;
}

void setup_open(void) {
    if (S.shown)
        return;
    S.shown = 1;
    S.section = 0;
    S.in_pane = S.row = S.scroll = 0;
    S.banner = draw_banner();
    S.mx = SCR_W / 2;
    S.my = SCR_H / 2;
    S.open_t0 = SDL_GetTicks();
    S.blind_t0 = 0;
    S.from = S.to = NULL;
    fill(S.section);
    dxm_log("setup: open");
}

void setup_close(void) {
    if (!S.shown)
        return;
    S.shown = 0;
    dxm_log("setup: closed");
}

int setup_visible(void) {
    return S.shown;
}

static void go_section(int to) {
    S.section = (to + SECTIONS) % SECTIONS;
    S.in_pane = S.row = S.scroll = 0;
    fill(S.section);
}

static void show_row(int r) {
    if (r < S.scroll)
        S.scroll = r;
    if (r >= S.scroll + ROWS)
        S.scroll = r - ROWS + 1;
}

/* one step of a value, or ten of them with shift held */
static void nudge(int by, int shift) {
    if (!nset)
        return;
    setting *s = &SET[S.row];
    float step = (s->hi - s->lo) / (shift ? 10.0f : 50.0f);
    float v = *s->val + step * (float)by;
    *s->val = v < s->lo ? s->lo : (v > s->hi ? s->hi : v);
}

void setup_key(int sdl_scancode, int shift) {
    switch (sdl_scancode) {
    case SDL_SCANCODE_ESCAPE:
        if (S.in_pane)
            S.in_pane = 0;
        else
            setup_close();
        break;
    case SDL_SCANCODE_UP:
        if (S.in_pane) {
            S.row = (S.row + (nset ? nset : 1) - 1) % (nset ? nset : 1);
            show_row(S.row);
        } else
            go_section(S.section - 1);
        break;
    case SDL_SCANCODE_DOWN:
        if (S.in_pane) {
            S.row = (S.row + 1) % (nset ? nset : 1);
            show_row(S.row);
        } else
            go_section(S.section + 1);
        break;
    case SDL_SCANCODE_SPACE: {
        /* another picture: the tube's settings are judged against what is on
         * it, and one image flatters what another shows up */
        int n = cv_banner_count();
        if (n > 1 && !S.blind_t0) {
            int next = (S.banner + 1) % n;
            if (banner_open_pair(S.banner, next, &S.from, &S.to)) {
                S.next_banner = next;
                S.blind_t0 = SDL_GetTicks();
            }
        }
        break;
    }
    case SDL_SCANCODE_TAB: /* across to the settings, and back again */
        if (S.in_pane)
            S.in_pane = 0;
        else if (nset)
            S.in_pane = 1;
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        if (S.section == SEC_SAVE) {
            setup_save();
            setup_close();
        } else if (nset)
            S.in_pane = 1;
        break;
    case SDL_SCANCODE_LEFT:
        if (S.in_pane)
            nudge(-1, shift);
        break;
    case SDL_SCANCODE_RIGHT:
        if (S.in_pane)
            nudge(1, shift);
        break;
    default:
        break;
    }
}

void setup_save(void) {
    if (S.crt_cfg)
        ui_save(S.crt_cfg);
    dxm_log("setup: kept");
}

/* the section row under a point, or -1 */
static int row_at(int x, int y) {
    if (x < LIST_X || x >= LIST_X + LIST_W)
        return -1;
    int r = (y - LIST_Y) / ROW_H;
    return (y >= LIST_Y && r >= 0 && r < SECTIONS) ? r : -1;
}
/* the setting under a point, or -1 */
static int set_at(int x, int y) {
    if (x < PANE_X || x >= PANE_X + PANE_W)
        return -1;
    int r = (y - PANE_Y) / ROW_H + S.scroll;
    return (y >= PANE_Y && r >= 0 && r < nset) ? r : -1;
}

/* the pointer on a groove: take the value from where it is */
static void grab(void) {
    if (!nset || S.mx < TRACK_X - 6 || S.mx > TRACK_X + TRACK_W + 6)
        return;
    setting *s = &SET[S.row];
    float t = (float)(S.mx - TRACK_X) / (float)(TRACK_W - 1);
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    *s->val = s->lo + t * (s->hi - s->lo);
}

/* The wheel scrolls the settings, wherever the pointer is: a list longer
 * than the pane is the one thing the keys alone cannot reach. */
void setup_wheel(int by) {
    int max = nset > ROWS ? nset - ROWS : 0;
    S.scroll -= by;
    if (S.scroll < 0)
        S.scroll = 0;
    if (S.scroll > max)
        S.scroll = max;
}

void setup_mouse(int dx, int dy) {
    S.mx += dx;
    S.my += dy;
    if (S.mx < 0)
        S.mx = 0;
    if (S.my < 0)
        S.my = 0;
    if (S.mx >= SCR_W)
        S.mx = SCR_W - 1;
    if (S.my >= SCR_H)
        S.my = SCR_H - 1;
    if (S.held && S.in_pane)
        grab();
}

void setup_click(int down) {
    S.held = down;
    if (!down)
        return;
    int r = row_at(S.mx, S.my);
    if (r >= 0) {
        go_section(r);
        return;
    }
    int t = set_at(S.mx, S.my);
    if (t >= 0) {
        S.in_pane = 1;
        S.row = t;
        grab();
    }
}

static void pane_rows(void) {
    for (int i = 0; i < ROWS && S.scroll + i < nset; i++) {
        setting *s = &SET[S.scroll + i];
        int y = PANE_Y + i * ROW_H, on = S.in_pane && S.scroll + i == S.row;
        cv_text(PANE_X, y, s->name, on ? C_WHITE : C_GREY, -1);
        cv_slider(TRACK_X, y + 2, TRACK_W, (*s->val - s->lo) / (s->hi - s->lo), on);
        char v[16];
        snprintf(v, sizeof v, "%.2f", (double)*s->val);
        cv_text(TRACK_X + TRACK_W + 12, y, v, on ? C_WHITE : C_GREY, -1);
    }
    /* how much of the list is showing, if it does not all fit */
    if (nset > ROWS) {
        int x = PANE_X + PANE_W - 3, h = PANE_H * ROWS / nset;
        cv_rect(x, PANE_Y, 1, PANE_H, C_DGREY);
        cv_rect(x, PANE_Y + PANE_H * S.scroll / nset, 1, h, C_GREY);
    }
}

/* How far the slats have opened, 0 to 1, on the wall clock like everything
 * else the glass does: SETUP stops the machine's clock, not the eye's. */
static float blinds(void) {
    if (!S.blind_t0)
        return -1.0f;
    float t = (float)(SDL_GetTicks() - S.blind_t0) / (float)BLIND_MS;
    if (t >= 1.0f) { /* through: the new one is simply the picture now */
        S.blind_t0 = 0;
        S.banner = S.next_banner;
        S.from = S.to = NULL;
        return -1.0f;
    }
    return t * t * (3.0f - 2.0f * t); /* eased, so they do not jerk open */
}

/* The screen the machine puts up while it gets SETUP ready: its name, and a
 * line that fills as the work is done.  The work is real - the artwork is
 * opened and fitted here - so the wait is the wait, not a pretence. */
static const uint8_t *loading(Uint64 since, int *w, int *h) {
    cv_clear(C_BLACK);
    cv_fade(1.0f);
    banner_open(S.banner); /* the slow part, done where it shows */

    const char *title = "DOS ex Machina", *sub = "SYSTEM SETUP";
    int y = SCR_H / 2 - 28;
    cv_text_bold((SCR_W - cv_width(title)) / 2, y, title, C_WHITE, -1);
    cv_text((SCR_W - cv_width(sub)) / 2, y + 20, sub, C_YELLOW, -1);

    int bw = 220, bx = (SCR_W - bw) / 2, by = y + 52;
    float t = (float)since / (float)LOAD_MS;
    cv_rect(bx, by, bw, 1, C_DGREY);
    cv_rect(bx, by, (int)((float)bw * (t > 1.0f ? 1.0f : t)), 1, C_YELLOW);

    *w = CANVAS_W;
    *h = CANVAS_H;
    return cv_rgb();
}

const uint8_t *setup_render(int *w, int *h) {
    Uint64 since = SDL_GetTicks() - S.open_t0;
    if (since < LOAD_MS)
        return loading(since, w, h);
    /* and then up out of black, on the palette */
    cv_fade(since < LOAD_MS + FADE_MS ? (float)(since - LOAD_MS) / (float)FADE_MS : 1.0f);

    cv_clear(C_BLACK);
    float open = blinds();
    if (open >= 0.0f)
        cv_banner_slats(S.from, S.to, open, SLATS);
    else
        cv_banner(S.banner);

    /* The panel: the artwork darkened rather than covered, a hairline
     * around it, and nothing that pretends to be a button. */
    cv_scrim(WIN_X, WIN_Y, WIN_W, WIN_H, 4, WIN_R);
    cv_round_frame(WIN_X, WIN_Y, WIN_W, WIN_H, C_DGREY, WIN_R);

    int pen = WIN_X + 12;
    pen += cv_text_bold(pen, WIN_Y + 8, "DOS ex Machina", C_WHITE, -1);
    cv_text(pen + 10, WIN_Y + 8, "SYSTEM SETUP", C_YELLOW, -1);
    cv_rect(WIN_X + 12, WIN_Y + BAR_H + 8, WIN_W - 24, 1, C_DGREY);

    for (int i = 0; i < SECTIONS; i++) {
        int y = LIST_Y + i * ROW_H, on = i == S.section;
        if (on && !S.in_pane)
            cv_text(LIST_X, y, "\x10", C_YELLOW, -1);
        if (on)
            cv_text_bold(LIST_X + 16, y, SECTION[i].name, C_WHITE, -1);
        else
            cv_text(LIST_X + 16, y, SECTION[i].name, C_GREY, -1);
    }
    cv_rect(PANE_X - 16, LIST_Y, 1, PANE_H, C_DGREY);

    if (nset)
        pane_rows();
    else {
        cv_text_bold(PANE_X, PANE_Y, SECTION[S.section].name, C_YELLOW, -1);
        cv_text(PANE_X, PANE_Y + 24, SECTION[S.section].about, C_GREY, -1);
        if (S.section != SEC_SAVE)
            cv_text(PANE_X, PANE_Y + 48, "Nothing here yet.", C_DGREY, -1);
    }

    /* the keys, along the foot under their own rule: each pair placed after
     * the last rather than in a column, since the face is proportional */
    cv_rect(WIN_X + 12, FOOT_Y - 8, WIN_W - 24, 1, C_DGREY);
    static const char *const KEYS[][2] = {{"\x18\x19", "Move"},
                                          {"\x1B\x1A", "Change"},
                                          {"TAB", "Settings"},
                                          {"SPACE", "Picture"},
                                          {"ESC", "Back"}};
    pen = WIN_X + 12;
    for (int i = 0; i < 5; i++) {
        pen += cv_text_bold(pen, FOOT_Y, KEYS[i][0], C_YELLOW, -1) + 6;
        pen += cv_text(pen, FOOT_Y, KEYS[i][1], C_GREY, -1) + 18;
    }

    cv_pointer(S.mx, S.my);

    *w = CANVAS_W; /* the picture and its overscan, as the tube wants it */
    *h = CANVAS_H;
    return cv_rgb();
}
