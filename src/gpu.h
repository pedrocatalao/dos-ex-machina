/* gpu.h — the ONLY file that names a graphics API (SPEC §6.5): OpenGL 3.3
 * core (SPEC §12.1).  Keeping it to this one module is what would make a
 * move to another API a contained port. */
#ifndef DXM_GPU_H
#define DXM_GPU_H
#include <stdint.h>

typedef struct gpu gpu;

gpu *gpu_create(int out_w, int out_h);
void gpu_destroy(gpu *g);
void gpu_resize(gpu *g, int out_w, int out_h);

/* upload the machine's static chassis (RGBA8, drawn on CPU once) */
void gpu_set_chassis(gpu *g, const uint8_t *rgba, int w, int h);
/* replace one rectangle of it - a knob that has been turned */
void gpu_patch_chassis(gpu *g, int x, int y, int w, int h, const uint8_t *rgba);

/* upload this frame's tube content (RGB8, already palette-resolved) */
void gpu_set_tube(gpu *g, const uint8_t *rgb, int w, int h);

/* knob values, 0..1 (SPEC §6.8) */
/* The adjustable CRT parameters, as the OSD (osd.h) and SETUP present them;
 * every one is 0..1 except where noted. */
typedef struct gpu_knobs {
    float brightness, contrast;
    float bloom;        /* light bleed between lit pixels             */
    float burn_in;      /* slow ghost of static content               */
    float noise;        /* static / snow                              */
    float jitter;       /* frame-to-frame image instability           */
    float glow_line;    /* the bright band drifting down the tube     */
    float ambient;      /* room light on the chassis                  */
    float flicker;      /* mains-rate brightness variation            */
    float hsync;        /* horizontal sync instability                */
    float rgb_shift;    /* convergence error between the guns         */
    float chassis_glow; /* screen light spilling onto the case        */
    float persistence;  /* phosphor trail                             */
    float scan;         /* scanline depth                             */
    float vgrid;        /* vertical pixel-column division             */
    float sharp_text;   /* 1 for the DOS screen, 0 while a game runs  */
    float warp;         /* barrel curvature                           */
    float margin;       /* unlit ring inside the aperture             */
    float overscan;     /* picture overflow past it, in output px     */
    float aperture_r;   /* aperture corner radius in output px        */
    int crt_lines;      /* physical scanlines for this mode           */
    int crt_cols;       /* source pixel columns                       */
    float mask;         /* aperture-grille stripes: 0 none, 1 full    */
    float sharpness;    /* text's pixel edges: 0 eased, 1 hard        */
} gpu_knobs;

/* tube rect in output pixels, and the whole composite */
void gpu_draw(gpu *g, float tube_x, float tube_y, float tube_w, float tube_h, const gpu_knobs *k,
              double t);

/* Startup splash: the wordmark on black while the machine is being built.
 * The RGBA must be PREMULTIPLIED (see gpu.c). */
void gpu_set_splash(gpu *g, const uint8_t *rgba, int w, int h);
void gpu_draw_splash(gpu *g, float alpha);
/* black veil over the finished frame: 1 = black, 0 = nothing */
void gpu_draw_fade(gpu *g, float a);

/* The tube's state as a piece of electronics: how much of the raster the
 * deflection is drawing (h,v: 1 = full, 0 = collapsed to a line or a dot)
 * and how hard the beam is driven (gain: 1 = normal).  Power-on warms up
 * from small and dim; power-off collapses to a line, then a dot, then
 * nothing.  Room light and the glass are unaffected - only the picture. */
void gpu_set_tube_power(gpu *g, float h, float v, float gain);

/* The monitor's OSD (osd.h): RGBA over the whole picture, mixed into the
 * signal ahead of everything the tube does with it (signal.frag).  NULL
 * takes it off; `changed` uploads the image, otherwise the last one is
 * kept. */
void gpu_set_osd(gpu *g, const uint8_t *rgba, int w, int h, int changed);

/* Live LED emission painted over the baked chassis, which contains only the
 * UNLIT lens.  idx 0 = floppy activity, 1 = power, 2 and 3 the turbo
 * display's FPS and MHz mode lights, 4 and 5 the mouse lamps, HOST and
 * DXM.  round!=0 uses a circular lens profile.  All the light and its
 * bleed onto the plastic come from here. */
/* clip: a height in 0..1 output space above which the LED throws no light
 * on the plastic - the underside of a button it sits beneath.  2.0 = none. */
void gpu_set_led(gpu *g, int idx, float x, float y, float w, float h, float on, float r, float gr,
                 float b, int round, float clip);
/* The turbo display's digits, lit over the baked window (see segdisp.h).
 * x,y,w,h in 0..1 output space; lvl[k*7+s] is how lit segment s of digit
 * k is, 0..1, left to right; on scales the whole emission. */
void gpu_set_segdisp(gpu *g, float x, float y, float w, float h, const float lvl[21], float on);

/* read the framebuffer back (for --shot); caller frees */
uint8_t *gpu_readback(gpu *g, int *w, int *h);

/* What the context actually is - vendor / renderer / version - and, after
 * gpu_create has failed, which entry points the driver did not provide. */
const char *gpu_describe(void);
const char *gpu_missing(void);

/* Where gpu.c's own messages go - shader compile errors, missing entry
 * points.  Unset, they go to stderr; the host points this at its log so
 * they reach the file too. */
void gpu_set_log(void (*fn)(const char *msg));
#endif
