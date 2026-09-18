/* keygrab.h — the desktop's own shortcuts, while the machine has the keys.
 *
 * A DOS program expects every key it is given: Ctrl with the arrows moves
 * a word in an editor, Alt with a letter opens a menu, Alt+Tab and the
 * function keys mean whatever the program says.  A desktop takes a good
 * many of those for itself before the window ever sees them - switching
 * spaces, showing the desktop, cycling windows.
 *
 * One rule, the same on every system: while the machine holds the mouse
 * and its window has the focus, the desktop's shortcuts are off and the
 * keys are the machine's.  Ctrl+F10 hands the mouse back and the keys
 * with it; so does the window losing the focus, and so does quitting.
 * The keyboard follows the mouse, so there is one thing to know and one
 * way out.
 *
 * What is underneath differs.  On Windows and Linux SDL's keyboard grab
 * does all of it.  On macOS SDL's grab does nothing unless SDL was built
 * for it, so the window server is asked directly to stand its hot keys
 * down - the call virtual machines and remote desktops use, looked up
 * when first needed rather than linked, so a system without it costs a
 * line in the log and nothing else.  The request lives with this
 * program's connection to the window server: if the program dies holding
 * it, the system takes its shortcuts back by itself. */
#ifndef DXM_KEYGRAB_H
#define DXM_KEYGRAB_H
#include <SDL3/SDL.h>

/* Make the desktop's shortcuts match who has the keys: `machine` is 1
 * while the machine holds the mouse.  Call it once a frame; it looks at
 * the window's focus itself, and does nothing unless the answer changed. */
void keygrab_sync(SDL_Window *win, int machine);
#endif
