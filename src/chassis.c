/* chassis.c — the machine, drawn from parameters (SPEC §6.1).  No raster art.
 * Rendered once into an RGBA buffer at startup / resolution change; per frame
 * it is one texture. */
#include "chassis.h"
#include "font.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "crt.h"

/* ---- chassis_params: every dimension and colour in one place (SPEC §5) --- */
#define RELIEF  4.0f          /* embossed grain depth */
#define LIGHT_X (-0.42f)
#define LIGHT_Y (-0.91f)

#define PLASTIC_R 0xC9
#define PLASTIC_G 0xC2
#define PLASTIC_B 0xAC
#define BEZEL_INSET 0.055f      /* of tube height */
#define TUBE_H_FRAC 0.66f

#define BEZEL_BAND    0.076f   /* dished part, next to the glass          */
#define BEZEL_HOUSING 0.095f   /* full surround depth beyond the picture   */
#define BEZEL_R_MID   0.042f   /* where the dished band meets the flat     */
                               /* moulding face - its OWN radius, not      */
                               /* R_IN + BAND, which forced a huge curve   */
#define BEZEL_R_OUT   0.006f   /* housing outer corner - tighter than the  */
                               /* aperture, so the moulding reads crisp    */
#define BEZEL_R_IN    0.018f   /* aperture corner ON TOP of the barrel:    */
                               /* the curvature already rounds the corners, */
                               /* so this only adds the moulding's own      */
                               /* radius - a large value here compounds and */
                               /* gives a blobby, over-rounded opening      */

typedef struct { uint8_t *px; int w,h; } canvas;

/* All moulded lettering shares one scale so the machine reads as one
 * product at every resolution (SPEC 6.3: shared metrics stay shared). */
static float g_lbl = 1.0f;

static void px_set(canvas *c,int x,int y,int r,int g,int b){
    if(x<0||y<0||x>=c->w||y>=c->h) return;
    uint8_t *p=c->px+((size_t)y*c->w+x)*4;
    p[0]=(uint8_t)(r<0?0:r>255?255:r); p[1]=(uint8_t)(g<0?0:g>255?255:g);
    p[2]=(uint8_t)(b<0?0:b>255?255:b); p[3]=255;
}
static void px_blend(canvas *c,int x,int y,int r,int g,int b,float a){
    if(x<0||y<0||x>=c->w||y>=c->h||a<=0) return;
    if(a>1) a=1;
    /* CLAMP before the uint8_t cast.  Shading factors >1 (the lit dish wall)
     * pushed channels past 255, and the unclamped cast WRAPPED them - red
     * wrapped first (the plastic's largest channel), leaving teal/blue
     * speckles across the brightest parts of the bezel. */
    if(r>255)r=255; if(r<0)r=0;
    if(g>255)g=255; if(g<0)g=0;
    if(b>255)b=255; if(b<0)b=0;
    uint8_t *p=c->px+((size_t)y*c->w+x)*4;
    p[0]=(uint8_t)(p[0]*(1-a)+r*a); p[1]=(uint8_t)(p[1]*(1-a)+g*a);
    p[2]=(uint8_t)(p[2]*(1-a)+b*a); p[3]=255;
}
/* deterministic integer hash — identical on all three platforms (SPEC 6.7) */
static float hash2(int x,int y,int s){
    unsigned h=(unsigned)(x*374761393)+(unsigned)(y*668265263)+(unsigned)(s*1442695041);
    h=(h^(h>>13))*1274126177u; h^=h>>16;
    return (float)(h&1023)/1023.0f;
}
/* Smoothly interpolated value noise - the blocky nearest-neighbour hash it
 * replaces is what made the plastic look mushy. */
static float vnoise(float x,float y,int seed){
    int xi=(int)floorf(x), yi=(int)floorf(y);
    float fx=x-xi, fy=y-yi;
    fx=fx*fx*(3.0f-2.0f*fx); fy=fy*fy*(3.0f-2.0f*fy);
    float a=hash2(xi,yi,seed),   b=hash2(xi+1,yi,seed);
    float c=hash2(xi,yi+1,seed), d=hash2(xi+1,yi+1,seed);
    return a+(b-a)*fx+(c-a)*fy+(a-b-c+d)*fx*fy;
}
/* The moulded pebble grain as a HEIGHT FIELD.  Real case plastic is
 * embossed, so the only way it reads as rugged rather than as noise is to
 * light it: sample the height, take its gradient, and shade by how each
 * micro-facet faces the light.  Brightness noise alone always looks flat. */
static float plastic_height(float x,float y){
    float pebble = vnoise(x*0.42f, y*0.42f, 2);        /* main grain  */
    float coarse = vnoise(x*0.14f, y*0.14f, 3);        /* clustering  */
    float fine   = vnoise(x*1.05f, y*1.05f, 5);        /* speckle     */
    return pebble*0.62f + coarse*0.24f + fine*0.14f;
}
/* Returns a shading offset: lit facets brighten, facets turned away darken,
 * and the pits between grains pick up a little occlusion. */
/* Printed labels are smooth: they are ink on a flat substrate, not moulded
 * plastic, so the embossed grain must not run under them. */
static int g_grain = 1;
static float plastic_tex(int x,int y){
    if(!g_grain) return 0.0f;
    float h  = plastic_height((float)x,      (float)y);
    float hx = plastic_height((float)x+1.0f, (float)y);
    float hy = plastic_height((float)x,      (float)y+1.0f);
    /* surface normal from the slope; LIGHT_* is the incoming direction */
    float dx = (hx-h)*RELIEF, dy = (hy-h)*RELIEF;
    float lam = -(dx*LIGHT_X + dy*LIGHT_Y);
    float occl = (h-0.5f)*0.055f;            /* pits sit slightly darker */
    float grit = (hash2(x,y,1)-0.5f)*0.016f; /* matte micro-speckle      */
    return lam*0.115f + occl + grit;
}
/* One light, from above and slightly left, as in the reference photo.  y runs
 * DOWN in canvas space, so "up" is negative y. */

