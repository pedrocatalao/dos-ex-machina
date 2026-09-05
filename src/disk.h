/* disk.h — C:\ as the machine sees it.
 *
 * A small virtual disk: the files a 1993 boot disk had, compiled into the
 * program, so that DIR has something honest to list and TYPE something to
 * show.  Nothing is written anywhere and nothing can be changed - the
 * disk is part of the machine, not a folder on the host.  C:\GAMES is the
 * games directory the library keeps, and appears here as a directory. */
#ifndef DXM_DISK_H
#define DXM_DISK_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    const char *name;        /* DOS 8.3, upper case */
    int         is_dir;
    long        size;
    const char *date, *time; /* as DIR prints them */
} disk_entry;

/* the root's entries, in their classic order; GAMES last.  Returns the
 * count.  Entries point at static storage. */
int  disk_list(disk_entry *out, int max);
/* read a root file by its DOS name (case-insensitive) as text.
 * 0 = ok, -1 = not found, 1 = a program (buf untouched). */
int  disk_read(const char *dosname, char *buf, size_t n);
/* the ECHO lines of AUTOEXEC.BAT, in order, for the boot to print */
int  disk_autoexec_echo(char (*lines)[80], int max);
#endif
