#ifndef DXM_CHASSIS_H
#define DXM_CHASSIS_H
#include <stdint.h>
typedef enum { LAY_COMPACT, LAY_STANDARD, LAY_STEREO } dxm_variant;
typedef struct {
    dxm_variant variant;
    float tube_x, tube_y, tube_w, tube_h;   /* output pixels */
    float cx, cy, cw, ch;                   /* chassis rect  */
} dxm_layout;
/* Solve tube-first (SPEC §6.3); the tube is 4:3 in every variant. */
dxm_layout chassis_layout(int out_w, int out_h);
/* Draw the static machine once into an RGBA8 buffer (caller frees). */
uint8_t *chassis_render(const dxm_layout *L, int out_w, int out_h);
#endif