/* signed distance to a rounded rect; negative inside */
static float rr_sd(float px,float py,float cx,float cy,float hw,float hh,float r){
    float qx=fabsf(px-cx)-(hw-r), qy=fabsf(py-cy)-(hh-r);
    float ax=fmaxf(qx,0.0f), ay=fmaxf(qy,0.0f);
    return sqrtf(ax*ax+ay*ay)+fminf(fmaxf(qx,qy),0.0f)-r;
}
/* read-modify-write: scale a pixel and add a specular term */
static void px_shade(canvas *c,int x,int y,float mul,float spec){
    if(x<0||y<0||x>=c->w||y>=c->h) return;
    uint8_t *p=c->px+((size_t)y*c->w+x)*4;
    for(int k=0;k<3;k++){
        float v=p[k]*mul+spec*255.0f;
        p[k]=(uint8_t)(v<0?0:v>255?255:v);
    }
}

/* Rounded box evaluated in the warped space, with its own corner radius and
 * an outward offset.  Two of these define the band: the aperture at offset 0,
 * and the band's outer boundary at offset = band width.  Giving that second
 * curve its own radius is the point - deriving it as a uniform offset of the
 * aperture forces its corner to be R_IN + BAND, which is far too round. */
static float warped_rr_sd(const dxm_layout *L,float px,float py,
                          float r,float grow){
    float tx=(px-L->tube_x)/L->tube_w, ty=(py-L->tube_y)/L->tube_h;
    float bx,by; barrel_cpu(tx,ty,DXM_WARP,&bx,&by);
    float ax=fabsf(bx*2.0f-1.0f)*L->tube_w*0.5f;   /* px from centre */
    float ay=fabsf(by*2.0f-1.0f)*L->tube_h*0.5f;
    float hw=L->tube_w*0.5f+grow, hh=L->tube_h*0.5f+grow;
    float qx=ax-(hw-r), qy=ay-(hh-r);
    if(qx>0.0f && qy>0.0f) return sqrtf(qx*qx+qy*qy)-r;
    return fmaxf(ax-hw, ay-hh);
}

static void rect(canvas *c,float x,float y,float w,float h,int r,int g,int b){
    for(int j=(int)y;j<(int)(y+h);j++) for(int i=(int)x;i<(int)(x+w);i++) px_set(c,i,j,r,g,b);
}
static void rrect(canvas *c,float x,float y,float w,float h,float rad,
                  int r,int g,int b,float shade_top,float shade_bot){
    for(int j=(int)y;j<(int)(y+h);j++){
        float ty=h>0?(j-y)/h:0;
        float sh=shade_top+(shade_bot-shade_top)*ty;
        for(int i=(int)x;i<(int)(x+w);i++){
            float dx=0,dy=0;
            if(i<x+rad) dx=x+rad-i; else if(i>x+w-rad) dx=i-(x+w-rad);
            if(j<y+rad) dy=y+rad-j; else if(j>y+h-rad) dy=j-(y+h-rad);
            float d=sqrtf(dx*dx+dy*dy);
            if(rad>0.0f && d>rad) continue;
            float a=(rad>0.0f && d>rad-1.2f)?(rad-d)/1.2f:1.0f;
            float n=plastic_tex(i,j);
            px_blend(c,i,j,(int)(r*(sh+n)),(int)(g*(sh+n)),(int)(b*(sh+n)),a);
        }
    }
}
static void bevel(canvas *c,float x,float y,float w,float h,float t,int up){
    for(int k=0;k<(int)t;k++){
        float a=0.5f*(1.0f-(float)k/t);
        int hi=up?255:0, lo=up?0:255;
        for(int i=(int)x+k;i<(int)(x+w)-k;i++){
            px_blend(c,i,(int)y+k,hi,hi,hi,a*0.55f);
            px_blend(c,i,(int)(y+h)-1-k,lo,lo,lo,a*0.45f);
        }
        for(int j=(int)y+k;j<(int)(y+h)-k;j++){
            px_blend(c,(int)x+k,j,hi,hi,hi,a*0.40f);
            px_blend(c,(int)(x+w)-1-k,j,lo,lo,lo,a*0.35f);
        }
    }
}
static void text(canvas *c,float x,float y,const char *s,float sc,int r,int g,int b){
    if(sc<1.0f) sc=1.0f;
    for(int n=0;s[n];n++){
        const uint8_t *gl=font_glyph((unsigned char)s[n]);
        for(int j=0;j<8;j++) for(int i=0;i<8;i++)
            if(gl[j]&(0x80>>i))
                for(int sy=0;sy<(int)sc;sy++) for(int sx=0;sx<(int)sc;sx++){
                    px_blend(c,(int)(x+(n*8+i)*sc+sx),(int)(y+j*sc+sy)+1,255,255,255,0.13f);
                    px_set(c,(int)(x+(n*8+i)*sc+sx),(int)(y+j*sc+sy),r,g,b);
                }
    }
}
static void led(canvas *c,float cx,float cy,float rad,int r,int g,int b){
    /* Matched to the reference PNG's LEDs at 16x magnification:
     *  - a THIN dark outline hugging the lens (heavier at the top), not a
     *    wide mounting ring
     *  - a saturated, fairly uniform body, deepening toward lower-right
     *  - a soft whitish hot spot in the upper-left quarter
     *  - NO glow halo on the surrounding plastic                        */
    for(int j2=(int)(cy-rad*1.25f);j2<=(int)(cy+rad*1.25f);j2++)
        for(int i2=(int)(cx-rad*1.25f);i2<=(int)(cx+rad*1.25f);i2++){
            float dx=(i2-cx)/rad, dy=(j2-cy)/rad;
            float d=sqrtf(dx*dx+dy*dy);
            if(d>1.50f) continue;
            if(d>0.94f){
                /* The lens sits in a HOLE, and the hole is what reads 3D:
                 * dark ring, a soft shadow on the plastic above it, and a
                 * lit chamfer lip on the plastic below it. */
                if(d<=1.18f){                      /* the ring itself */
                    float a=1.0f-fmaxf(0.0f,(d-1.06f)/0.12f);
                    a*=fminf(1.0f,(d-0.90f)/0.06f);
                    float top=(dy<0.0f)?1.0f:0.66f;
                    px_blend(c,i2,j2,26,22,14,a*0.85f*top);
                } else {
                    float t=(d-1.18f)/0.32f;       /* 0 at ring, 1 outside */
                    if(dy<-0.15f)                  /* shadow above the hole */
                        px_shade(c,i2,j2,1.0f-0.16f*(1.0f-t)*(-dy/d),0.0f);
                    else if(dy>0.15f)              /* lit lip below it */
                        px_shade(c,i2,j2,1.0f+0.20f*(1.0f-t)*(dy/d),
                                 (1.0f-t)*(dy/d)*0.06f);
                }
                continue;
            }
            /* body: uniform, deepening to lower-right */
            float f=0.94f-0.22f*fmaxf(0.0f,(dx+dy)*0.5f);
            if(d>0.72f) f*=1.0f-(d-0.72f)/0.28f*0.30f;
            /* hot spot up-left: soft gaussian, whitening not just brightening */
            float hx=(dx+0.30f)/0.42f, hy=(dy+0.32f)/0.42f;
            float w2=expf(-(hx*hx+hy*hy));
            int rr=(int)(r*f+(255-r*f)*w2);
            int gg=(int)(g*f+(255-g*f)*w2);
            int bb=(int)(b*f+(255-b*f)*w2*0.9f);
            px_blend(c,i2,j2,rr,gg,bb,1.0f);
        }
}

