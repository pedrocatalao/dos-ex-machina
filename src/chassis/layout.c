/* layout.c — solve the machine's layout from the display size:
 * tube-first, one arrangement for every aspect (SPEC §6.3). */
#include "internal.h"

dxm_layout chassis_layout(int W,int H){
    dxm_layout L; float aspect=(float)W/(float)H;
    L.variant = aspect<1.45f?LAY_COMPACT : (aspect<1.85f?LAY_STANDARD:LAY_STEREO);
    L.cx=0; L.cy=0; L.cw=(float)W; L.ch=(float)H;
    float edge=W*0.024f, inset=H*0.052f;
    /* the monitor housing is chunky: it wraps the picture by BEZEL_HOUSING
     * on every side, and the layout has to budget for it */
    float th=((float)H-inset)*0.765f, tw=th*4.0f/3.0f;   /* slimmer band */
    float hous=th*BEZEL_HOUSING;
    /* The CRT sits in the CENTRE, a speaker column on each side; the drive
     * and controls all live in the bottom band.  One arrangement for every
     * aspect - narrow displays just get slimmer speakers. */
    float min_spk=inset*1.1f;
    float avail=(W-2*edge-tw-2*hous)*0.5f;      /* per side */
    if(avail<min_spk){
        float k=(W-2*edge-2*min_spk)/(tw+2.0f*hous);
        if(k<0.35f) k=0.35f;
        tw*=k; th*=k; hous=th*BEZEL_HOUSING;
    }
    L.tube_w=tw; L.tube_h=th;
    L.tube_x=((float)W-tw)*0.5f;
    L.tube_y=hous+inset*0.22f;
    return L;
}
