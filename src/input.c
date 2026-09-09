/* input.c — see input.h. */
#include "input.h"
#include "log.h"
#include "dos.h"
#include "corehost.h"
#include "dosbox.h"
#include "ui.h"
#include "dxm_core.h"

/* SDL scancode -> XT set 1 make code, the whole keyboard.  A game reads
 * corehost's keys[] by the plain byte, so extended keys (arrows, Ins/Del,
 * Home/End, PgUp/PgDn, right Ctrl/Alt, keypad Enter and /) report the base
 * code they share with their 84-key twins, which is also what INT 9 handed
 * a DOS game that did not track the E0 prefix. */
// clang-format off
static const unsigned char xt_of_sdl[SDL_SCANCODE_COUNT] = {
    [SDL_SCANCODE_ESCAPE] = DXM_SC_ESC,
    [SDL_SCANCODE_1] = 0x02, [SDL_SCANCODE_2] = 0x03, [SDL_SCANCODE_3] = 0x04,
    [SDL_SCANCODE_4] = 0x05, [SDL_SCANCODE_5] = 0x06, [SDL_SCANCODE_6] = 0x07,
    [SDL_SCANCODE_7] = 0x08, [SDL_SCANCODE_8] = 0x09, [SDL_SCANCODE_9] = 0x0A,
    [SDL_SCANCODE_0] = 0x0B, [SDL_SCANCODE_MINUS] = 0x0C, [SDL_SCANCODE_EQUALS] = 0x0D,
    [SDL_SCANCODE_BACKSPACE] = 0x0E, [SDL_SCANCODE_TAB] = 0x0F,
    [SDL_SCANCODE_Q] = 0x10, [SDL_SCANCODE_W] = 0x11, [SDL_SCANCODE_E] = 0x12,
    [SDL_SCANCODE_R] = 0x13, [SDL_SCANCODE_T] = 0x14, [SDL_SCANCODE_Y] = 0x15,
    [SDL_SCANCODE_U] = 0x16, [SDL_SCANCODE_I] = 0x17, [SDL_SCANCODE_O] = 0x18,
    [SDL_SCANCODE_P] = 0x19, [SDL_SCANCODE_LEFTBRACKET] = 0x1A, [SDL_SCANCODE_RIGHTBRACKET] = 0x1B,
    [SDL_SCANCODE_RETURN] = DXM_SC_ENTER, [SDL_SCANCODE_KP_ENTER] = DXM_SC_ENTER,
    [SDL_SCANCODE_LCTRL] = 0x1D, [SDL_SCANCODE_RCTRL] = 0x1D,
    [SDL_SCANCODE_A] = 0x1E, [SDL_SCANCODE_S] = 0x1F, [SDL_SCANCODE_D] = 0x20,
    [SDL_SCANCODE_F] = 0x21, [SDL_SCANCODE_G] = 0x22, [SDL_SCANCODE_H] = 0x23,
    [SDL_SCANCODE_J] = 0x24, [SDL_SCANCODE_K] = 0x25, [SDL_SCANCODE_L] = 0x26,
    [SDL_SCANCODE_SEMICOLON] = 0x27, [SDL_SCANCODE_APOSTROPHE] = 0x28, [SDL_SCANCODE_GRAVE] = 0x29,
    [SDL_SCANCODE_LSHIFT] = 0x2A, [SDL_SCANCODE_BACKSLASH] = 0x2B, [SDL_SCANCODE_NONUSHASH] = 0x2B,
    [SDL_SCANCODE_Z] = 0x2C, [SDL_SCANCODE_X] = 0x2D, [SDL_SCANCODE_C] = 0x2E,
    [SDL_SCANCODE_V] = 0x2F, [SDL_SCANCODE_B] = 0x30, [SDL_SCANCODE_N] = 0x31,
    [SDL_SCANCODE_M] = 0x32, [SDL_SCANCODE_COMMA] = 0x33, [SDL_SCANCODE_PERIOD] = 0x34,
    [SDL_SCANCODE_SLASH] = 0x35, [SDL_SCANCODE_KP_DIVIDE] = 0x35,
    [SDL_SCANCODE_RSHIFT] = 0x36, [SDL_SCANCODE_KP_MULTIPLY] = 0x37,
    [SDL_SCANCODE_LALT] = 0x38, [SDL_SCANCODE_RALT] = 0x38,
    [SDL_SCANCODE_SPACE] = DXM_SC_SPACE, [SDL_SCANCODE_CAPSLOCK] = 0x3A,
    [SDL_SCANCODE_F1] = 0x3B, [SDL_SCANCODE_F2] = 0x3C, [SDL_SCANCODE_F3] = 0x3D,
    [SDL_SCANCODE_F4] = 0x3E, [SDL_SCANCODE_F5] = 0x3F, [SDL_SCANCODE_F6] = 0x40,
    [SDL_SCANCODE_F7] = 0x41, [SDL_SCANCODE_F8] = 0x42,
    [SDL_SCANCODE_F9] = DXM_SC_F9, [SDL_SCANCODE_F10] = DXM_SC_F10,
    [SDL_SCANCODE_NUMLOCKCLEAR] = 0x45, [SDL_SCANCODE_SCROLLLOCK] = 0x46,
    [SDL_SCANCODE_KP_7] = 0x47, [SDL_SCANCODE_HOME] = 0x47,
    [SDL_SCANCODE_KP_8] = DXM_SC_UP, [SDL_SCANCODE_UP] = DXM_SC_UP,
    [SDL_SCANCODE_KP_9] = 0x49, [SDL_SCANCODE_PAGEUP] = 0x49,
    [SDL_SCANCODE_KP_MINUS] = 0x4A,
    [SDL_SCANCODE_KP_4] = DXM_SC_LEFT, [SDL_SCANCODE_LEFT] = DXM_SC_LEFT,
    [SDL_SCANCODE_KP_5] = 0x4C,
    [SDL_SCANCODE_KP_6] = DXM_SC_RIGHT, [SDL_SCANCODE_RIGHT] = DXM_SC_RIGHT,
    [SDL_SCANCODE_KP_PLUS] = 0x4E,
    [SDL_SCANCODE_KP_1] = 0x4F, [SDL_SCANCODE_END] = 0x4F,
    [SDL_SCANCODE_KP_2] = DXM_SC_DOWN, [SDL_SCANCODE_DOWN] = DXM_SC_DOWN,
    [SDL_SCANCODE_KP_3] = 0x51, [SDL_SCANCODE_PAGEDOWN] = 0x51,
    [SDL_SCANCODE_KP_0] = 0x52, [SDL_SCANCODE_INSERT] = 0x52,
    [SDL_SCANCODE_KP_PERIOD] = 0x53, [SDL_SCANCODE_DELETE] = 0x53,
    [SDL_SCANCODE_NONUSBACKSLASH] = 0x56,
    [SDL_SCANCODE_F11] = 0x57, [SDL_SCANCODE_F12] = 0x58,
};
// clang-format on

