/* unzip.h — see unzip.c.  Extracts flat, into destdir (which must end in a
 * separator).  Refuses archives whose entry names could escape it. */
#ifndef DXM_UNZIP_H
#define DXM_UNZIP_H
#include <stdint.h>
#include <stddef.h>
int unzip_extract(const char *zip, const char *destdir, int *n_out,
                  char *err, size_t errsz);
#endif
