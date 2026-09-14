/* internal.h — shared inside src/dosbox only.  The public interface is
 * src/dosbox.h. */
#ifndef DXM_DOSBOX_INTERNAL_H
#define DXM_DOSBOX_INTERNAL_H
#include <stddef.h>
/* keys.c: the libretro key an SDL scancode stands for; 0 for none */
unsigned dosbox_retrok_of_sdl(int sdl_scancode);
/* layout.c: the DOS keyboard layout the host's own keyboard asks for,
 * guessed from what SDL says its keys produce; "us" when nothing matches */
const char *dosbox_layout_detect(void);
/* motd.c: the message of the day, the line above the greeting.  Drawn at
 * each boot, from a MOTD.TXT in the root of C: if there is one and from
 * the shipped set otherwise. */
void motd_today(const char *c_drive, char *buf, size_t n);
#endif
