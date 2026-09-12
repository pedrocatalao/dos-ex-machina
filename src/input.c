/* input.c — see input.h. */
#include "input.h"
#include "cursor.h"
#include "log.h"
#include "dos.h"
#include "dosbox.h"
#include "setup.h"
#include "ui.h"

/* Which of the display's buttons, if any, is under a point in drawable
 * pixels; -1 for none. */
static int button_at(const dxm_layout *L, float x, float y) {
    for (int i = 0; i < 3; i++)
        if (x >= L->btn[i][0] && x < L->btn[i][0] + L->btn[i][2] && y >= L->btn[i][1] &&
            y < L->btn[i][1] + L->btn[i][3])
            return i;
    return -1;
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
    in->arrow = cursor_vintage();
    if (in->arrow)
        SDL_SetCursor(in->arrow);
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
    } else if (setup_visible()) {
        /* SETUP has the machine while it is up: the keys are its own */
        if (down)
            setup_key(e->key.scancode);
    } else if (dosbox_shown()) {
        dosbox_key(e->key.scancode, down);
    } else if (down) {
        /* dev-only room-light adjust during the boot: F5 darker, F6
         * brighter (the real fiction control is a chassis knob - this is
         * for tuning taste) */
        if (e->key.key == SDLK_F5 || e->key.key == SDLK_F6) {
            k->ambient += (e->key.key == SDLK_F6) ? 0.05f : -0.05f;
            if (k->ambient < 0)
                k->ambient = 0;
            if (k->ambient > 1)
                k->ambient = 1;
            dxm_log("ambient = %.2f", (double)k->ambient);
        } else if (e->key.scancode == SDL_SCANCODE_SPACE)
            setup_open(); /* what the POST screen says the key does */
        else
            dos_skip(); /* any other key hurries the POST along */
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
        int bt = (!in->captured && !ui_visible()) ? button_at(L, mx, my) : -1;
        if (setup_visible())
            setup_mouse(0, 0, e->button.button == SDL_BUTTON_LEFT ? 1 : 2);
        else if (in->captured && dosbox_shown())
            dosbox_mouse_button(e->button.button, 1);
        else if (bt >= 0 && e->button.button == SDL_BUTTON_LEFT) {
            in->button = bt;
            return INPUT_BUTTON;
        } else if (kn >= 0 && e->button.button == SDL_BUTTON_LEFT) {
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
        if (setup_visible())
            setup_mouse((int)e->motion.xrel, (int)e->motion.yrel, 0);
        else if (in->captured && dosbox_shown())
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
