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
#include <stdint.h>

void setup_open(void);
void setup_close(void);
int setup_visible(void);

/* Input while it is up.  The key is an SDL scancode; the mouse arrives as
 * the relative motion the machine already holds, and buttons as a bitmask
 * of 1 (left) and 2 (right). */
void setup_key(int sdl_scancode);
void setup_mouse(int dx, int dy, int buttons);

/* The picture, for the tube: 640x480 RGB8. */
const uint8_t *setup_render(int *w, int *h);
#endif
