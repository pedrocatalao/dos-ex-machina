/* disk.h — C:\ as real files.
 *
 * The root of the machine's disk is a directory in the preferences folder,
 * seeded on first start with the files a 1993 boot disk had, so that DIR
 * lists what is really there and TYPE shows what is really in it - and so
 * a user can open AUTOEXEC.BAT in an editor and watch the machine change.
 * C:\GAMES is the games directory the library already keeps; it appears
 * here as a directory and lives where it always did.
 *
 * The shell only READS.  There is no COPY, DEL or EDIT: the disk is a place
 * to look, not a file manager. */
#ifndef DXM_DISK_H
#define DXM_DISK_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    char    name[13];        /* DOS 8.3, upper case */
    int     is_dir;
    long    size;
    int64_t mtime_ns;        /* 0 if unknown */
} disk_entry;

/* create C:\ if missing and put back any seed file that is gone */
void disk_init(void);
/* the root's entries, seeds first in their classic order, then the rest;
 * GAMES last.  Returns the count. */
int  disk_list(disk_entry *out, int max);
/* read a root file by its DOS name (case-insensitive) into buf as text.
 * 0 = ok, -1 = not found, 1 = binary (buf untouched). */
int  disk_read(const char *dosname, char *buf, size_t n);
/* the ECHO lines of AUTOEXEC.BAT, in order, for the boot to print */
int  disk_autoexec_echo(char (*lines)[80], int max);
#endif
