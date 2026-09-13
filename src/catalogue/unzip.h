/* unzip.h — see unzip.c.  Extracts an archive under `destdir`, tree and
 * all, refusing any entry whose name could land it anywhere else. */
#ifndef DXM_UNZIP_H
#define DXM_UNZIP_H
#include <stddef.h>
/* `destdir` must exist and end in a separator.  Directories in the archive
 * are created under it as they are met.  0 on success; `err` says why not. */
int unzip_extract(const char *zip, const char *destdir, int *n_out, char *err, size_t errsz);
#endif
