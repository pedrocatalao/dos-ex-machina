/* png.h — see png.c.  Returns a w*h RGBA buffer the caller frees. */
#ifndef DXM_PNG_H
#define DXM_PNG_H
#include <stdint.h>
#include <stddef.h>
uint8_t *png_decode(const uint8_t *data, size_t n, int *w, int *h,
                    char *err, size_t errsz);
uint8_t *png_load(const char *path, int *w, int *h, char *err, size_t errsz);
#endif
