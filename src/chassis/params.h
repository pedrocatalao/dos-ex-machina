/* params.h — every dimension and colour of the machine that is not solved
 * from the display: the plastic, the light, the bezel's proportions.
 * Values were tuned by eye; a change here is a change to how the machine
 * looks, and the golden frames will say so. */
#ifndef DXM_CHASSIS_PARAMS_H
#define DXM_CHASSIS_PARAMS_H
#define RELIEF  2.0f          /* embossed grain depth */
#define LIGHT_X (-0.42f)
#define LIGHT_Y (-0.91f)

/* The warm taupe the floppy faceplate used to be - promoted to the whole
 * case, with the drive now going the other way (darker and greyer). */
#define PLASTIC_R 0xBF
#define PLASTIC_G 0xAF
#define PLASTIC_B 0x8A
#define BEZEL_INSET 0.055f      /* of tube height */
#define TUBE_H_FRAC 0.66f

#define BEZEL_BAND    0.076f   /* dished part, next to the glass          */
#define BEZEL_HOUSING 0.095f   /* full surround depth beyond the picture   */
#define BEZEL_R_MID   0.042f   /* shoulder corner radius                    */
#define FACE_R_TOP    0.0f    /* mm: the front face's top corners, where it
                                  curves off into the set-back sides        */
#define FILLET_START     0.915f /* where the shoulder roll begins,
                                 * as a fraction across the dish  */
#define BEZEL_R_MID_WARP 0.45f /* the shoulder follows the tube's curvature  */
                               /* only PARTLY - the moulding flattens as it  */
                               /* moves out from the glass, but it does not  */
                               /* go straight.                               */
                               /* moulding face - its OWN radius, not      */
                               /* R_IN + BAND, which forced a huge curve   */
#define BEZEL_R_OUT   0.006f   /* housing outer corner - tighter than the  */
                               /* aperture, so the moulding reads crisp    */
#define BEZEL_R_IN    0.018f   /* aperture corner ON TOP of the barrel:    */
                               /* the curvature already rounds the corners, */
                               /* so this only adds the moulding's own      */
                               /* radius - a large value here compounds and */
                               /* gives a blobby, over-rounded opening      */
#endif
