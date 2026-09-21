/* artfind.h — finding a picture for a title that has not got one.
 *
 * A catalogue of things already on this computer is put together by the
 * person sitting at it, from a folder they unzipped, and that person almost
 * never has a picture of the game to hand.  The picture box would be a
 * plate for ever.  So the machine offers to go and look.
 *
 * It looks on archive.org, and not at a search engine, for three reasons
 * that all point the same way.  Its software library can be asked for MS-DOS
 * items specifically - `collection:softwarelibrary_msdos` - rather than
 * hoping the words "ms-dos" steer a general search; it answers in JSON over
 * a documented address with no key to register for; and what it holds for a
 * DOS item is a screenshot taken from the game running, which is a far
 * better thing to put on a 486 than a photograph of a boxed DVD.
 *
 * Nothing is taken automatically.  Matching on a name alone is right most
 * of the time and confidently wrong the rest - asking for Lemmings offers
 * The Lemmings Chronicles first - so this puts up what it found, shows the
 * picture belonging to whichever row the cursor is on, and waits.  It is
 * the same shape as browse.h: a panel over the editor that knows nothing
 * about catalogues, and hands back an answer for the caller to use. */
#ifndef DXM_ARTFIND_H
#define DXM_ARTFIND_H

/* Look for `name`, keeping what it downloads under `pref_dir`. */
void artfind_open(const char *name, const char *pref_dir);
void artfind_close(void);

int artfind_up(void); /* one is on screen, and has the keys */
void artfind_key(int sdl_scancode, int shift);
void artfind_click(int what);
int artfind_hit(int x, int y); /* what is under the pointer, or -1 */
void artfind_draw(void);       /* which also lays out what can be hit */

/* ---- what it hands back, which the caller implements ------------------- */

/* The picture that was chosen: where it lives, and what it hashed to when
 * the machine fetched it - the two things a catalogue records about one. */
void artfind_took(const char *url, const char *sha256);
/* ESC, or a click on Cancel. */
void artfind_gave_up(void);
/* F3: none of these is the picture, and there is one on this computer.  The
 * panel is already down by the time this is called. */
void artfind_wants_file(void);

#endif
