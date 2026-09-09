/* internal.h — shared inside src/dosbox only.  The public interface is
 * src/dosbox.h. */
#ifndef DXM_DOSBOX_INTERNAL_H
#define DXM_DOSBOX_INTERNAL_H
/* keys.c: the libretro key an SDL scancode stands for; 0 for none */
unsigned dosbox_retrok_of_sdl(int sdl_scancode);
#endif
