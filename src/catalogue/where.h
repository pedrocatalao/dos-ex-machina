/* where.h — where each title ended up on this machine.
 *
 * The catalogues say what a title is; this says where it is.  The two are
 * kept apart on purpose: a path on a DOS drive is a fact about one person's
 * disk, and a catalogue is a file that can be passed on, so a catalogue
 * never carries one.  That holds for both kinds of catalogue - a download
 * installed somewhere of your choosing, and a folder on a drive you pointed
 * the machine at, are the same kind of fact and are written to the same
 * place.
 *
 * It lives beside crt.cfg and dxm.cfg in the machine's own folder, in the
 * same shape those use, and is meant to be readable if anybody looks. */
#ifndef DXM_WHERE_H
#define DXM_WHERE_H
#include <stddef.h>

/* A DOS path, with room to spare rather than exactly: DOS itself will not
 * take one past DOS_PATH_MAX, and a title unpacked from an archive with a
 * tree of its own adds that tree's folders on the end of the one chosen. */
#define DOS_PATH_MAX 78 /* the drive and the 76 DOSBox's DOS_PATHLENGTH leaves */
#define WHERE_PATH 160

/* Read the file; missing is an empty table, not a fault.  The path is kept,
 * so what is changed afterwards can be written back without being told
 * again where it goes. */
void where_load(const char *path);

/* Where a title is, as a DOS path with its drive letter, or NULL when this
 * machine has never put it anywhere.
 *
 * This is the directory to start it in, which is not always where its
 * archive was unpacked: an archive that carries a tree of its own keeps the
 * tree, and the title then lives in one of its folders. */
const char *where_get(const char *catalogue, const char *category, const char *id);

/* Whether the machine made that directory, as against having been pointed
 * at one that was already there.  Nothing acts on this yet; it is recorded
 * because it cannot be worked out afterwards, and anything that ever
 * removes a directory will need to know. */
int where_made(const char *catalogue, const char *category, const char *id);

/* Remember, forget, and forget everything belonging to one catalogue - for
 * when the catalogue itself goes.  Each writes the file at once, the way
 * SETUP saves a setting the moment it is changed. */
void where_set(const char *catalogue, const char *category, const char *id, const char *dos,
               int made);
void where_forget(const char *catalogue, const char *category, const char *id);
void where_forget_catalogue(const char *catalogue);

#endif
