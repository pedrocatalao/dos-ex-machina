#ifndef DXM_DOS_H
#define DXM_DOS_H
#include <stdint.h>
#define DOS_COLS 80
#define DOS_ROWS 25
/* The picture carries a border around the text, the way a real card's
 * overscan did: the whole area is lit and scanned, the characters just do
 * not run to the edge of the tube. */
#define DOS_PAD_X 20
#define DOS_PAD_Y 14
#define DOS_W (DOS_COLS * 8 + DOS_PAD_X * 2)
#define DOS_H (DOS_ROWS * 16 + DOS_PAD_Y * 2)
/* DOS_HANDOVER: the boot is done and a real DOS owns the tube from here;
 * this simulation has nothing more to show. */
typedef enum { DOS_BOOT, DOS_PROMPT, DOS_RUNNING, DOS_OFF, DOS_HANDOVER } dos_state;
void dos_init(void);
/* The boot ends on a cleared screen instead of at this prompt: a real DOS
 * (DOSBox) prints its own AUTOEXEC and takes over.  Call after dos_init. */
void dos_handover_mode(void);
void dos_key(int ch, int scancode);
int dos_nc_open(void); /* the navigator owns the keys */
dos_state dos_update(double t);
const uint8_t *dos_render(void);      /* DOS_W x DOS_H RGB8 */
/* A text screen that is not this simulation's - a real DOS's cells -
 * drawn with the machine's own font and cursor, so the two are the
 * same thing on the glass. */
const uint8_t *dos_render_text(const uint8_t *cells, int cur_col, int cur_row, int cur_on);
const char *dos_launch_request(void); /* non-NULL once, when a game starts */
void dos_core_exited(void);
/* the launch could not start at all - say so and give the prompt back */
void dos_core_failed(void);
int dos_take_beep(void);      /* 1 once when the POST beep should sound */
double dos_take_floppy(void); /* >0 once when the drive should run   */
#endif
