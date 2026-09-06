/* params.h — every dimension and colour of the machine that is not solved
 * from the display: the plastic, the light, the bezel's proportions.
 * Values were tuned by eye; a change here is a change to how the machine
 * looks, and the golden frames will say so. */
#ifndef DXM_CHASSIS_PARAMS_H
#define DXM_CHASSIS_PARAMS_H

/* embossed grain depth */
#define RELIEF 2.0f
/* the key light's direction, for every bevel and every scratch */
#define LIGHT_X (-0.42f)
#define LIGHT_Y (-0.91f)

/* The warm taupe the floppy faceplate used to be - promoted to the whole
 * case, with the drive now going the other way (darker and greyer). */
#define PLASTIC_R 0xBF
#define PLASTIC_G 0xAF
#define PLASTIC_B 0x8A

/* The bezel, as fractions of the tube's height. */
#define BEZEL_BAND 0.076f    /* dished part, next to the glass */
#define BEZEL_HOUSING 0.095f /* full surround depth beyond the picture */
/* shoulder corner radius: the moulding face's OWN radius, not R_IN + BAND,
 * which forced a huge curve */
#define BEZEL_R_MID 0.042f
/* aperture corner ON TOP of the barrel: the curvature already rounds the
 * corners, so this only adds the moulding's own radius - a large value here
 * compounds and gives a blobby, over-rounded opening */
#define BEZEL_R_IN 0.018f
/* where the shoulder roll begins, as a fraction across the dish */
#define FILLET_START 0.915f
/* the shoulder follows the tube's curvature only PARTLY - the moulding
 * flattens as it moves out from the glass, but it does not go straight */
#define BEZEL_R_MID_WARP 0.45f
/* mm: the front face's top corners, where it curves off into the set-back
 * sides */
#define FACE_R_TOP 0.0f

#endif
