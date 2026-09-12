/* setup.h — the machine's own configuration, the way a BIOS setup was:
 * the boot stops, the tube is the program's, and nothing of the desktop
 * shows through.
 *
 * It is not a DOS program.  DOSBox neither draws it nor knows it happened;
 * the machine takes the tube back for as long as it is up, in 640x480 and
 * 256 colours, and hands it back on the way out.  SPACE during the POST
 * opens it, and later the SETUP command at the DOS prompt will. */
#ifndef DXM_SETUP_H
#define DXM_SETUP_H
#include "gpu.h"
#include <stdint.h>

/* What SETUP is allowed to change, handed over once at startup: the live
 * CRT parameters - the same struct the panel writes and the shader reads,
 * so a slider moved here shows on the glass at once - and where they are
 * kept between runs. */
void setup_bind(gpu_knobs *knobs, const char *crt_cfg);

void setup_open(void);
void setup_close(void);
int setup_visible(void);
void setup_save(void); /* keep what was changed, where it is kept */

/* Input while it is up.  The key is an SDL scancode, with the shift state
 * for the coarse step; the mouse arrives as the relative motion the machine
 * already holds, and the left button as it goes down and comes up. */
void setup_key(int sdl_scancode, int shift);
void setup_mouse(int dx, int dy);
void setup_click(int down);
void setup_wheel(int by); /* notches, positive away from the hand */

/* The picture, for the tube: 640x480 RGB8. */
const uint8_t *setup_render(int *w, int *h);
#endif