/* Rectangular LED window, same construction as the round one: thin outline
 * heavier on top, body deepening lower-right, soft hot spot up-left, the
 * recess shadow above and lit lip below.  This is what the reference's
 * floppy activity light is. */
static void led_rect(canvas *c,float cx,float cy,float w,float h,
                     int r,int g,int b){
    float hw=w*0.5f, hh=h*0.5f, rad=h*0.30f;
    float ring=fmaxf(1.2f,h*0.16f);
    for(int j2=(int)(cy-hh-ring*3);j2<=(int)(cy+hh+ring*3);j2++)
        for(int i2=(int)(cx-hw-ring*3);i2<=(int)(cx+hw+ring*3);i2++){
            float sd=rr_sd((float)i2,(float)j2,cx,cy,hw,hh,rad);
            float dy=(j2-cy)/hh;
            if(sd<=0.0f){
                /* lens body */
                float dx=(i2-cx)/hw;
                float f=0.94f-0.20f*fmaxf(0.0f,(dx+dy)*0.5f);
                float e=-sd/h;                     /* depth into the lens */
                if(e<0.20f) f*=0.70f+1.5f*e;       /* darker at the frame */
                float hx=(dx+0.35f)/0.55f, hy=(dy+0.40f)/0.60f;
                float w2=expf(-(hx*hx+hy*hy));
                px_blend(c,i2,j2,(int)(r*f+(255-r*f)*w2),
                         (int)(g*f+(255-g*f)*w2),
                         (int)(b*f+(255-b*f)*w2*0.9f),1.0f);
            } else if(sd<=ring){
                float a=1.0f-sd/ring;
                float top=(dy<0.0f)?1.0f:0.66f;
                px_blend(c,i2,j2,26,22,14,a*0.85f*top);
            } else if(sd<=ring*3.0f){
                float t=(sd-ring)/(ring*2.0f);
                if(dy<-0.15f)
                    px_shade(c,i2,j2,1.0f-0.15f*(1.0f-t),0.0f);
                else if(dy>0.15f)
                    px_shade(c,i2,j2,1.0f+0.18f*(1.0f-t),(1.0f-t)*0.05f);
            }
        }
}

/* ---- moulded modules ---- */

/* A grille is a recessed WELL with a moulded lip, and slots inside it with
 * real cross-section: dark trough, lit lower lip, shadowed upper lip. */
static void housing_edge(canvas *c,float x,float y,float w,float h,float r,
                         float ew,float shadow,int raised,float gain);
