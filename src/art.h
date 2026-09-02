/* art.h — see art.c.
 * Returns RGBA pixels for a catalogue image, or NULL while it is still being
 * fetched - callers draw their placeholder until it turns up. */
#ifndef DXM_ART_H
#define DXM_ART_H
#include <stdint.h>
#include "catalog.h"
const uint8_t *art_get(const cat_file *f, int *w, int *h);
#endif
