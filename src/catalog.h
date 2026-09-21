/* catalog.h — CATALOG, the program: the shop window onto the catalogues
 * (SPEC §8.3).  Its data is catalogue.h; this is the screen.
 *
 * Like SETUP it is not a DOS program: `CATALOG` at the prompt is a stub in
 * the fork that asks the machine to put this screen up and waits.  Unlike
 * SETUP it sends DOS on errands while it is up - run a title, run its
 * setup, open a prompt in its directory - and comes back as it was when
 * they return.  Drawn on the same canvas as SETUP, in 640x400 and 256
 * colours, with a look of its own. */
#ifndef DXM_CATALOG_H
#define DXM_CATALOG_H
#include <stddef.h>
#include <stdint.h>

/* Where things are: the catalogue files beside the program, the
 * preferences directory (artwork, downloads, and the note of where each
 * title ended up), and the folder that is C:, which is the only drive the
 * machine mounts. */
void catalog_bind(const char *base_dir, const char *pref_dir, const char *c_drive);
void catalog_load(void); /* read the list and every catalogue in it */
void catalog_fixed_clock(int fixed);

void catalog_open(void);
void catalog_close(void);
int catalog_running(void); /* the program is up, on screen or on an errand */
int catalog_visible(void); /* the tube is its */
void catalog_back(void);   /* the errand is done: the screen returns */

void catalog_key(int sdl_scancode, int shift);
void catalog_mouse(int dx, int dy);
void catalog_click(int down);
void catalog_wheel(int by);
void catalog_mouse_held(int held); /* as setup_mouse_held */

/* the picture for the tube, with its overscan */
const uint8_t *catalog_render(int *w, int *h);
#endif
