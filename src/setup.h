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
void setup_bind(gpu_knobs *knobs, const char *crt_cfg, const char *dxm_cfg, const char *c_drive);
/* Under --deterministic the screen has to be the same picture on every run:
 * the first piece of artwork rather than one drawn at random, and no
 * loading screen or fade, both of which are timed by the wall clock. */
void setup_fixed_clock(int fixed);

/* What the machine is set to.  None of it can change under a running DOS,
 * so main.c reads it before the core starts and tells the core then; SETUP
 * only says what it will be the next time the machine is switched on. */
void setup_load(void);
const char *setup_keyboard(void); /* "auto", or a DOS keyboard layout */
const char *setup_memory(void);   /* megabytes, as the core wants it */
const char *setup_cpu_core(void);
const char *setup_midi(void); /* which MIDI device, or none */
int setup_boot_catalogue(void);

void setup_open(void);
void setup_close(void);
int setup_visible(void);
void setup_save(void); /* keep what was changed, where it is kept */
/* 1 once when SAVE & REBOOT was chosen: the settings it keeps are read
 * as the machine starts, so the machine has to start again. */
int setup_take_reboot(void);

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
