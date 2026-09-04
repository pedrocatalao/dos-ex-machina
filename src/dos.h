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
#define DOS_W (DOS_COLS*8  + DOS_PAD_X*2)
#define DOS_H (DOS_ROWS*16 + DOS_PAD_Y*2)
typedef enum { DOS_BOOT, DOS_PROMPT, DOS_RUNNING, DOS_OFF } dos_state;
void       dos_init(void);
void       dos_key(int ch, int scancode);
int        dos_nc_open(void);           /* the navigator owns the keys */
dos_state  dos_update(double t);
const uint8_t *dos_render(void);        /* DOS_W x DOS_H RGB8 */
const char *dos_launch_request(void);   /* non-NULL once, when a game starts */
void       dos_core_exited(void);
/* the launch could not start at all - say so and give the prompt back */
void       dos_core_failed(void);
int        dos_take_beep(void);    /* 1 once when the POST beep should sound */
double     dos_take_floppy(void);  /* >0 once when the drive should run   */
#endif
