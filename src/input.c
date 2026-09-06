/* input.c — see input.h. */
#include "input.h"
#include "log.h"
#include "dos.h"
#include "corehost.h"
#include "ui.h"
#include "dxm_core.h"

static int sc_from_sdl(SDL_Scancode s) {
    switch (s) {
    case SDL_SCANCODE_ESCAPE:
        return DXM_SC_ESC;
    case SDL_SCANCODE_RETURN:
        return DXM_SC_ENTER;
    case SDL_SCANCODE_SPACE:
        return DXM_SC_SPACE;
    case SDL_SCANCODE_UP:
        return DXM_SC_UP;
    case SDL_SCANCODE_DOWN:
        return DXM_SC_DOWN;
    case SDL_SCANCODE_LEFT:
        return DXM_SC_LEFT;
    case SDL_SCANCODE_RIGHT:
        return DXM_SC_RIGHT;
    case SDL_SCANCODE_F9:
        return DXM_SC_F9;
    case SDL_SCANCODE_F10:
        return DXM_SC_F10;
    /* plain DOS scancodes; the navigator's key bar lives on these */
    case SDL_SCANCODE_F1:
        return 0x3B;
    case SDL_SCANCODE_F2:
        return 0x3C;
    case SDL_SCANCODE_F3:
        return 0x3D;
    case SDL_SCANCODE_F4:
        return 0x3E;
    case SDL_SCANCODE_TAB:
        return 0x0F;
    default:
        return 0;
    }
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

void input_capture(input_state *in, app *a, const dxm_layout *L, int on) {
    bool ok;
    in->captured = on;
    if (on) {
        SDL_Rect r = {(int)(L->tube_x * a->win_wf / a->W), (int)(L->tube_y * a->win_hf / a->H),
                      (int)(L->tube_w * a->win_wf / a->W), (int)(L->tube_h * a->win_hf / a->H)};
        ok = SDL_SetWindowMouseRect(a->win, &r);
    } else
        ok = SDL_SetWindowMouseRect(a->win, NULL);
    dxm_log("mouse %s%s%s", on ? "captured (confined to the glass)" : "released to the machine",
            ok ? "" : " - but SDL could not confine it: ", ok ? "" : SDL_GetError());
}

void input_init(input_state *in, app *a, const dxm_layout *L) {
    in->knob_drag = -1;
    in->knob_y0 = in->knob_v0 = 0.0f;
    input_capture(in, a, L, 1);
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
    if (toggle) {
        input_capture(in, a, L, !in->captured);
        in->knob_drag = -1;
    } else if (corehost_running()) {
        int ch = 0;
        if (sc == DXM_SC_ESC)
            ch = 27;
        else if (sc == DXM_SC_ENTER)
            ch = 13;
        else if (sc == DXM_SC_SPACE)
            ch = ' ';
        else if (sc)
            ch = 0x100 | sc;
        corehost_push_key(sc, down, ch);
    } else if (down) {
        /* dev-only room-light adjust while at the prompt: F5 darker, F6
         * brighter (the real fiction control is a chassis knob, SPEC 6.8 -
         * this is for tuning taste) */
        if (e->key.key == SDLK_F1 && !dos_nc_open()) {
            ui_toggle();
        } else if (e->key.key == SDLK_F5 || e->key.key == SDLK_F6) {
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
        else if (sc)
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
        if (kn >= 0 && e->button.button == SDL_BUTTON_LEFT) {
            /* grab: remember where the hand and the knob started */
            in->knob_drag = kn;
            in->knob_y0 = my;
            in->knob_v0 = (kn == 0) ? k->brightness : (k->contrast - 0.4f) / 1.4f;
        } else
            ui_mouse((int)mx, (int)my, 1, 0);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (in->knob_drag >= 0)
            in->knob_drag = -1;
        else
            ui_mouse((int)(e->button.x * a->W / a->win_wf), (int)(e->button.y * a->H / a->win_hf),
                     0, 0);
        break;
    case SDL_EVENT_MOUSE_MOTION: {
        float mx = e->motion.x * a->W / a->win_wf, my = e->motion.y * a->H / a->win_hf;
        if (in->knob_drag >= 0)
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
        if (!corehost_running())
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

void input_cursor(const input_state *in) {
    if (in->captured && !ui_visible())
        SDL_HideCursor();
    else
        SDL_ShowCursor();
}
