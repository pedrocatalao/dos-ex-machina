/* sha256.h — see sha256.c. */
#ifndef DXM_SHA256_H
#define DXM_SHA256_H
#include <stdint.h>
#include <stddef.h>

typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; size_t n; } sha256;

void sha256_init(sha256 *c);
void sha256_update(sha256 *c, const void *data, size_t n);
void sha256_final(sha256 *c, char out[65]);       /* lowercase hex, NUL-terminated */

int  sha256_file(const char *path, char out[65]);
/* 1 if the file hashes to `want`.  An empty `want` passes: the catalogue is
 * allowed to omit a hash, and refusing would be worse than trusting it. */
int  sha256_matches(const char *path, const char *want);

#endif
