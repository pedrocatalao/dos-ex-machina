/* input.c — see input.h. */
#include "input.h"
#include "keygrab.h"
#include "cursor.h"
#include "log.h"
#include "dos.h"
#include "dosbox.h"
#include "setup.h"
#include "catalog.h"
#include "osd.h"

/* Which of the display's buttons, if any, is under a point in drawable
 * pixels; -1 for none. */
static int button_at(const dxm_layout *L, float x, float y) {
    for (int i = 0; i < 3; i++)
        if (x >= L->btn[i][0] && x < L->btn[i][0] + L->btn[i][2] && y >= L->btn[i][1] &&
            y < L->btn[i][1] + L->btn[i][3])
            return i;
    return -1;
}

/* Whether a point is inside a key's well, x,y,w,h; a w of 0 is a key the
 * case had no room for. */
static int key_at(const float *k, float x, float y) {
    return k[2] > 0.0f && x >= k[0] && x < k[0] + k[2] && y >= k[1] && y < k[1] + k[3];
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

/* Whether the machine holds the mouse.  Everything on the tube - DOSBox,
 * SETUP, CATALOG - is the machine's, and hears the mouse only then;
 * otherwise it is the arrow's, out on the case, and the lamps say HOST.
 * The OSD is the monitor's and takes nothing from the mouse: it is worked
 * from the case's OSD key and the keyboard. */
static int machine_has_mouse(const input_state *in) {
    return in->captured;
}

/* The mouse is leaving the machine: whatever it was holding down there it
 * lets go of, or a button held as the mouse left stays down for good -
 * a slider SETUP never stops dragging, a DOS game's fire held on. */
static void let_go(const input_state *in) {
    if (!machine_has_mouse(in))
        return;
    if (setup_visible())
        setup_click(0);
    else if (catalog_visible())
        catalog_click(0);
    else if (dosbox_shown()) {
        dosbox_mouse_button(SDL_BUTTON_LEFT, 0);
        dosbox_mouse_button(SDL_BUTTON_MIDDLE, 0);
        dosbox_mouse_button(SDL_BUTTON_RIGHT, 0);
    }
}

/* Give the mouse to the machine, or hand it back to the operating system.
 * The machine holds it in SDL's relative mode: the pointer is taken off
 * the desktop entirely - no arrow drawn over the glass and nothing warped
 * into the middle of the tube - and SDL reports motion as deltas.  A game
 * that wants a pointer draws its own, on the tube, where it belongs.
 * Letting go puts the arrow back where the hand left it. */
void input_capture(input_state *in, app *a, int on) {
    if (!on)
        let_go(in);
    in->captured = on;
    input_mouse_sync(in, a);
}

void input_init(input_state *in, app *a) {
    in->knob_drag = -1;
    in->knob_y0 = in->knob_v0 = 0.0f;
    in->key_hit = -1;
    in->key_held = in->key_let_go = -1;
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

/* While the OSD is up it has the keys that work it; any other key goes on
 * to the machine as ever, so a game does not freeze while its picture is
 * adjusted. */
static int osd_keys(const SDL_Event *e) {
    return osd_visible() && osd_key(e->key.scancode, (e->key.mod & SDL_KMOD_SHIFT) != 0);
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
    /* Shift+F1 for the OSD, from anywhere - a game, DOSBox, the prompt -
     * and never a bare F1, which belongs to whatever is running.  Not
     * Ctrl+F1: macOS binds that to keyboard-access and eats it before
     * the window hears it.  Ctrl is still accepted where it gets through. */
    int osd = down && e->key.key == SDLK_F1 && (e->key.mod & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL));
    if (toggle) {
        input_capture(in, a, !in->captured);
        in->knob_drag = -1;
    } else if (osd) {
        osd_toggle();
    } else if (down && osd_keys(e)) {
        /* the OSD is up and that key was its own: the machine never sees it
         * go down, and the release, when it comes, reaches it harmlessly */
    } else if (setup_visible()) {
        /* SETUP has the keys while it is up - but not the releases of keys
         * DOS is still holding down.  The RETURN that ran the SETUP command
         * is let go while this screen is up, and a key DOS never sees come
         * up is a key its BIOS goes on repeating: the prompt fills with
         * newlines, and the next press of it does nothing at all. */
        if (down)
            setup_key(e->key.scancode, (e->key.mod & SDL_KMOD_SHIFT) != 0);
        else
            dosbox_key(e->key.scancode, 0);
    } else if (catalog_visible()) {
        if (down)
            catalog_key(e->key.scancode, (e->key.mod & SDL_KMOD_SHIFT) != 0);
        else
            dosbox_key(e->key.scancode, 0); /* the same as SETUP: releases reach DOS */
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
        /* out on the case, released to the host, the arrow works the case's
         * own controls */
        int machine = machine_has_mouse(in);
        int kn = !machine ? knob_at(L, mx, my) : -1;
        int bt = !machine ? button_at(L, mx, my) : -1;
        if (machine && setup_visible())
            setup_click(e->button.button == SDL_BUTTON_LEFT);
        else if (machine && catalog_visible())
            catalog_click(e->button.button == SDL_BUTTON_LEFT);
        else if (machine && dosbox_shown())
            dosbox_mouse_button(e->button.button, 1);
        else if (machine) {
            /* the machine has it, and nothing on the tube wants it yet */
        } else if (e->button.button == SDL_BUTTON_LEFT && key_at(L->osd_btn, mx, my)) {
            /* the OSD key: opens the monitor's display, and closes it */
            in->key_hit = KEY_OSD;
            osd_toggle();
        } else if (e->button.button == SDL_BUTTON_LEFT && key_at(L->mouse_btn, mx, my)) {
            /* the MOUSE key: out on the case the host has the mouse, so a
             * press can only give it to the machine; CTRL+F10, printed over
             * the key, takes it back */
            in->key_hit = KEY_MOUSE;
            input_capture(in, a, 1);
            in->knob_drag = -1;
        } else if (e->button.button == SDL_BUTTON_LEFT && key_at(L->pwr_btn, mx, my)) {
            /* the power button: out on the case, it goes down under the hand
             * and stays down while held; the machine switches off when it is
             * let go of over the button, the way EXIT at the prompt does */
            in->key_held = KEY_POWER;
        } else if (bt >= 0 && e->button.button == SDL_BUTTON_LEFT) {
            in->button = bt;
            in->key_hit = KEY_MODE + bt;
            return INPUT_BUTTON;
        } else if (kn >= 0 && e->button.button == SDL_BUTTON_LEFT) {
            /* grab: remember where the hand and the knob started */
            in->knob_drag = kn;
            in->knob_y0 = my;
            in->knob_v0 = (kn == 0) ? k->brightness : (k->contrast - 0.4f) / 1.4f;
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (in->key_held == KEY_POWER && e->button.button == SDL_BUTTON_LEFT) {
            /* let go of: off, unless the hand slid away from it first */
            float mx = e->button.x * a->W / a->win_wf, my = e->button.y * a->H / a->win_hf;
            in->key_let_go = KEY_POWER;
            in->key_held = -1;
            if (key_at(L->pwr_btn, mx, my))
                return INPUT_POWER;
        } else if (machine_has_mouse(in) && setup_visible())
            setup_click(0);
        else if (machine_has_mouse(in) && catalog_visible())
            catalog_click(0);
        else if (machine_has_mouse(in) && dosbox_shown())
            dosbox_mouse_button(e->button.button, 0);
        else if (in->knob_drag >= 0)
            in->knob_drag = -1;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        /* A trackpad sends fractions of a notch, so they are added up until
         * they make one: otherwise a slow drag scrolls nothing at all. */
        if (machine_has_mouse(in) && (setup_visible() || catalog_visible())) {
            static float notch = 0.0f;
            notch += e->wheel.y;
            int n = (int)notch;
            notch -= (float)n;
            if (n && setup_visible())
                setup_wheel(n);
            else if (n)
                catalog_wheel(n);
        }
        break;
    case SDL_EVENT_MOUSE_MOTION: {
        float my = e->motion.y * a->H / a->win_hf;
        int machine = machine_has_mouse(in);
        if (machine && setup_visible())
            setup_mouse((int)e->motion.xrel, (int)e->motion.yrel);
        else if (machine && catalog_visible())
            catalog_mouse((int)e->motion.xrel, (int)e->motion.yrel);
        else if (machine && dosbox_shown())
            dosbox_mouse_move((int)e->motion.xrel, (int)e->motion.yrel);
        else if (machine) {
            /* held by the machine, before anything on the tube wants it */
        } else if (in->knob_drag >= 0)
            knob_turn(in, a, k, my);
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
    int want = in->captured;
    /* the keyboard follows the mouse: the desktop's shortcuts are off while
     * the machine has it, and back the moment it has not (keygrab.h) */
    keygrab_sync(a->win, want);
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
