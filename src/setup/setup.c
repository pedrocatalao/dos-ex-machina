/* setup.c — the SETUP program: what it holds, what the keys do, and the
 * screen it draws.
 *
 * The shape is a configuration program of its day: artwork across the top
 * with the screen fading to black under it, the sections down the left, the
 * chosen one's settings in the pane beside them, and the keys it answers
 * along the bottom.  The mouse is the machine's own, so the pointer is
 * drawn here rather than by the desktop.
 *
 * This pass draws the frame and moves between sections; the settings
 * themselves arrive with the widgets. */
#include "setup.h"
#include "internal.h"
#include "log.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static const struct {
    const char *name, *about;
} SECTION[] = {
    {"MONITOR", "The tube and its glass."}, {"KEYBOARD", "The layout DOS types in."},
    {"MACHINE", "Clock, core and memory."}, {"SOUND", "The card and its MIDI."},
    {"BOOT", "Where the boot ends up."},    {"SAVE & EXIT", "Keep it, or leave it be."},
};
#define SECTIONS ((int)(sizeof SECTION / sizeof SECTION[0]))

/* The window, in the lower half where the artwork has faded out: a panel
 * standing off the screen, its title bar, the sections in a well down the
 * left, the chosen one's settings in the well beside them, and the buttons
 * along the bottom. */
enum {
    WIN_X = 16,
    WIN_Y = 152, /* up into the artwork, so it shows through the panel */
    WIN_W = SCR_W - 2 * WIN_X,
    WIN_H = SCR_H - WIN_Y - 12,
    WIN_R = 10, /* the corners, taken off */
    BAR_H = 20,
    LIST_X = WIN_X + 12,
    LIST_Y = WIN_Y + BAR_H + 16,
    LIST_W = 160,
    ROW_H = 18,
    PANE_X = LIST_X + LIST_W + 16,
    PANE_Y = LIST_Y,
    PANE_W = WIN_X + WIN_W - 12 - PANE_X,
    PANE_H = WIN_Y + WIN_H - 34 - PANE_Y,
    FOOT_Y = WIN_Y + WIN_H - 26
};

static struct {
    int shown, section, banner;
    int mx, my; /* the pointer, in the canvas's own pixels */
} S = {.mx = SCR_W / 2, .my = SCR_H / 2};

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
    S.banner = draw_banner();
    S.mx = SCR_W / 2;
    S.my = SCR_H / 2;
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

/* the section row under a point, or -1 */
static int row_at(int x, int y) {
    if (x < LIST_X || x >= LIST_X + LIST_W)
        return -1;
    int r = (y - LIST_Y) / ROW_H;
    return (y >= LIST_Y && r >= 0 && r < SECTIONS) ? r : -1;
}

void setup_key(int sdl_scancode) {
    switch (sdl_scancode) {
    case SDL_SCANCODE_ESCAPE:
        setup_close();
        break;
    case SDL_SCANCODE_UP:
        S.section = (S.section + SECTIONS - 1) % SECTIONS;
        break;
    case SDL_SCANCODE_DOWN:
        S.section = (S.section + 1) % SECTIONS;
        break;
    default:
        break;
    }
}

void setup_mouse(int dx, int dy, int buttons) {
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
    if (buttons & 1) {
        int r = row_at(S.mx, S.my);
        if (r >= 0)
            S.section = r;
    }
}

const uint8_t *setup_render(int *w, int *h) {
    cv_clear(C_BLACK);
    cv_banner(S.banner);

    /* The panel: the artwork darkened rather than covered, a hairline
     * around it, and nothing that pretends to be a button you could press
     * with a real finger. */
    cv_scrim(WIN_X, WIN_Y, WIN_W, WIN_H, 4, WIN_R);
    cv_round_frame(WIN_X, WIN_Y, WIN_W, WIN_H, C_DGREY, WIN_R);

    /* the head: the machine's name, and a rule under it */
    cv_text(WIN_X + 12, WIN_Y + 10, "DOS ex Machina", C_WHITE, -1);
    cv_text(WIN_X + 12 + 15 * 8, WIN_Y + 10, "SYSTEM SETUP", C_YELLOW, -1);
    cv_rect(WIN_X + 12, WIN_Y + BAR_H + 8, WIN_W - 24, 1, C_DGREY);

    /* the sections, marked rather than boxed */
    for (int i = 0; i < SECTIONS; i++) {
        int y = LIST_Y + i * ROW_H;
        int on = i == S.section;
        if (on)
            cv_text(LIST_X, y, "\x10", C_YELLOW, -1); /* the pointing mark */
        cv_text(LIST_X + 16, y, SECTION[i].name, on ? C_WHITE : C_GREY, -1);
    }

    /* the rule between the sections and what they hold */
    cv_rect(PANE_X - 16, LIST_Y, 1, PANE_H, C_DGREY);

    cv_text(PANE_X, PANE_Y, SECTION[S.section].name, C_YELLOW, -1);
    cv_text(PANE_X, PANE_Y + 24, SECTION[S.section].about, C_GREY, -1);

    /* the keys, along the foot under their own rule */
    cv_rect(WIN_X + 12, FOOT_Y - 8, WIN_W - 24, 1, C_DGREY);
    cv_text(WIN_X + 12, FOOT_Y, "\x18\x19", C_YELLOW, -1);
    cv_text(WIN_X + 12 + 3 * 8, FOOT_Y, "SECTION", C_GREY, -1);
    cv_text(WIN_X + 12 + 12 * 8, FOOT_Y, "ENTER", C_YELLOW, -1);
    cv_text(WIN_X + 12 + 18 * 8, FOOT_Y, "CHANGE", C_GREY, -1);
    cv_text(WIN_X + 12 + 26 * 8, FOOT_Y, "ESC", C_YELLOW, -1);
    cv_text(WIN_X + 12 + 30 * 8, FOOT_Y, "LEAVE SETUP", C_GREY, -1);

    cv_pointer(S.mx, S.my);

    *w = CANVAS_W; /* the picture and its overscan, as the tube wants it */
    *h = CANVAS_H;
    return cv_rgb();
}
