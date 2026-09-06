/* input.h — the keyboard and the mouse.
 *
 * Keys go to the running game as XT scancodes, or to the DOS prompt.  The
 * mouse belongs either to the machine - confined to the glass, unseen,
 * which is how it starts and how a game has it - or to the operating
 * system, where the arrow shows and turns the knobs.  Ctrl+F10 switches,
 * as it does in DOSBox. */
#ifndef DXM_INPUT_H
#define DXM_INPUT_H
#include "app.h"
#include "chassis.h"

typedef struct {
    int captured;           /* the machine holds the mouse */
    int knob_drag;          /* the knob being turned, or -1 */
    float knob_y0, knob_v0; /* where the hand and the knob started */
} input_state;

typedef enum { INPUT_HANDLED, INPUT_QUIT, INPUT_RESIZED } input_result;

/* The machine takes the mouse from the start. */
void input_init(input_state *in, app *a, const dxm_layout *L);
void input_capture(input_state *in, app *a, const dxm_layout *L, int on);
/* One event.  INPUT_RESIZED means the drawable changed size and the
 * caller must lay the machine out again; nothing else escapes. */
input_result input_event(input_state *in, app *a, const dxm_layout *L, gpu_knobs *k,
                         const SDL_Event *e);
/* The arrow shows only while the mouse has been given to the operating
 * system, or over the panel. */
void input_cursor(const input_state *in);
#endif
