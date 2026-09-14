/* cursor.h — the arrow the mouse wears when it is out on the case: the
 * chunky black-and-white pointer a DOS mouse driver drew, not the
 * desktop's. */
#ifndef DXM_CURSOR_H
#define DXM_CURSOR_H
#include <SDL3/SDL.h>
/* Build the pointer once.  NULL if SDL would not make it, and the desktop's
 * own arrow serves. */
SDL_Cursor *cursor_vintage(void);
#endif