static void grille_panel(canvas *c,float x,float y,float w,float h,float pitch){
    /* Perforated speaker area: a hex-packed grid of small punched holes
     * confined to a capsule-shaped zone.  The case face stays flush and
     * plain; each hole gets the full treatment - dark interior slightly
     * lighter at its bottom, an overhang shadow inside the top edge, a
     * faint lit lip on the plastic below, soft anti-aliased rim. */
    float ccx=x+w*0.5f, ccy=y+h*0.5f;
    float hw=w*0.40f, hh=h*0.44f;
    float crad=fminf(hw,hh)*0.09f;      /* rectangular, corners just eased */
    float hp=fmaxf(2.0f,pitch*0.21f);            /* hole pitch  */
    float hr=hp*0.25f;                           /* hole radius */
    int row=0;
    for(float cy2=y+hp; cy2<y+h-hp*0.5f; cy2+=hp*0.87f,row++){
        float ox=(row&1)? hp*0.5f : 0.0f;
        for(float cx2=x+hp+ox; cx2<x+w-hp*0.5f; cx2+=hp){
            /* only holes fully inside the capsule */
            if(rr_sd(cx2,cy2,ccx,ccy,hw,hh,crad) > -hr*1.2f) continue;
            /* At this pitch a hole spans only a few pixels, so shade it by
             * ANALYTIC COVERAGE rather than discrete zones - otherwise the
             * rim aliases and the grid reads as ragged dots. */
            for(int j2=(int)(cy2-hr-2);j2<=(int)(cy2+hr+2);j2++)
                for(int i2=(int)(cx2-hr-2);i2<=(int)(cx2+hr+2);i2++){
                    float dxp=i2+0.5f-cx2, dyp=j2+0.5f-cy2;
                    float d=sqrtf(dxp*dxp+dyp*dyp);
                    float cov=hr-d+0.5f;              /* pixel coverage */
                    if(cov<=0.0f){
                        /* just outside: a whisper of lit lip below the hole */
                        if(cov>-1.2f && dyp>0.0f)
                            px_shade(c,i2,j2,1.0f+0.10f*(1.2f+cov)/1.2f
                                     *(dyp/fmaxf(d,0.001f)),0.0f);
                        continue;
                    }
                    if(cov>1.0f) cov=1.0f;
                    float u=dyp/fmaxf(hr,0.001f);     /* -1 top .. +1 bottom */
                    float v=15.0f+11.0f*fmaxf(0.0f,u);/* floor lit at bottom */
                    if(u<-0.15f) v*=0.60f;            /* overhang shadow     */
                    px_blend(c,i2,j2,(int)v,(int)v,(int)(v*1.04f),cov);
                }
        }
    }
}
static void seam(canvas *c,float x,float y,float len,int vertical,float w){
    for(int t=0;t<(int)fmaxf(1.0f,w);t++){
        if(vertical) for(int j=(int)y;j<(int)(y+len);j++){
            px_blend(c,(int)x+t,j,52,49,44,0.60f);
            px_blend(c,(int)x+t+(int)fmaxf(1.0f,w),j,255,252,244,0.24f);
        } else for(int i=(int)x;i<(int)(x+len);i++){
            px_blend(c,i,(int)y+t,52,49,44,0.60f);
            px_blend(c,i,(int)y+t+(int)fmaxf(1.0f,w),255,252,244,0.24f);
        }
    }
}
static void slider(canvas *c,float x,float y,float w,float h,float pos){
    rrect(c,x,y+h*0.34f,w,h*0.32f,h*0.16f,96,93,85,0.55f,0.72f);
    bevel(c,x,y+h*0.34f,w,h*0.32f,fmaxf(1.0f,h*0.10f),0);
    float tw=w*0.15f, tx=x+(w-tw)*pos;
    rrect(c,tx,y,tw,h,h*0.14f,PLASTIC_R,PLASTIC_G,PLASTIC_B,1.14f,0.82f);
    bevel(c,tx,y,tw,h,fmaxf(1.0f,h*0.12f),1);
    for(float i=tx+tw*0.24f;i<tx+tw*0.80f;i+=fmaxf(2.0f,tw*0.20f))
        for(int j=(int)(y+h*0.18f);j<(int)(y+h*0.82f);j++){
            px_blend(c,(int)i,j,55,52,47,0.45f);
            px_blend(c,(int)i+1,j,255,252,244,0.22f);
        }
}
/* Chamfer ring around a recessed opening: a small dished slope from the
 * face down into the hole.  Lit from above, its top run falls dark and its
 * bottom run catches light. */
static void chamfer_ring(canvas *c,float x,float y,float w,float h,float r,
                         float cw){
    float cx=x+w*0.5f, cy=y+h*0.5f, hw=w*0.5f, hh=h*0.5f;
    for(int j2=(int)(y-cw-1);j2<(int)(y+h+cw+1);j2++)
      for(int i2=(int)(x-cw-1);i2<(int)(x+w+cw+1);i2++){
        float sd=rr_sd((float)i2,(float)j2,cx,cy,hw,hh,r);
        if(sd<0.0f||sd>cw) continue;
        float gx=rr_sd((float)i2+1,(float)j2,cx,cy,hw,hh,r)
                -rr_sd((float)i2-1,(float)j2,cx,cy,hw,hh,r);
        float gy=rr_sd((float)i2,(float)j2+1,cx,cy,hw,hh,r)
                -rr_sd((float)i2,(float)j2-1,cx,cy,hw,hh,r);
        float gl=sqrtf(gx*gx+gy*gy); if(gl<1e-4f) continue;
        /* slope faces INTO the hole: inward normal */
        float lam=(-gx/gl)*LIGHT_X+(-gy/gl)*LIGHT_Y;
        float prof=1.0f-sd/cw;
        px_shade(c,i2,j2,1.0f+lam*prof*0.45f,
                 powf(fmaxf(lam,0.0f),8.0f)*prof*0.22f);
      }
}

/* Soft horizontal edge: shade/spec eased over a span of rows, so the
 * transition reads as a moulded curve rather than a drawn line.  This is
 * the difference between the LED (which looks real) and hard 1px lips
 * (which look like a cartoon). */
static void soft_hedge(canvas *c,float x0,float x1,float y,float span,
                       float mul_peak,float spec_peak,int downward){
    int n=(int)fmaxf(2.0f,span);
    for(int k=0;k<n;k++){
        float t=(float)k/n;
        float w2=sinf((1.0f-t)*1.5708f); w2*=w2;         /* eased falloff */
        int yy=downward? (int)y+k : (int)y-k;
        for(int i2=(int)x0;i2<(int)x1;i2++)
            px_shade(c,i2,yy,1.0f+(mul_peak-1.0f)*w2,spec_peak*w2);
    }
}
/* soft vertical edge, same idea */
static void soft_vedge(canvas *c,float y0,float y1,float x,float span,
                       float mul_peak,int rightward){
    int n=(int)fmaxf(2.0f,span);
    for(int k=0;k<n;k++){
        float t=(float)k/n;
        float w2=sinf((1.0f-t)*1.5708f); w2*=w2;
        int xx=rightward? (int)x+k : (int)x-k;
        for(int j2=(int)y0;j2<(int)y1;j2++)
            px_shade(c,xx,j2,1.0f+(mul_peak-1.0f)*w2,0.0f);
    }
}

/* 3.5" drive: same geometry as before (recessed plate, finger cuts, slot
 * through them, protruding eject, inserted diskette), but every edge is now
 * an eased ramp at the LED's fidelity - no hard 1px lines anywhere. */
