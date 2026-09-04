/* gpu.h — the ONLY file that names a graphics API (SPEC §6.5).
 * v0 runs OpenGL 3.3 core; the SPEC §12.1 decision is SDL3+SDL_GPU, and this
 * isolation is exactly what makes that a contained port. */
#ifndef DXM_GPU_H
#define DXM_GPU_H
#include <stdint.h>

typedef struct gpu gpu;

gpu *gpu_create(int out_w, int out_h);
void gpu_destroy(gpu *g);
void gpu_resize(gpu *g, int out_w, int out_h);

/* upload the machine's static chassis (RGBA8, drawn on CPU once) */
void gpu_set_chassis(gpu *g, const uint8_t *rgba, int w, int h);

/* upload this frame's tube content (RGB8, already palette-resolved) */
void gpu_set_tube(gpu *g, const uint8_t *rgb, int w, int h);

/* knob values, 0..1 (SPEC §6.8) */
/* The adjustable CRT parameters.  Ordered as they are presented in the
 * on-screen panel; every one is 0..1 except where noted. */
typedef struct gpu_knobs {
    float brightness, contrast;
    float bloom;             /* light bleed between lit pixels             */
    float burn_in;           /* slow ghost of static content               */
    float noise;             /* static / snow                              */
    float jitter;            /* frame-to-frame image instability           */
    float glow_line;         /* the bright band drifting down the tube     */
    float ambient;           /* room light on the chassis                  */
    float flicker;           /* mains-rate brightness variation            */
    float hsync;             /* horizontal sync instability                */
    float rgb_shift;         /* convergence error between the guns         */
    float chassis_glow;      /* screen light spilling onto the case        */
    float persistence;       /* phosphor trail                             */
    float scan;              /* scanline depth                             */
    float vgrid;             /* vertical pixel-column division             */
    float sharp_text;        /* 1 for the DOS screen, 0 while a game runs  */
    float warp;              /* barrel curvature                           */
    float margin;            /* unlit ring inside the aperture             */
    float overscan;          /* picture overflow past it, in output px     */
    float aperture_r;        /* aperture corner radius in output px        */
    int   crt_lines;         /* physical scanlines for this mode           */
    int   crt_cols;          /* source pixel columns                       */
} gpu_knobs;

/* tube rect in output pixels, and the whole composite */
void gpu_draw(gpu *g, float tube_x, float tube_y, float tube_w, float tube_h,
              const gpu_knobs *k, double t);

/* Startup splash: the wordmark on black while the machine is being built.
 * The RGBA must be PREMULTIPLIED (see gpu.c). */
void gpu_set_splash(gpu *g,const uint8_t *rgba,int w,int h);
void gpu_draw_splash(gpu *g,float alpha);
/* black veil over the finished frame: 1 = black, 0 = nothing */
void gpu_draw_fade(gpu *g,float a);

/* The settings panel, drawn on the CPU and composited last. */
void gpu_set_overlay(gpu *g,const uint8_t *rgba,int w,int h);
void gpu_draw_overlay(gpu *g);

/* Live LED emission painted over the baked chassis, which contains only the
 * UNLIT lens.  idx 0 = floppy activity, 1 = power.  round!=0 uses a circular
 * lens profile.  All the light and its bleed onto the plastic come from here. */
/* clip: a height in 0..1 output space above which the LED throws no light
 * on the plastic - the underside of a button it sits beneath.  2.0 = none. */
void gpu_set_led(gpu *g,int idx,float x,float y,float w,float h,
                 float on,float r,float gr,float b,int round,float clip);

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