/* A modifier or a lock key changes what other keys mean; it is not a
 * keypress in its own right.  DOS never returned one from INT 16h, and
 * the prompt must not take one as "any key". */
static int is_modifier(int xt) {
    return xt == 0x1D || xt == 0x2A || xt == 0x36 || xt == 0x38 || xt == 0x3A || xt == 0x45 ||
           xt == 0x46;
}

static int sc_from_sdl(SDL_Scancode s) {
    return (s >= 0 && s < SDL_SCANCODE_COUNT) ? xt_of_sdl[s] : 0;
}

/* Which knob, if any, is under a point in drawable pixels; -1 for none.
 * The hit circle is a little larger than the knob, since a finger is. */
static int knob_at(const dxm_layout *L, float x, float y) {
    for (int i = 0; i < 2; i++) {
        float dx = x - L->knob[i][0], dy = y - L->knob[i][1], r = L->knob[i][2] * 1.35f;
        if (dx * dx + dy * dy <= r * r)
            return i;
    }
    return -1;
}

/* Give the mouse to the machine, or hand it back to the operating system.
 * The machine holds it in SDL's relative mode: the pointer is taken off
 * the desktop entirely - no arrow drawn over the glass and nothing warped
 * into the middle of the tube - and SDL reports motion as deltas.  A game
 * that wants a pointer draws its own, on the tube, where it belongs.
 * Letting go puts the arrow back where the hand left it. */
