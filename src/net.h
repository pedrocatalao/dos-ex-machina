/* net.h — see net.c.  The only file in DXM that talks to the network. */
#ifndef DXM_NET_H
#define DXM_NET_H
#include <stddef.h>

/* A catalogue is a few KB; anything claiming to be megabytes is not one. */
#define NET_MAX_MEM (4u*1024u*1024u)

void net_init(void);
void net_quit(void);

/* got/total in bytes; total is 0 until the server says how big it is. */
typedef void (*net_progress)(void *ud, double got, double total);

/* Fetch to a file.  Writes to <path>.part and renames on success, so a
 * failed download never leaves something that looks finished.  Set *cancel
 * non-zero from another thread to abort. */
int net_get_file(const char *url, const char *path, net_progress cb, void *ud,
                 volatile int *cancel, char *err, size_t errsz);

/* Fetch into memory (the catalogue).  Caller frees *out. */
int net_get_mem(const char *url, char **out, size_t *outn,
                char *err, size_t errsz);
#endif
