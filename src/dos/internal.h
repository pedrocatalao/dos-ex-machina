/* internal.h — shared between the files of the boot, and nothing outside
 * src/dos.  The public interface is src/dos.h. */
#ifndef DXM_DOS_INTERNAL_H
#define DXM_DOS_INTERNAL_H
#include "dos.h"
#include "term.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* What the boot sequence changes and main.c asks about each frame. */
typedef struct {
    dos_state state;
    double now;        /* the time of the last dos_update */
    double floppy_req; /* seconds of drive activity wanted, taken by main */
    int beep_pending;  /* the POST beep, taken by main */
} dos_machine;
extern dos_machine machine;

/* boot.c - the POST */
void boot_init(int mhz, int fixed_clock);
void boot_skip(void);      /* a key: no more waiting between lines */
int boot_update(double t); /* 1 the moment the screen should clear */
void boot_badge(double t); /* the POST badge, over the frame buffer */
#endif
