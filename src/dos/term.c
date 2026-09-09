/* term.c — the text screen: 80x25 characters with a VGA attribute each,
 * a cursor, and the rendering of that grid into pixels with the VGA
 * font.  Everything the machine shows at the prompt, and everything the
 * navigator draws, goes through here. */
#include "internal.h"
#include "font.h"

static char scr[DOS_ROWS][DOS_COLS];
/* One VGA attribute per cell: low nibble foreground, high nibble background.
 * The boot screen and the prompt never leave 0x07, but a text-mode UI is
 * mostly colour - drawing Norton Commander in one grey would be pointless. */
static uint8_t att[DOS_ROWS][DOS_COLS];
static uint8_t cur_att = TERM_ATTR;
static int cur_r, cur_c;
static uint8_t fb[DOS_W * DOS_H * 3];

void term_clear(void) {
    memset(scr, ' ', sizeof scr);
    memset(att, TERM_ATTR, sizeof att);
    cur_att = TERM_ATTR;
    cur_r = cur_c = 0;
}
void term_fill(int ch, uint8_t a) {
    memset(scr, ch, sizeof scr);
    memset(att, a, sizeof att);
}
static void term_scroll(void) {
    memmove(scr[0], scr[1], (DOS_ROWS - 1) * DOS_COLS);
    memmove(att[0], att[1], (DOS_ROWS - 1) * DOS_COLS);
    memset(scr[DOS_ROWS - 1], ' ', DOS_COLS);
    memset(att[DOS_ROWS - 1], cur_att, DOS_COLS);
    cur_r = DOS_ROWS - 1;
}
void term_put(char ch) {
    if (ch == '\n') {
        cur_c = 0;
        if (++cur_r >= DOS_ROWS)
            term_scroll();
        return;
    }
    if (cur_c >= DOS_COLS) {
        cur_c = 0;
        if (++cur_r >= DOS_ROWS)
            term_scroll();
    }
    att[cur_r][cur_c] = cur_att;
    scr[cur_r][cur_c++] = ch;
}
void term_say(const char *s) {
    while (*s)
        term_put(*s++);
}
void term_sayln(const char *s) {
    term_say(s);
    term_put('\n');
}
void term_newline(void) {
    cur_c = 0;
    if (++cur_r >= DOS_ROWS)
        term_scroll();
}

void term_cell(int r, int c, int ch, uint8_t a) {
    if (r < 0 || r >= DOS_ROWS || c < 0 || c >= DOS_COLS)
        return;
    scr[r][c] = (char)ch;
    att[r][c] = a;
}
void term_fill_row(int r, int c, int n, int ch, uint8_t a) {
    for (int k = 0; k < n; k++)
        term_cell(r, c + k, ch, a);
}
void term_puts(int r, int c, const char *s, uint8_t a) {
    for (int k = 0; s[k]; k++)
        term_cell(r, c + k, (unsigned char)s[k], a);
}
void term_poke(int r, int c, char ch) {
    if (r < 0 || r >= DOS_ROWS || c < 0 || c >= DOS_COLS)
        return;
    scr[r][c] = ch;
}
void term_set_attr(int r, int c, uint8_t a) {
    if (r < 0 || r >= DOS_ROWS || c < 0 || c >= DOS_COLS)
        return;
    att[r][c] = a;
}
uint8_t term_attr_at(int r, int c) {
    if (r < 0 || r >= DOS_ROWS || c < 0 || c >= DOS_COLS)
        return 0;
    return att[r][c];
}
int term_row(void) {
    return cur_r;
}
int term_col(void) {
    return cur_c;
}
void term_set_cursor(int r, int c) {
    cur_r = r;
    cur_c = c;
}
uint8_t *term_fb(void) {
    return fb;
}

/* the 16 VGA text colours, as the DAC actually produced them */
static const uint8_t VGA16[16][3] = {
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0xAA}, {0x00, 0xAA, 0x00}, {0x00, 0xAA, 0xAA},
    {0xAA, 0x00, 0x00}, {0xAA, 0x00, 0xAA}, {0xAA, 0x55, 0x00}, {0xAA, 0xAA, 0xAA},
    {0x55, 0x55, 0x55}, {0x55, 0x55, 0xFF}, {0x55, 0xFF, 0x55}, {0x55, 0xFF, 0xFF},
    {0xFF, 0x55, 0x55}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0x55}, {0xFF, 0xFF, 0xFF}};

void term_render(void) {
    memset(fb, 0, sizeof fb);
    for (int r = 0; r < DOS_ROWS; r++)
        for (int c = 0; c < DOS_COLS; c++) {
            const uint8_t *g = font_glyph16((unsigned char)scr[r][c]);
            const uint8_t *fg = VGA16[att[r][c] & 0x0F];
            const uint8_t *bg = VGA16[(att[r][c] >> 4) & 0x07];
            for (int j = 0; j < 16; j++) {
                uint8_t bits = g[j];
                for (int i = 0; i < 8; i++) {
                    const uint8_t *col = (bits & (0x80 >> i)) ? fg : bg;
                    int y = DOS_PAD_Y + r * 16 + j, x = DOS_PAD_X + c * 8 + i;
                    uint8_t *p = fb + ((size_t)y * DOS_W + x) * 3;
                    p[0] = col[0];
                    p[1] = col[1];
                    p[2] = col[2];
                }
            }
        }
}
/* the cursor the VGA BIOS set, at the text position, when the caller says
 * it is on: the underline, on the last two rows of the cell */
void term_draw_cursor(void) {
    for (int j = 13; j < 15; j++)
        for (int i = 0; i < 8; i++) {
            int y = DOS_PAD_Y + cur_r * 16 + j, x = DOS_PAD_X + cur_c * 8 + i;
            if (y < DOS_H && x < DOS_W) {
                uint8_t *p = fb + ((size_t)y * DOS_W + x) * 3;
                p[0] = 0xAA;
                p[1] = 0xAA;
                p[2] = 0xAA;
            }
        }
}
