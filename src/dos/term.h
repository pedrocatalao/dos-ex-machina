/* term.h — the text screen (see term.c).  Rows and columns are cells;
 * pixels only exist in the frame buffer term_render() fills. */
#ifndef DXM_DOS_TERM_H
#define DXM_DOS_TERM_H
#include "dos.h"
#include <stdint.h>
#define TERM_ATTR 0x07             /* light grey on black */
void term_clear(void);             /* blank, default attribute, cursor home */
void term_fill(int ch, uint8_t a); /* every cell one character and attribute */
void term_put(char ch);            /* at the cursor; \n ends the line, scrolls */
void term_say(const char *s);
void term_sayln(const char *s);
void term_newline(void);
void term_cell(int r, int c, int ch, uint8_t a);
void term_fill_row(int r, int c, int n, int ch, uint8_t a);
void term_puts(int r, int c, const char *s, uint8_t a);
void term_poke(int r, int c, char ch); /* the character only; the attribute stays */
void term_set_attr(int r, int c, uint8_t a);
uint8_t term_attr_at(int r, int c);
int term_row(void);
int term_col(void);
void term_set_cursor(int r, int c);
uint8_t *term_fb(void); /* DOS_W x DOS_H RGB8, valid after term_render */
void term_render(void);
void term_draw_cursor(void);
#endif
