/* splash.h — the startup splash, and the chassis build it covers.
 *
 * chassis_render() is the one genuinely slow thing at startup - a few
 * million pixels of signed-distance work - and it touches no GL, so it
 * runs on a worker while the main thread holds the splash up.  Doing it
 * inline would freeze the fade for its whole duration. */
#ifndef DXM_SPLASH_H
#define DXM_SPLASH_H
#include "app.h"
#include "chassis.h"
#include <stdint.h>

/* The worker reports completion itself, through `done`; that also covers
 * the no-threads fallback, where the work has already happened inline. */
typedef struct { int W,H; dxm_layout L; uint8_t *px;
                 volatile int done; Uint64 ms; } chassis_job;

/* Start building the case for a W x H display.  Returns the thread to
 * join, or NULL when it had to run inline (already done on return). */
SDL_Thread *chassis_build_begin(chassis_job *job,int W,int H);
/* Wait for it and log the outcome; job->px is NULL if it failed. */
void chassis_build_join(SDL_Thread *th,chassis_job *job);

/* Fast in, hold until the case is ready, then out.  Returns 1 if the user
 * closed the window meanwhile. */
int splash_show(app *a,const chassis_job *job);
#endif
