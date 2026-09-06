/* theatre.h — what the machine does that is not the picture: powering on
 * and off, the two LEDs, the drive running, and the room going dark.
 *
 * Power on: the mains switch, and the monitor's degauss thump as its coil
 * kicks in.  The picture then WARMS UP over the next second or so - small
 * and dim first, filling out as the tube comes to temperature - rather
 * than simply being there.  Power off runs the other way: switch, fans
 * spin down, the raster collapses to a line, the line to a dot, the dot
 * fades; then the room goes dark and the program ends. */
#ifndef DXM_THEATRE_H
#define DXM_THEATRE_H
#include "app.h"
#include "chassis.h"

typedef struct {
    Uint64 fade0;          /* when the machine came up out of the splash's black */
    double off_t0;         /* when power-off began; <0 while the machine is on */
    double drive_until;    /* the drive runs until this machine time */
    float  pwr;            /* the power LED, eased toward on or off */
    int    selftest;       /* no warm-up and no fade: the test wants the picture now */
    int    deterministic;  /* the drive LED follows the machine clock, not the audio */
} theatre;

void theatre_power_on(theatre *th,int selftest,int deterministic);
void theatre_power_off(theatre *th,double t);
/* The drive works for `seconds` from machine time t: sound, and the LED. */
void theatre_drive(theatre *th,double seconds,double t);
/* Per frame, before the picture: the tube's power state and both LEDs.
 * Returns 1 once the power-off sequence has run its course. */
int  theatre_frame(theatre *th,gpu *g,const dxm_layout *L,int W,int H,double t);
/* Per frame, after the picture: the fade up from black, the room going
 * dark after the tube has. */
void theatre_room(const theatre *th,gpu *g,double t);
#endif
