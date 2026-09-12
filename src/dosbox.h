/* dosbox.h — the public face of src/dosbox/: a real DOS behind the glass.
 *
 * DOSBox Pure, opened at run time as a libretro core, runs a whole PC -
 * the CPU, the VGA card, the sound cards, COMMAND.COM.  What comes out is
 * what came down the monitor cable, a framebuffer, plus what came out of
 * the speaker jack, and that is all the machine takes from it.  The
 * picture goes through the tube like anything else; the case, the glow
 * and the theatre never know the difference.
 *
 * The emulator boots hidden behind the machine's own POST, and the
 * handover happens on a cleared screen: DOS's first prompt is the first
 * thing of DOSBox's anyone sees. */
#ifndef DXM_DOSBOX_H
#define DXM_DOSBOX_H
#include <stdint.h>

/* Open the core and boot it with `c_drive` mounted as C:.  A NULL
 * core_path looks beside the executable (in a macOS bundle, in
 * Contents/Frameworks) and then in the preferences directory.  The picture and the sound stay
 * hidden until dosbox_show(). 0 on success; the reason is logged otherwise. */
int dosbox_start(const char *core_path, const char *c_drive, const char *pref_dir);
void dosbox_stop(void);   /* unwind the emulator and join its thread */
int dosbox_running(void); /* booted, or still booting */
int dosbox_exited(void);  /* DOS was told EXIT: the machine should power off */

/* The handover: from here on the tube shows DOSBox and the speaker plays it. */
void dosbox_show(void);
int dosbox_shown(void);

/* The latest frame, RGB8, with the physical line count for the tube.  NULL
 * until the emulator has drawn one.  The buffer stays valid - nobody
 * writes it - until dosbox_frame_done(). */
const uint8_t *dosbox_frame(int *w, int *h, int *crt_lines);
void dosbox_frame_done(void);
/* the frame being shown is a text mode, as far as its geometry says */
int dosbox_text_mode(void);

/* Input, straight from the SDL event: the SDL scancode, relative mouse
 * motion, and SDL's button numbering. */
void dosbox_key(int sdl_scancode, int down);
void dosbox_mouse_move(int dx, int dy);
void dosbox_mouse_button(int sdl_button, int down);

/* Type a string into DOS, a key per frame, as if from the keyboard: ASCII
 * on a US layout, '\r' for Enter.  For --type and the tests. */
void dosbox_type(const char *s);
/* The emulated CPU's speed, in DOSBox cycles (instructions per
 * millisecond).  Takes effect on the core's next frame; set before
 * dosbox_start() it is what the machine boots with. */
void dosbox_set_cycles(int cycles);
/* The same speed as the turbo display reads it, for the BIOS screen the
 * core draws after the handover. */
void dosbox_set_mhz(int mhz);
/* Force the DOS keyboard layout ("us", "po", "fr", ...) instead of the one
 * guessed from the host's keyboard.  Set before dosbox_start(). */
void dosbox_set_layout(const char *code);
/* Answer one of the core's own settings with this instead of the machine's
 * default - memory size, CPU core and the like, all of which the core reads
 * as it starts.  Set before dosbox_start(); later has no effect. */
void dosbox_set_option(const char *key, const char *value);
/* Seconds of drive the core's BIOS screen asked for, taken once. */
double dosbox_take_floppy(void);

/* Pull for the audio device: 16-bit stereo at DOSBOX_AUDIO_HZ.  Silent
 * until shown. */
#define DOSBOX_AUDIO_HZ 44100
void dosbox_audio(int16_t *out, int frames);
#endif
