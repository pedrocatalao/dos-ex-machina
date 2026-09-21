/* dosname.h — what DOS makes of a name before it goes looking for it.
 *
 * DOS_MakeName in the core takes whatever is typed, drops the spaces, puts
 * it in upper case, keeps only the first dot, and cuts the stem to eight
 * characters with three after the dot.  It does that to the *request*, so a
 * file whose name on the disk survives the same treatment unchanged is one
 * DOS can reach, and one that comes back shorter is not: DOS will look for
 * the cut-down name and find nothing.
 *
 * That matters when a title is unpacked from an archive.  A folder called
 * "PREHISTORIK 2 [REPLAYERS.ORG]" reaches DOS as PREHISTO.ORG and matches
 * nothing, so CD fails and the title will not start.  The core does give
 * such a folder a PREHIS~1 of its own, but which number it gets depends on
 * what else is in the directory, so it is not something to write down; the
 * machine renames the folder to what DOS would ask for instead.  That is
 * safe precisely because the request is cut the same way - nothing that
 * resolved before stops resolving.
 *
 * Nothing here touches the disk, SDL or the catalogue. */
#ifndef DXM_DOSNAME_H
#define DXM_DOSNAME_H
#include <stddef.h>

/* The name DOS would ask for, given this one.  Always something DOS can
 * reach, even where DOS would have refused the original outright - a second
 * dot is dropped rather than reproduced, since the point is to produce a
 * usable name rather than to repeat a failure.  `out` wants DOS_NAME room. */
#define DOS_NAME 13 /* eight, a dot, three, and the NUL */
void dos_name(const char *in, char *out, size_t n);

/* Whether DOS can reach `name` as it stands.  Case does not count: the core
 * upper-cases both what is typed and the name on the disk before comparing
 * them, so `prez.exe` is reached perfectly well by PREZ.EXE. */
int dos_reachable(const char *name);

/* ---- paths made of those names ---------------------------------------- */

/* The name at the end of a path, whichever way its separators lean. */
const char *dos_leaf(const char *path);

/* A path without its trailing separator, in place.  A drive's root keeps
 * its own: C: is not a folder and C:\ is. */
void dos_trim_sep(char *p);

/* The folder above `at`, or 0 when there is nothing above it - the root of
 * a disk on one system, a drive letter on another.  The separator stays on
 * the end, since that is how a folder is named here. */
int dos_up(const char *at, char *out, size_t n);

/* Upper case, in place: every DOS name the machine writes down is. */
void dos_upper(char *p);

/* Whether DOS would start this: .EXE, .COM or .BAT, in any case. */
int dos_is_program(const char *name);

#endif