void input_capture(input_state *in, app *a, int on) {
    in->captured = on;
    input_mouse_sync(in, a);
}

void input_init(input_state *in, app *a) {
    in->knob_drag = -1;
    in->knob_y0 = in->knob_v0 = 0.0f;
    in->holding = -1; /* nothing asked of SDL yet */
    input_capture(in, a, 1);
}

/* the knobs' travel: a third of the screen's height is a whole turn, so a
 * turn is a wrist, not an arm */
static void knob_turn(input_state *in, const app *a, gpu_knobs *k, float my) {
    float v = in->knob_v0 + (in->knob_y0 - my) / (a->H * 0.30f);
    if (v < 0.0f)
        v = 0.0f;
    if (v > 1.0f)
        v = 1.0f;
    if (in->knob_drag == 0)
        k->brightness = v;
    else
        k->contrast = 0.4f + 1.4f * v;
}

static void key_event(input_state *in, app *a, const dxm_layout *L, gpu_knobs *k,
                      const SDL_Event *e) {
    int down = (e->type == SDL_EVENT_KEY_DOWN);
    int sc = sc_from_sdl(e->key.scancode);
    /* Ctrl+F10 everywhere, as DOSBox.  On a Mac the F10 key is Mute
     * unless Fn is held, so a tap of Command alone - a key no DOS game
     * could have bound - does the same there. */
    int toggle = down && e->key.key == SDLK_F10 && (e->key.mod & SDL_KMOD_CTRL);
#ifdef __APPLE__
    {
        static int gui_alone = 0;
        if (e->key.key == SDLK_LGUI || e->key.key == SDLK_RGUI) {
            if (down)
                gui_alone = 1;
            else if (gui_alone) {
                gui_alone = 0;
                toggle = 1;
            }
        } else if (down)
            gui_alone = 0;
    }
#endif
    /* Shift+F1 for the panel, from anywhere - a game, DOSBox, the prompt -
     * and never a bare F1, which belongs to whatever is running.  Not
     * Ctrl+F1: macOS binds that to keyboard-access and eats it before
     * the window hears it.  Ctrl is still accepted where it gets through. */
    int panel = down && e->key.key == SDLK_F1 && (e->key.mod & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL));
    if (toggle) {
        input_capture(in, a, !in->captured);
        in->knob_drag = -1;
    } else if (panel) {
        ui_toggle();
    } else if (dosbox_shown()) {
        dosbox_key(e->key.scancode, down);
    } else if (corehost_running()) {
        int ch = 0;
        if (sc == DXM_SC_ESC)
            ch = 27;
        else if (sc == DXM_SC_ENTER)
            ch = 13;
        else if (sc == DXM_SC_SPACE)
            ch = ' ';
        else if (sc && !is_modifier(sc))
            ch = 0x100 | sc;
        corehost_push_key(sc, down, ch);
    } else if (down) {
        /* dev-only room-light adjust while at the prompt: F5 darker, F6
         * brighter (the real fiction control is a chassis knob, SPEC 6.8 -
         * this is for tuning taste) */
        if (e->key.key == SDLK_F5 || e->key.key == SDLK_F6) {
            k->ambient += (e->key.key == SDLK_F6) ? 0.05f : -0.05f;
            if (k->ambient < 0)
                k->ambient = 0;
            if (k->ambient > 1)
                k->ambient = 1;
            dxm_log("ambient = %.2f", (double)k->ambient);
        } else if (e->key.key == '\r')
            dos_key('\r', sc);
        else if (e->key.key == SDLK_BACKSPACE)
            dos_key('\b', sc);
        /* the navigator is driven by keys that carry no character at all
         * - without this the prompt never hears an arrow or an Esc */
        else if (sc && !is_modifier(sc))
            dos_key(0, sc);
    }
}

