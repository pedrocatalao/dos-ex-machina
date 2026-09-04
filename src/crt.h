/* crt.h — the barrel geometry, shared so the moulding and the image agree.
 *
 * The bezel aperture MUST be generated with the same distortion the shader
 * applies to the picture: a rounded-rectangle opening around a barrel-warped
 * image reads as a sticker over a screen, not as glass in a moulding.
 * gpu.c's FS_COMPOSITE barrel() and barrel_cpu() below are the same function;
 * change one, change both. */
#ifndef DXM_CRT_H
#define DXM_CRT_H
#include <math.h>

#define DXM_WARP      0.125f   /* default curvature (a knob, SPEC 6.8)  */
#define DXM_WARP_K    0.30f    /* r^2 term                              */
#define DXM_WARP_NORM 0.32f    /* edge-midpoint normalisation           */

/* p in 0..1 across the tube rect -> warped sample coords in 0..1 */
static inline void barrel_cpu(float px,float py,float warp,float *ox,float *oy){
    float cx=px*2.0f-1.0f, cy=py*2.0f-1.0f;
    float r2=cx*cx+cy*cy;
    float k=1.0f+warp*r2*DXM_WARP_K;
    float d=1.0f+warp*DXM_WARP_NORM;
    *ox=(cx*k/d)*0.5f+0.5f;
    *oy=(cy*k/d)*0.5f+0.5f;
}
#endif
