/* input.h — the keyboard and the mouse.
 *
 * Keys go to the running game as XT scancodes, or to the DOS prompt.  The
 * mouse belongs either to the machine - locked to the window and off the
 * desktop entirely, which is how it starts and how a game has it - or to
 * the operating system, where the arrow shows and turns the knobs.
 * Ctrl+F10 switches, as it does in DOSBox. */
#ifndef DXM_INPUT_H
#define DXM_INPUT_H
#include "app.h"
#include "chassis.h"

typedef struct {
    int captured;           /* the machine has been given the mouse */
    int holding;            /* relative mode as SDL currently has it, -1 unknown */
    int knob_drag;          /* the knob being turned, or -1 */
    float knob_y0, knob_v0; /* where the hand and the knob started */
    int button;             /* the display button just pressed, for INPUT_BUTTON */
} input_state;

typedef enum { INPUT_HANDLED, INPUT_QUIT, INPUT_RESIZED, INPUT_BUTTON } input_result;

/* The machine takes the mouse from the start. */
void input_init(input_state *in, app *a);
void input_capture(input_state *in, app *a, int on);
/* One event.  INPUT_RESIZED means the drawable changed size and the
 * caller must lay the machine out again; INPUT_BUTTON that one of the
 * display's buttons was pressed, which one in in->button.  Nothing else
 * escapes. */
input_result input_event(input_state *in, app *a, const dxm_layout *L, gpu_knobs *k,
                         const SDL_Event *e);
/* Make SDL's idea of who holds the mouse match this state; call it once a
 * frame.  The arrow exists only while the mouse has been given back to the
 * operating system, or while the panel is up. */
void input_mouse_sync(input_state *in, app *a);
#endif
