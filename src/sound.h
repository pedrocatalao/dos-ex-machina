/* sound.h — everything the SHELL makes noise with (SPEC §9).  The game core
 * renders its own audio; this is the machine around it: PC speaker, PSU fan,
 * hard disk spindle and head seeks. */
#ifndef DXM_SOUND_H
#define DXM_SOUND_H
#include <stdint.h>

void snd_init(int rate);
void snd_power(int on);              /* fans + spindle spin up / down       */
void snd_beep(double ms);            /* PC speaker POST beep                */
void snd_disk(double seconds);       /* head seek chatter for a while       */
void snd_mix(int16_t *out,int nframes);   /* additive, s16 stereo           */
#endif
