#ifndef DXM_CHASSIS_H
#define DXM_CHASSIS_H
#include <stdint.h>
typedef enum { LAY_COMPACT, LAY_STANDARD, LAY_STEREO } dxm_variant;
typedef struct {
    dxm_variant variant;
    float tube_x, tube_y, tube_w, tube_h;   /* output pixels */
    float cx, cy, cw, ch;                   /* chassis rect  */
    float aperture_r;                       /* corner radius of the hole the
                                             * chassis cut, in output px -
                                             * the shader must match it     */
    /* LED placeholders: drawn UNLIT in the baked chassis; all emission is
     * added live by the shader (SPEC §6.1 - live elements on top). */
    float fdd_led[4];                       /* x,y,w,h, output px  */
    float pwr_led[4];
    float pwr_shelf;     /* y of the power cap's lower edge: the LED's light
                            reaches the plastic below it, not the cap's face */
    /* The two rotary knobs, brightness then contrast: centre x, y and
     * radius in output px.  They are the one part of the case that moves. */
    float knob[2][3];
} dxm_layout;
/* Solve tube-first (SPEC §6.3); the tube is 4:3 in every variant. */
dxm_layout chassis_layout(int out_w, int out_h);
/* Draw the static machine once into an RGBA8 buffer (caller frees). */
uint8_t *chassis_render(dxm_layout *L, int out_w, int out_h);

/* Turn a knob.  pos is 0..1 across its travel.  Returns a small RGBA8 patch
 * - the knob redrawn over the plastic chassis_render saved beneath it - and
 * where it goes, for the caller to upload into the chassis texture.  The
 * buffer belongs to chassis.c and is valid until the next call. */
const uint8_t *chassis_knob_set(int which, float pos, int *x, int *y, int *w, int *h);
#endif