static void floppy_drive(canvas *c,float x,float y,float w,float h){
    float mm=h/25.4f; (void)w;
    float fw=101.6f*mm;
    int pr=(int)(PLASTIC_R*0.95f),pg=(int)(PLASTIC_G*0.90f),pb=(int)(PLASTIC_B*0.80f);
    /* chassis cut-out: chamfered case edge, thin gap, recessed plate */
    rrect(c,x-0.5f*mm,y-0.5f*mm,fw+1.0f*mm,h+1.0f*mm,2.2f*mm,
          (int)(PLASTIC_R*0.38f),(int)(PLASTIC_G*0.38f),(int)(PLASTIC_B*0.38f),
          0.92f,1.04f);
    chamfer_ring(c,x-0.5f*mm,y-0.5f*mm,fw+1.0f*mm,h+1.0f*mm,2.2f*mm,0.5f*mm);
    rrect(c,x,y,fw,h,2.0f*mm,pr,pg,pb,0.97f,1.01f);
    housing_edge(c,x,y,fw,h,2.0f*mm,1.2f*mm,0.0f,0,1.3f);

    float sly=y+8.6f*mm, slh=4.8f*mm;
    float slx=x+5.0f*mm, slw=fw-10.0f*mm;
    float tcw=36.0f*mm, tcx=x+(fw-tcw)*0.5f, tcy0=y+3.4f*mm;
    float bcw=41.0f*mm, bcx=x+(fw-bcw)*0.5f, bcy1=y+21.2f*mm;
    chamfer_ring(c,tcx,tcy0,tcw,sly-tcy0,1.6f*mm,0.9f*mm);
    chamfer_ring(c,bcx,sly+slh,bcw,bcy1-(sly+slh),1.6f*mm,0.9f*mm);

    /* finger cuts: floors with smooth gradients, eased walls */
    struct { float cx,cw,y0,y1; } cut[2] = {
        { tcx, tcw, tcy0,   sly },
        { bcx, bcw, sly+slh, bcy1 },
    };
    for(int k=0;k<2;k++){
        float cx0=cut[k].cx, cw0=cut[k].cw, y0=cut[k].y0, y1=cut[k].y1;
        float ch0=y1-y0, rr=1.6f*mm;
        if(k==0) rrect(c,cx0,y0,cw0,ch0+rr,rr,pr,pg,pb,0.62f,0.80f);
        else     rrect(c,cx0,y0-rr,cw0,ch0+rr,rr,pr,pg,pb,0.46f,0.84f);
        /* eased side walls */
        soft_vedge(c,y0+rr*0.4f,y1-((k==1)?rr*0.4f:0.0f),cx0+0.2f*mm,1.1f*mm,0.62f,1);
        soft_vedge(c,y0+rr*0.4f,y1-((k==1)?rr*0.4f:0.0f),cx0+cw0-0.2f*mm,1.1f*mm,0.72f,0);
        /* eased overhang shadow at the top of the cut */
        soft_hedge(c,cx0,cx0+cw0,y0,1.8f*mm,0.42f,0.0f,1);
        /* eased lit lip at the bottom inner wall */
        soft_hedge(c,cx0+0.8f*mm,cx0+cw0-0.8f*mm,y1-1,1.3f*mm,
                   (k==1)?1.34f:1.20f,(k==1)?0.05f:0.02f,0);
    }

    /* slot: chamfered opening, eased interior faces */
    chamfer_ring(c,slx,sly,slw,slh,slh*0.24f,1.2f*mm);
    rrect(c,slx,sly,slw,slh,slh*0.24f,12,11,10,1.0f,1.0f);
    { float ch1=1.4f*mm;
      for(int j2=0;j2<(int)ch1;j2++){
        float t2=(float)j2/ch1;
        float e=sinf((1.0f-t2)*1.5708f); e*=e;
        int vt=(int)(14.0f+52.0f*e);
        int vb=(int)(14.0f+86.0f*e);
        for(int i2=(int)(slx+1.2f*mm);i2<(int)(slx+slw-1.2f*mm);i2++){
            px_blend(c,i2,(int)sly+j2,vt,vt-2,vt-4,1.0f);
            px_blend(c,i2,(int)(sly+slh)-1-j2,(int)(vb*1.05f),vb,(int)(vb*0.88f),1.0f);
        }
      }
      soft_hedge(c,slx+1.5f*mm,slx+slw-1.5f*mm,sly+slh,1.0f*mm,1.30f,0.04f,1);
    }
    /* inserted diskette: eased grazing light, soft seam */
    { float dkw=90.0f*mm, dkx=x+(fw-dkw)*0.5f;
      float dky=sly+1.4f*mm, dkh=slh-2.6f*mm;
      for(int j2=(int)dky;j2<(int)(dky+dkh);j2++){
        float t2=(float)(j2-dky)/dkh;
        int v=(int)(62.0f-24.0f*t2);
        for(int i2=(int)dkx;i2<(int)(dkx+dkw);i2++){
            float n=hash2(i2,j2,7)*5.0f;
            px_blend(c,i2,j2,(int)(v+n),(int)(v+n),(int)(v+n+4),1.0f);
        }
      }
      soft_hedge(c,dkx,dkx+dkw,dky,1.0f*mm,1.85f,0.10f,1);
      for(int i2=(int)(dkx+2.0f*mm);i2<(int)(dkx+dkw-2.0f*mm);i2++)
        px_blend(c,i2,(int)(dky+dkh*0.55f),20,20,22,0.22f);
    }

    /* eject: cut opening + protruding cap, eased shine */
    float ew=13.0f*mm, eh=6.0f*mm;
    float ex=x+fw-ew-9.0f*mm, ey=y+16.4f*mm;
    rrect(c,ex-0.8f*mm,ey-0.8f*mm,ew+1.6f*mm,eh+1.6f*mm,1.5f*mm,
          (int)(pr*0.48f),(int)(pg*0.48f),(int)(pb*0.48f),0.90f,1.02f);
    chamfer_ring(c,ex-0.8f*mm,ey-0.8f*mm,ew+1.6f*mm,eh+1.6f*mm,1.5f*mm,0.7f*mm);
    rrect(c,ex,ey,ew,eh,1.2f*mm,
          (int)(pr*1.08f),(int)(pg*1.08f),(int)(pb*1.08f),1.12f,0.86f);
    housing_edge(c,ex,ey,ew,eh,1.2f*mm,1.0f*mm,0.6f*mm,1,1.6f);
    soft_hedge(c,ex+1.2f*mm,ex+ew-1.2f*mm,ey+0.5f*mm,1.0f*mm,1.16f,0.08f,1);

    /* activity light: rectangular window, as in the reference */
    led_rect(c,x+16.5f*mm,y+18.6f*mm,4.6f*mm,2.2f*mm,70,225,60);
}

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

