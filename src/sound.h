/* sound.h — everything the machine itself makes noise with (SPEC §9).  The
 * DOS's own sound comes from DOSBox; this is the case around it: the POST
 * beep, the PSU fan, the disk spindle and the floppy drive. */
#ifndef DXM_SOUND_H
#define DXM_SOUND_H
#include <stdint.h>

void snd_init(int rate);
void snd_power(int on);                  /* fans + spindle spin up / down       */
void snd_beep(double ms);                /* PC speaker POST beep                */
void snd_relay(void);                    /* the mains switch: a mechanical clack */
void snd_degauss(void);                  /* the monitor's coil thump at power-on */
void snd_disk(double seconds);           /* (retired: head seeks removed)       */
void snd_floppy(double seconds);         /* 3.5" drive: motor + stepper buzz    */
float snd_floppy_level(void);            /* 0..1, for driving the activity LED  */
void snd_mix(int16_t *out, int nframes); /* additive, s16 stereo           */
#endif
