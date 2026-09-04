#ifndef DXM_FONT_H
#define DXM_FONT_H
#include <stdint.h>
/* 8x8 CP437 glyphs, all 256, rendered at 8x16 (row-doubled) - the way CGA/EGA
 * text modes actually looked.  MSB = leftmost pixel. */
const uint8_t *font_glyph(int ch);
#endif