/* THE BEZEL.  Proportions measured off all-in-one-pc-dos.png:
 *
 *   bezel band  ~4.4% of picture height   (at the edge midpoints)
 *   OUTER corner ~3.1%                    (housing meets flat case)
 *   INNER corner ~4.8%                    (moulding meets the glass)
 *
 * The inner radius is LARGER than the outer one, so the band is thicker at
 * the corners than along the edges.  That is not a mistake — it is what a
 * tube in a housing actually looks like, and getting it backwards is what
 * made the previous bezel read as a sticker.
 *
 * The aperture is a rounded box evaluated in the SAME warped space the
 * shader samples in (crt.h), so the opening follows the glass curvature and
 * carries a realistic corner radius at the same time. */

/* signed distance to the aperture, in pixels; <=0 is glass */
static float aperture_sd(const dxm_layout *L,float px,float py,float rin){
    return warped_rr_sd(L,px,py,rin,0.0f);
}

/* The housing's outer edge: a rolled lip that catches a hard specular line
 * along the top and upper-left, falls into shadow along the bottom, and casts
 * a soft contact shadow onto the flat case beneath it.  This is what gives
 * the monitor its depth against the rest of the machine. */
static void housing_edge(canvas *c,float x,float y,float w,float h,float r,
                         float ew,float shadow,int raised,float gain){
    float cx=x+w*0.5f, cy=y+h*0.5f, hw=w*0.5f, hh=h*0.5f;
    float m=ew+shadow+2.0f;
    for(int j=(int)(y-m);j<(int)(y+h+m);j++)
      for(int i=(int)(x-m);i<(int)(x+w+m);i++){
        float sd=rr_sd((float)i,(float)j,cx,cy,hw,hh,r);
        if(sd<-ew || sd>shadow) continue;
        float gx=rr_sd((float)i+1,(float)j,cx,cy,hw,hh,r)
                -rr_sd((float)i-1,(float)j,cx,cy,hw,hh,r);
        float gy=rr_sd((float)i,(float)j+1,cx,cy,hw,hh,r)
                -rr_sd((float)i,(float)j-1,cx,cy,hw,hh,r);
        float gl=sqrtf(gx*gx+gy*gy); if(gl<1e-4f) continue;
        float nx=gx/gl, ny=gy/gl;              /* points OUT of the housing */
        float lam=(nx*LIGHT_X+ny*LIGHT_Y)*(raised?1.0f:-1.0f);
        if(sd<=0.0f){
            /* the rolled lip itself */
            float t=-sd/ew;                     /* 0 at the very edge */
            float prof=(1.0f-t)*(1.0f-t);
            float mul=1.0f+lam*prof*0.50f*gain;
            float sp=fmaxf(lam,0.0f);
            /* the shine: a hard, narrow catch along the top of the roll */
            px_shade(c,i,j,mul,powf(sp,10.0f)*prof*0.52f*gain);
        } else {
            /* contact shadow cast onto the case, opposite the light */
            float t=sd/shadow;
            float occl=fmaxf(raised?-lam:lam,0.0f);
            px_shade(c,i,j,1.0f-occl*(1.0f-t)*(1.0f-t)*0.46f*gain,0.0f);
        }
      }
}

