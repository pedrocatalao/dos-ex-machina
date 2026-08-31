/* ui.h — the CRT settings panel.
 *
 * A dev/tuning surface, deliberately outside the fiction (SPEC §2.1 bans UI
 * chrome from the *machine*).  Long term these belong on chassis knobs or a
 * CRT.EXE utility; until the look is settled, adjusting ten interacting
 * parameters by editing constants and rebuilding is the wrong loop. */
#ifndef DXM_UI_H
#define DXM_UI_H
#include <stdint.h>
#include "gpu.h"

void        ui_init(gpu_knobs *k);        /* binds the panel to the knobs   */
int         ui_visible(void);
void        ui_toggle(void);
/* mouse in OUTPUT pixels; returns 1 if the panel consumed the event */
int         ui_mouse(int x,int y,int down,int moving);
/* the panel bitmap, or NULL when hidden; w/h in output pixels */
const uint8_t *ui_render(int out_w,int out_h,int *w,int *h);
void        ui_load(const char *path);
void        ui_save(const char *path);
#endif
