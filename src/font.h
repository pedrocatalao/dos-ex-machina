#ifndef DXM_FONT_H
#define DXM_FONT_H
#include <stdint.h>
/* Two faces, both code page 437, all 256 glyphs, MSB = leftmost pixel.
 * The 8x8 is for lettering on the case and the panel.  The 8x16 is the VGA
 * character generator's own, and is what the text screen draws with - so
 * the BIOS, the prompt and a real DOS's text are the same glyphs. */
const uint8_t *font_glyph(int ch);   /* 8 rows  */
const uint8_t *font_glyph16(int ch); /* 16 rows */
#endif