static void bezel(canvas *c,const dxm_layout *L,float bz,float rin,float rmid){
    float m=bz*1.20f;   /* the band only - see below */
    for(int j=(int)(L->tube_y-m);j<(int)(L->tube_y+L->tube_h+m);j++)
      for(int i=(int)(L->tube_x-m);i<(int)(L->tube_x+L->tube_w+m);i++){
        float d=aperture_sd(L,(float)i,(float)j,rin);
        if(d<=0.0f) continue;                    /* glass: the shader owns it */
        float dout=warped_rr_sd(L,(float)i,(float)j,rmid,bz);
        if(dout>=0.0f) continue;                 /* past the band: flat face */
        float t=d/fmaxf(d-dout,1e-3f);           /* 0 at glass, 1 at the edge */
        float sh, spec=0.0f;
        {
            /* The reveal is a WALL, not a dome.  Every point on it faces the
             * light at the same angle, so its tone is essentially CONSTANT
             * across the wall - dark all the way down on the shadowed side,
             * light all the way down on the lit side.  Shading it as a ramp
             * from the shoulder to the glass is what made it read as a raised
             * rounded lip instead of a deep recess. */
            float gx=aperture_sd(L,(float)i+1,(float)j,rin)
                    -aperture_sd(L,(float)i-1,(float)j,rin);
            float gy=aperture_sd(L,(float)i,(float)j+1,rin)
                    -aperture_sd(L,(float)i,(float)j-1,rin);
            float gl=sqrtf(gx*gx+gy*gy);
            float lam=0.0f;
            if(gl>1e-4f){
                /* the wall climbs away from the glass, so it faces back
                 * toward the aperture: normal = -gradient */
                float nx=-gx/gl, ny=-gy/gl;
                lam=nx*LIGHT_X+ny*LIGHT_Y;
            }
            /* Measured off the reference, per side (glass -> shoulder):
             *   top    : reveal LIT well above face tone (~180-206), with a
             *            dark groove right at the shoulder
             *   bottom : face tone, plus a bright catch just off the glass
             *   sides  : darker than the face all the way up
             * i.e. overhead light pours INTO the dish and lands on the wall
             * you see along the top edge; the side walls rake away from it.
             * That asymmetry is what makes the dish read deep. */
            float niy = (gl>1e-4f)? -gy/gl : 0.0f;   /* inward normal, y */
            float nix = (gl>1e-4f)? -gx/gl : 0.0f;
            float k=1.0f-t;                   /* 1 at the glass, 0 at shoulder */
            sh = 1.00f + lam*0.05f;
            sh += fmaxf(niy,0.0f)*0.40f;                   /* top wall lit,
                                                              evenly - the ref
                                                              is bright right
                                                              off the glass  */
            sh -= fabsf(nix)*0.32f;                        /* side walls dark,
                                                              FLAT like the ref
                                                              (~90-115 held)  */
            /* groove where the lit top wall meets the shoulder */
            if(t>0.80f) sh -= fmaxf(niy,0.0f)*0.55f*(t-0.80f)/0.20f;
            /* contact shadow at the glass: narrow on the bottom (the ref
             * jumps from 102 to 155 in one step there), fuller elsewhere */
            float cs = 0.46f*(1.0f-0.62f*fmaxf(-niy,0.0f));
            float kc = k*k*k*k;              /* NARROW: the ref jumps from
                                                contact-dark to lit in one
                                                sample, not a long ramp */
            sh *= 1.0f - cs*kc;
            /* bottom: bright catch just off the glass (fillet facing up) */
            spec += fmaxf(-niy,0.0f)*k*k*k*0.30f;
        }
        float n=plastic_tex(i,j);
        float a=(d<1.3f)?d/1.3f:1.0f;
        px_blend(c,i,j,(int)(PLASTIC_R*(sh+n)+spec*255.0f),
                     (int)(PLASTIC_G*(sh+n)+spec*255.0f),
                     (int)(PLASTIC_B*(sh+n)+spec*255.0f),a);
        /* the glass sits below the moulding: a real shadow, scaled to the
         * bezel so it survives at any resolution instead of being 2px */
        float rw=fmaxf(2.0f,bz*0.13f);
        if(d<rw) px_blend(c,i,j,8,8,7,0.70f*(1.0f-d/rw));

      }
    /* ---- the reveal SHOULDER ----
     * Where the dished reveal rolls back up onto the flat moulding face.
     * It is a convex fillet, so with the light above it takes a hard catch
     * along the top and drops into shadow along the bottom.  This is the
     * edge that gives the bezel its depth; putting the highlight out at the
     * chassis boundary instead was simply the wrong edge. */
    float fw=fmaxf(2.0f,bz*0.30f);
    for(int j=(int)(L->tube_y-m);j<(int)(L->tube_y+L->tube_h+m);j++)
      for(int i=(int)(L->tube_x-m);i<(int)(L->tube_x+L->tube_w+m);i++){
        if(aperture_sd(L,(float)i,(float)j,rin)<=0.0f) continue;
        float dq=warped_rr_sd(L,(float)i,(float)j,rmid,bz);
        /* ONLY between the shoulder and the glass.  This used to run to
         * +fw as well, putting the highlight on the flat moulding face
         * outside the reveal - the flat face must stay uniform. */
        if(dq>0.0f || dq<-fw) continue;
        float gx=warped_rr_sd(L,(float)i+1,(float)j,rmid,bz)
                -warped_rr_sd(L,(float)i-1,(float)j,rmid,bz);
        float gy=warped_rr_sd(L,(float)i,(float)j+1,rmid,bz)
                -warped_rr_sd(L,(float)i,(float)j-1,rmid,bz);
        float gl=sqrtf(gx*gx+gy*gy); if(gl<1e-4f) continue;
        /* A recessed shoulder catches the light on the side FACING the
         * light source - the BOTTOM shoulder, whose fillet tilts up toward
         * it.  The top shoulder shades itself.  So the shine follows the
         * inward normal, not the outward one. */
        float nx=-gx/gl, ny=-gy/gl;        /* inward, toward the glass */
        float lam=nx*LIGHT_X+ny*LIGHT_Y;
        float prof=1.0f+dq/fw;          /* 1 at the shoulder, 0 into the wall */
        prof*=prof;
        float sp=fmaxf(lam,0.0f);
        /* the shoulder is a soft break, not a chrome edge */
        px_shade(c,i,j,1.0f+lam*prof*0.10f,powf(sp,9.0f)*prof*0.10f);
      }
}

