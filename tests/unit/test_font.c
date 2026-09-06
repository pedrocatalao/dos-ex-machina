/* font: the CP437 8x8 table - every code has a glyph, printable
 * characters have ink, the space does not, and the arrows the navigator
 * scrolls with are there. */
#include "check.h"
#include "font.h"

static int ink(const uint8_t *g) {
    int n = 0;
    for (int i = 0; i < 8; i++)
        n += g[i] != 0;
    return n;
}

int main(void) {
    CHECK(ink(font_glyph(' ')) == 0);
    CHECK(ink(font_glyph(0)) == 0);
    /* every printable ASCII character has some ink */
    for (int ch = '!'; ch <= '~'; ch++)
        CHECK(ink(font_glyph(ch)) > 0);
    /* a few shapes the machine depends on: the box-drawing set the
     * navigator's frames are made of, and the scroll arrows */
    CHECK(ink(font_glyph(0xC4)) > 0);  /* horizontal line */
    CHECK(ink(font_glyph(0xB3)) > 0);  /* vertical line */
    CHECK(ink(font_glyph(0xDA)) > 0);  /* top-left corner */
    CHECK(ink(font_glyph(0x18)) > 0);  /* up arrow */
    CHECK(ink(font_glyph(0x19)) > 0);  /* down arrow */
    CHECK(ink(font_glyph(0xDB)) == 8); /* the full block: every row */
    /* 'A' has its crossbar and its two legs */
    const uint8_t *a = font_glyph('A');
    CHECK(a[1] != 0 && a[6] != 0);
    /* the table wraps at 256, so a signed char never reads past it */
    CHECK(font_glyph(256) == font_glyph(0));
    CHECK(font_glyph(-1) == font_glyph(255));
    return check_done("font");
}
