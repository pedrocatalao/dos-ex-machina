/* dos.h — the machine's own boot: the BIOS screen it draws before the real
 * DOS takes the tube.  There is no shell here any more; DOSBox is the DOS.
 * What remains is the POST theatre and the text screen it is drawn on,
 * which the boot ends by clearing (DOS_HANDOVER) for DOSBox to draw the
 * next thing. */
#ifndef DXM_DOS_H
#define DXM_DOS_H
#include <stdint.h>
#define DOS_COLS 80
#define DOS_ROWS 25
/* The picture carries a border around the text, the way a real card's
 * overscan did: the whole area is lit and scanned, the characters just do
 * not run to the edge of the tube.  DOSBox's text modes wear the same
 * border in the same proportion (dosbox.c), so the handover does not move
 * a pixel. */
#define DOS_PAD_X 14
#define DOS_PAD_Y 12
#define DOS_W (DOS_COLS * 8 + DOS_PAD_X * 2)
#define DOS_H (DOS_ROWS * 16 + DOS_PAD_Y * 2)
typedef enum { DOS_BOOT, DOS_HANDOVER } dos_state;
/* the CPU clock the POST reports, and 1 to read its clock as a fixed time */
void dos_init(int mhz, int fixed_clock);
void dos_skip(void); /* a key during the boot: no more waiting between lines */
dos_state dos_update(double t);
const uint8_t *dos_render(void); /* DOS_W x DOS_H RGB8 */
int dos_take_beep(void);         /* 1 once when the POST beep should sound */
double dos_take_floppy(void);    /* >0 once when the drive should run   */
#endif