uint8_t *chassis_render(const dxm_layout *L,int W,int H){
    canvas C; C.w=W; C.h=H; C.px=calloc((size_t)W*H,4);
    canvas *c=&C;
    g_lbl=fmaxf(1.0f,(float)H/760.0f);
    float inset=H*0.052f, edge=W*0.024f;
    float bz=L->tube_h*BEZEL_BAND;      /* measured off the reference */
    float hous=L->tube_h*BEZEL_HOUSING;

    rrect(c,0,0,(float)W,(float)H,0.0f,PLASTIC_R,PLASTIC_G,PLASTIC_B,1.03f,0.90f);

    /* Edge strips: plain plastic, separated from the face by a seam. */
    {
      seam(c,edge,0,(float)H,1,fmaxf(1.0f,W*0.0012f));
      seam(c,(float)W-edge,0,(float)H,1,fmaxf(1.0f,W*0.0012f));
    }

    /* bezel band -> aperture -> glass.  The housing face itself is NOT
     * repainted: it is flush with the case, and giving it its own plate with
     * its own shade ramp left a visible tone step against the surrounding
     * plastic.  The base coat IS the housing face. */
    float rin=L->tube_h*BEZEL_R_IN;
    bezel(c,L,bz,rin,L->tube_h*BEZEL_R_MID);
    for(int j=(int)(L->tube_y-hous);j<(int)(L->tube_y+L->tube_h+hous);j++)
      for(int i=(int)(L->tube_x-hous);i<(int)(L->tube_x+L->tube_w+hous);i++)
        if(aperture_sd(L,(float)i,(float)j,rin)<=0.0f) px_set(c,i,j,5,6,5);

    /* ---- speaker columns, one each side of the tube ---- */
    {
        float gl2=L->tube_x-hous-(edge+inset*0.45f);   /* space per side */
        float gw=gl2-inset*0.35f;
        if(gw>inset*0.5f){
            float gh=L->tube_h*0.5f;
            float gy=L->tube_y+(L->tube_h-gh)*0.5f;   /* centred on the tube */
            grille_panel(c,edge+inset*0.45f,gy,gw,gh,H*0.019f);
            grille_panel(c,(float)W-edge-inset*0.45f-gw,gy,gw,gh,H*0.019f);
        }
    }

    /* ---- bottom band: badge | power+LEDs | volume/phones | floppy ---- */
    float band_y=L->tube_y+L->tube_h+hous+inset*0.22f;
    float band_h=(float)H-inset*0.55f-band_y;
    if(band_h>inset*0.8f){
        seam(c,edge,band_y-inset*0.26f,(float)W-2*edge,0,fmaxf(1.0f,W*0.0012f));
        float mid=band_y+band_h*0.46f;
        float mm=H/268.0f;              /* ONE physical scale, tied to the
                                           DISPLAY, not the band - so slimming
                                           the band does not shrink the
                                           devices mounted on it */

        /* badge: the logo, kept, but narrow */
        float pbw=fminf(W*0.115f,L->tube_h*0.34f);
        float pbh=fminf(band_h*0.52f,pbw*0.42f);
        float pbx=edge+inset*0.65f, pby=mid-pbh*0.5f;
        g_grain=0;                       /* the badge is a printed label */
        rrect(c,pbx,pby,pbw,pbh,pbh*0.10f,0x22,0x26,0x30,1.0f,0.82f);
        housing_edge(c,pbx,pby,pbw,pbh,pbh*0.10f,
                     fmaxf(2.0f,pbh*0.09f),fmaxf(2.0f,pbh*0.08f),0,1.0f);
        { float sc=fminf(fmaxf(1.0f,pbw/110.0f),pbh/30.0f);
          text(c,pbx+pbw*0.10f,pby+pbh*0.16f,"PC-486",sc*1.35f,0xEC,0xE8,0xDC);
          text(c,pbx+pbw*0.10f,pby+pbh*0.56f,"MULTIMEDIA",sc*0.60f,0xA8,0xB0,0xC6);
          int cols[4][3]={{0x2E,0x4C,0xA8},{0x2E,0x8C,0x50},{0xC8,0x9A,0x28},{0xB8,0x3C,0x34}};
          for(int k=0;k<4;k++)
            rect(c,pbx+pbw*0.10f,pby+pbh*(0.78f+k*0.048f),pbw*0.52f,
                 fmaxf(1.0f,pbh*0.030f),cols[k][0],cols[k][1],cols[k][2]); }
        g_grain=1;

        /* power button + status LEDs */
        float px0=pbx+pbw+inset*0.85f;
        float pw=16.0f*mm;                       /* a 16mm power cap */
        text(c,px0,mid-pw*0.39f-g_lbl*11,"POWER",g_lbl,86,82,74);
        /* thin cut around the button, cap nearly filling it */
        rrect(c,px0,mid-pw*0.39f,pw,pw*0.78f,pw*0.10f,64,61,56,0.80f,0.92f);
        rrect(c,px0+pw*0.045f,mid-pw*0.39f+pw*0.04f,pw*0.91f,pw*0.70f,pw*0.08f,
              PLASTIC_R,PLASTIC_G,PLASTIC_B,1.16f,0.84f);
        bevel(c,px0+pw*0.045f,mid-pw*0.39f+pw*0.04f,pw*0.91f,pw*0.70f,
              fmaxf(1.0f,pw*0.06f),1);
        /* power LED: a small round lens under the button */
        led(c,px0+pw*0.5f,mid-pw*0.39f+pw*0.78f+fmaxf(4.0f,band_h*0.09f),
            1.35f*mm,70,225,60);

        /* centre: stereo sound, volume, phones */
        float cxm=(float)W*0.5f;
        text(c,cxm-g_lbl*8*6.0f,band_y+band_h*0.12f,"STEREO SOUND",g_lbl,96,92,84);
        rect(c,cxm-g_lbl*8*6.0f,band_y+band_h*0.12f+g_lbl*11,g_lbl*8*7.0f,
             fmaxf(1.0f,g_lbl*1.6f),0x2E,0x4C,0xA8);
        slider(c,cxm-g_lbl*8*5.0f,band_y+band_h*0.42f,g_lbl*8*10.0f,band_h*0.28f,0.62f);
        text(c,cxm-g_lbl*8*2.0f,band_y+band_h*0.78f,"VOLUME",g_lbl,96,92,84);
        float jx=cxm+g_lbl*8*8.0f;
        float js=9.0f*mm;                        /* 9mm jack surround */
        rrect(c,jx,mid-js*0.5f,js,js,js*0.5f,64,61,56,0.7f,0.8f);
        bevel(c,jx,mid-js*0.5f,js,js,fmaxf(1.0f,js*0.12f),0);
        led(c,jx+js*0.5f,mid,3.2f*mm,14,13,12);  /* 6.35mm hole */
        text(c,jx-g_lbl*8*1.0f,band_y+band_h*0.78f,"PHONES",g_lbl,96,92,84);

        /* floppy drive: a real 3.5" face is 101.6 x 25.4 mm, centred
         * vertically between the divider ridge and the case bottom */
        float fh=25.4f*mm, fw2=101.6f*mm;
        float fx=(float)W-edge-inset*0.65f-fw2;
        float fmid=((band_y-inset*0.26f)+(float)H)*0.5f;
        floppy_drive(c,fx,fmid-fh*0.5f,fw2,fh);
    }

    return C.px;
}