input_result input_event(input_state *in, app *a, const dxm_layout *L, gpu_knobs *k,
                         const SDL_Event *e) {
    switch (e->type) {
    case SDL_EVENT_QUIT:
        return INPUT_QUIT;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        float mx = e->button.x * a->W / a->win_wf, my = e->button.y * a->H / a->win_hf;
        int kn = (!in->captured && !ui_visible()) ? knob_at(L, mx, my) : -1;
        if (in->captured && dosbox_shown())
            dosbox_mouse_button(e->button.button, 1);
        else if (kn >= 0 && e->button.button == SDL_BUTTON_LEFT) {
            /* grab: remember where the hand and the knob started */
            in->knob_drag = kn;
            in->knob_y0 = my;
            in->knob_v0 = (kn == 0) ? k->brightness : (k->contrast - 0.4f) / 1.4f;
        } else
            ui_mouse((int)mx, (int)my, 1, 0);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (in->captured && dosbox_shown())
            dosbox_mouse_button(e->button.button, 0);
        else if (in->knob_drag >= 0)
            in->knob_drag = -1;
        else
            ui_mouse((int)(e->button.x * a->W / a->win_wf), (int)(e->button.y * a->H / a->win_hf),
                     0, 0);
        break;
    case SDL_EVENT_MOUSE_MOTION: {
        float mx = e->motion.x * a->W / a->win_wf, my = e->motion.y * a->H / a->win_hf;
        if (in->captured && dosbox_shown())
            dosbox_mouse_move((int)e->motion.xrel, (int)e->motion.yrel);
        else if (in->knob_drag >= 0)
            knob_turn(in, a, k, my);
        else
            ui_mouse((int)mx, (int)my, (e->motion.state & SDL_BUTTON_LMASK) ? 1 : 0, 1);
        break;
    }
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED: {
        int nw, nh;
        SDL_GetWindowSizeInPixels(a->win, &nw, &nh);
        if (nw > 0 && nh > 0 && (nw != a->W || nh != a->H))
            return INPUT_RESIZED;
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
        /* what the layout produced: ASCII for now, one char at a time; the
         * prompt has no use for anything the font's lower half cannot show */
        if (!corehost_running() && !dosbox_shown())
            for (const char *p = e->text.text; *p; p++)
                if ((unsigned char)*p >= 32 && (unsigned char)*p < 127)
                    dos_key(*p, 0);
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        key_event(in, a, L, k, e);
        break;
    default:
        break;
    }
    return INPUT_HANDLED;
}

void input_mouse_sync(input_state *in, app *a) {
    /* The panel is worked with the arrow, so it borrows the mouse back for
     * as long as it is up. */
    int want = in->captured && !ui_visible();
    if (want == in->holding)
        return;
    in->holding = want;
    if (!SDL_SetWindowRelativeMouseMode(a->win, want ? true : false))
        dxm_log("mouse: SDL would not %s relative mode: %s", want ? "take" : "release",
                SDL_GetError());
    /* Relative mode hides the pointer itself; this covers a platform where
     * it does not, and costs nothing where it does. */
    if (want)
        SDL_HideCursor();
    else
        SDL_ShowCursor();
    dxm_log("mouse %s", want ? "held by the machine" : "released to the desktop");
}
