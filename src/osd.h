/* osd.h — the monitor's on-screen display.
 *
 * The monitor's own menu, not the PC's: generated inside the monitor and
 * mixed into the picture before the tube, so it sits on the glass with the
 * scanlines and the curvature, over whatever the machine is showing - the
 * POST, DOS, a game, SETUP, CATALOG.  It is worked from the case and the
 * keyboard, never the machine's mouse: the OSD key opens and closes it,
 * the arrows move and adjust, TAB changes page, ESC puts it away.  All the
 * tube's settings, on three pages; their values persist in crt.cfg. */
#ifndef DXM_OSD_H
#define DXM_OSD_H
#include <stdint.h>
#include "gpu.h"

void osd_init(gpu_knobs *k); /* binds the settings to the knobs */
int osd_visible(void);
void osd_toggle(void); /* the OSD key, and Shift+F1 */
/* A key going down while it is up: 1 if the OSD took it.  Arrows move and
 * adjust one step in a hundred (Shift for ten), TAB and Shift+TAB (and
 * PAGE UP/DOWN) change page, ESC closes. */
int osd_key(int sdl_scancode, int shift);
/* The picture of it, RGBA over the whole tube, OSD_W x OSD_H, and 1 in
 * *changed when it differs from the last call's; NULL while hidden. */
#define OSD_W 640
#define OSD_H 480
const uint8_t *osd_frame(int *changed);
void osd_load(const char *path);
void osd_save(const char *path);
#endif
