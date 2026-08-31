#ifndef DXM_DOS_H
#define DXM_DOS_H
#include <stdint.h>
#define DOS_COLS 80
#define DOS_ROWS 25
#define DOS_W (DOS_COLS*8)
#define DOS_H (DOS_ROWS*16)
typedef enum { DOS_BOOT, DOS_PROMPT, DOS_RUNNING, DOS_OFF } dos_state;
void       dos_init(void);
void       dos_key(int ch, int scancode);
dos_state  dos_update(double t);
const uint8_t *dos_render(void);        /* DOS_W x DOS_H RGB8 */
const char *dos_launch_request(void);   /* non-NULL once, when a game starts */
void       dos_core_exited(void);
int        dos_take_beep(void);    /* 1 once when the POST beep should sound */
double     dos_take_floppy(void);  /* >0 once when the drive should run   */
#endif
