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
typedef struct gpu_knobs {
    float brightness, contrast, ambient, glow, persistence, scan;
    float warp;              /* barrel curvature                            */
    float margin;            /* unlit ring inside the aperture (0..0.1)     */
    int   crt_lines;         /* physical scanlines for this mode (§2.2)     */
} gpu_knobs;

/* tube rect in output pixels, and the whole composite */
void gpu_draw(gpu *g, float tube_x, float tube_y, float tube_w, float tube_h,
              const gpu_knobs *k, double t);

/* read the framebuffer back (for --shot); caller frees */
uint8_t *gpu_readback(gpu *g, int *w, int *h);
#endif
