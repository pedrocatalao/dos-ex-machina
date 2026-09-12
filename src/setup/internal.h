/* internal.h — shared inside src/setup only.  The public face is
 * src/setup.h. */
#ifndef DXM_SETUP_INTERNAL_H
#define DXM_SETUP_INTERNAL_H
#include <stdint.h>

/* The screen SETUP draws on: 640x400 in 256 colours.  The same geometry as
 * the machine's text screen, so the tube treats it exactly as it treats the
 * POST and DOS - and enough room for an interface with chrome rather than
 * rows of text.  The artwork is drawn at half that and doubled on the way
 * in: art of the period had visible pixels, the interface over it did not.
 *
 * The canvas is indexed, not RGB, the way such a program's own was: the
 * first sixteen entries are the interface's, the rest belong to whichever
 * artwork is on screen. */
#define SCR_W 640
#define SCR_H 400

/* The picture carries the same overscan border every other picture on this
 * machine does - the POST's text screen, and DOSBox's frames, which get it
 * in proportion (dosbox.c).  Without it the drawing stops short of the
 * aperture and the glass shows in a ring around it. */
#define SCR_PAD_X 14
#define SCR_PAD_Y 12
#define CANVAS_W (SCR_W + 2 * SCR_PAD_X)
#define CANVAS_H (SCR_H + 2 * SCR_PAD_Y)

/* the interface's sixteen, the VGA text palette by another name */
enum {
    C_BLACK = 0,
    C_BLUE,
    C_GREEN,
    C_CYAN,
    C_RED,
    C_MAGENTA,
    C_BROWN,
    C_GREY,
    C_DGREY,
    C_LBLUE,
    C_LGREEN,
    C_LCYAN,
    C_LRED,
    C_LMAGENTA,
    C_YELLOW,
    C_WHITE
};

/* One thing that can be changed: a value with two ends, or one of a short
 * list.  Some of them the machine can act on at once; the rest are read
 * when it powers on, and say so. */
typedef enum { SET_SLIDER, SET_CHOICE } set_kind;
typedef struct {
    const char *name;
    set_kind kind;
    float *val, lo, hi;      /* SET_SLIDER: where the value lives */
    int *pick;               /* SET_CHOICE: which one is chosen */
    const char *const *opts; /* and what they are called */
    int nopts;
    int next_boot;    /* takes effect when the machine is switched on again */
    int fixed;        /* this machine cannot do otherwise */
    const char *note; /* the line under it, when the eye is on it */
} setting;

/* machine.c — what the machine is set to, as against how the tube looks:
 * kept in its own file next to the preferences, read before DOS boots. */
int machine_settings(int section, setting *out, int max);
void machine_load(const char *path);
void machine_save(const char *path);
void machine_where(const char *c_drive); /* where to look for MIDI ROMs */
const char *machine_keyboard(void);      /* "auto", or a DOS layout */
const char *machine_memory(void);        /* as the core wants it: "16" */
const char *machine_cpu_core(void);
const char *machine_midi(void); /* auto | off | mt32 | sc55 | sf2 */
int machine_boot_catalogue(void);

/* banner.c — the artwork, opened from the file it was made as and fitted
 * to the palette entries the interface leaves it */
#define BANNER_FIRST 16 /* the first palette entry the artwork owns */
#define BANNER_COLOURS (256 - BANNER_FIRST)
typedef struct {
    const char *name;
    int w, h;
    const uint8_t *px;  /* w*h indices, 0..BANNER_COLOURS-1 */
    const uint8_t *pal; /* BANNER_COLOURS RGB triples */
} banner;
const banner *banner_open(int which); /* NULL if the file will not open */
/* Both of a change of picture, fitted to one palette so they can be on the
 * glass at the same time.  0 if either will not open. */
int banner_open_pair(int from, int to, const banner **a, const banner **b);
int banner_count(void);

/* canvas.c — the surface and what can be put on it */
void cv_clear(uint8_t colour);
void cv_banner(int which); /* the artwork at the top, and its palette */
/* A change of picture, part way through: the new one showing in slats that
 * grow down from each of `slats` bands, the old one in what is left. */
void cv_banner_slats(const banner *from, const banner *to, float shut, int slats);
void cv_rect(int x, int y, int w, int h, uint8_t colour);
void cv_frame(int x, int y, int w, int h, uint8_t colour);
/* Darken what is already there, leaving one pixel in `n` of it: the way a
 * program of the day made a panel you could see through, and the only way
 * on a canvas whose colours are indices and cannot be blended.  n=2 is a
 * veil, n=4 is nearly solid.  `r` rounds the corners off. */
void cv_scrim(int x, int y, int w, int h, int n, int r);
void cv_round_frame(int x, int y, int w, int h, uint8_t colour, int r);
/* A value between its ends: the groove, how much of it is filled, and the
 * handle.  `on` is the row the eye is on. */
void cv_slider(int x, int y, int w, float t, int on);
#define CV_SLIDER_H 12
/* Text is proportional and hung off a baseline: `y` is the top of the line,
 * the call returns how far the pen moved, and bg < 0 leaves what is under
 * it alone. */
#define CV_LINE 16 /* ascent and descent of the face, as a row */
int cv_text(int x, int y, const char *s, uint8_t fg, int bg);
int cv_text_bold(int x, int y, const char *s, uint8_t fg, int bg);
int cv_width(const char *s); /* what it will take, without drawing it */
/* The same, broken at spaces to fit `w`, one line every CV_LINE down from
 * `y`.  Returns how many lines it took. */
int cv_text_wrap(int x, int y, int w, const char *s, uint8_t fg);
void cv_pointer(int x, int y);
/* How bright the palette is on its way out, 0 black to 1 full: the fade a
 * program of the day did by ramping the DAC rather than by touching a
 * single pixel, which is also the only fade an indexed screen has. */
void cv_fade(float f);
const uint8_t *cv_rgb(void); /* the canvas as SCR_W x SCR_H RGB8 */
int cv_banner_count(void);
#endif
