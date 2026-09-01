/* chassis.c — the machine, drawn from parameters (SPEC §6.1).  No raster art.
 * Rendered once into an RGBA buffer at startup / resolution change; per frame
 * it is one texture. */
#include "chassis.h"
#include "font.h"
#include "dxm_road.h"
#include "sb_logo.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "crt.h"

/* ---- chassis_params: every dimension and colour in one place (SPEC §5) --- */
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
                          float r,float grow,float warp){
    float tx=(px-L->tube_x)/L->tube_w, ty=(py-L->tube_y)/L->tube_h;
    float bx,by; barrel_cpu(tx,ty,warp,&bx,&by);
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

/* Rectangular LED window.  A drive-activity light is a FLAT-FRONTED light
 * pipe, not a bead: the face is a plane, the corners are barely eased, and
 * the plastic is frosted so it scatters rather than reflecting a highlight.
 * The generous corner radius and the gaussian hot spot this replaces are
 * exactly what made it read as a little round lens. */
static void led_rect(canvas *c,float cx,float cy,float w,float h,
                     int r,int g,int b){
    float hw=w*0.5f, hh=h*0.5f;
    float rad=h*0.15f;                    /* eased, not rounded */
    float ring=fmaxf(1.2f,h*0.16f);
    float pitch=fmaxf(1.6f,h*0.17f);      /* diffuser striation pitch */
    for(int j2=(int)(cy-hh-ring*3);j2<=(int)(cy+hh+ring*3);j2++)
        for(int i2=(int)(cx-hw-ring*3);i2<=(int)(cx+hw+ring*3);i2++){
            float sd=rr_sd((float)i2,(float)j2,cx,cy,hw,hh,rad);
            float dy=(j2-cy)/hh;
            if(sd<=0.0f){
                float e=-sd/h;                     /* depth in from the frame */
                /* A plane takes an even wash.  Only the very bottom falls
                 * off, where the frame shades it. */
                float f=0.99f-0.12f*fmaxf(0.0f,dy);
                if(e<0.16f) f*=0.66f+2.10f*e;      /* the moulded frame */
                /* frosted plastic: fine grain, plus the horizontal tool
                 * striations a moulded pipe carries */
                f += (hash2(i2,j2,7)-0.5f)*0.14f;
                f += sinf((float)j2*3.14159265f/pitch)*0.045f;
                /* the bevel along the top of the face catches a thin line -
                 * a flat front has an EDGE, which is what sells it as flat */
                float bev=(e<0.11f&&dy<0.0f)?0.17f*(1.0f-e/0.11f):0.0f;
                px_blend(c,i2,j2,(int)(r*f+255.0f*bev),
                                 (int)(g*f+255.0f*bev),
                                 (int)(b*f+255.0f*bev),1.0f);
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

/* The sound-card sticker.  Everything else on this machine was moulded or
 * printed at the factory; this is the one mark a PREVIOUS OWNER left, so it
 * is applied ON the case - clear laminate margin, contact shadow round the
 * edge, and the gloss catch vinyl has and plastic does not.
 *
 * The artwork is the real logo, baked (tools/mklogo.py).  Setting it in the
 * 8x8 case font got the words right and everything else wrong: the mark has
 * letterforms of its own - the triangular A over its rule, the notched E -
 * and faking those is exactly the sort of thing that reads as a cartoon. */
static void sb_sticker(canvas *c,float cx,float cy,float w){
    float lw=w*0.885f;                       /* the print inside the laminate */
    float lh=lw*(float)SB_LOGO_HT/(float)SB_LOGO_W;
    float h=lh+w*0.115f;
    float x=cx-w*0.5f, y=cy-h*0.5f;
    float rad=h*0.16f, hw=w*0.5f, hh=h*0.5f;
    g_grain=0;                               /* printed vinyl has no grain */

    /* the shadow it casts on the pod, which is what puts it on top */
    { float sw=h*0.13f;
      for(int j2=(int)(y-sw-1);j2<(int)(y+h+sw+1);j2++)
        for(int i2=(int)(x-sw-1);i2<(int)(x+w+sw+1);i2++){
            float sd=rr_sd((float)i2,(float)j2,cx,cy,hw,hh,rad);
            if(sd<=0.0f||sd>sw) continue;
            float t=sd/sw, dyn=((float)j2-cy)/hh;
            /* deeper below, away from the key light */
            float side=0.55f+0.60f*fmaxf(0.0f,dyn);
            px_shade(c,i2,j2,1.0f-0.17f*(1.0f-t)*(1.0f-t)*side,0.0f);
        }
    }

    /* the clear laminate the print sits inside */
    rrect(c,x,y,w,h,rad, 234,234,230, 1.0f,0.96f);

    /* the artwork, box-filtered down to whatever size it landed at */
    { float lx=cx-lw*0.5f, ly=cy-lh*0.5f;
      float sx=(float)SB_LOGO_W/lw, sy=(float)SB_LOGO_HT/lh;
      for(int j2=0;j2<(int)lh;j2++)
        for(int i2=0;i2<(int)lw;i2++){
            int u0=(int)(i2*sx), u1=(int)((i2+1)*sx); if(u1<=u0) u1=u0+1;
            int v0=(int)(j2*sy), v1=(int)((j2+1)*sy); if(v1<=v0) v1=v0+1;
            if(u1>SB_LOGO_W) u1=SB_LOGO_W;
            if(v1>SB_LOGO_HT) v1=SB_LOGO_HT;
            int r=0,g=0,b=0,n=0;
            for(int v=v0;v<v1;v++)
              for(int u=u0;u<u1;u++){
                const uint8_t *sp=sb_logo+((size_t)v*SB_LOGO_W+u)*4;
                r+=sp[0]; g+=sp[1]; b+=sp[2]; n++;
              }
            if(!n) continue;
            px_blend(c,(int)lx+i2,(int)ly+j2,r/n,g/n,b/n,1.0f);
        }
    }

    /* vinyl is glossy: one broad diagonal catch, which is the difference
     * between a label and a printed rectangle */
    for(int j2=(int)y;j2<(int)(y+h);j2++)
      for(int i2=(int)x;i2<(int)(x+w);i2++){
        if(rr_sd((float)i2,(float)j2,cx,cy,hw,hh,rad)>0.0f) continue;
        float u=((float)i2-x)/w + (((float)j2-y)/h)*0.50f;
        float d=(u-0.40f)/0.22f;
        float a=expf(-d*d)*0.11f;
        if(a>0.004f) px_blend(c,i2,j2,255,255,255,a);
      }
    g_grain=1;
}

/* The parting between the two mouldings: the monitor housing sits ON the
 * base, and where two parts meet there is a real gap, not a drawn line.  A
 * gap has a cross-section - the upper part overhangs and casts into it, the
 * floor sits in shade, and the base's own top edge comes back up into the
 * light - and that cross-section is the whole reason it reads as two parts
 * rather than as a scratch across one. */
static void panel_gap(canvas *c,float x,float y,float w,float d){
    float lip=d*0.55f;                        /* how far the light spreads  */
    for(int j2=(int)(y-lip-1);j2<(int)(y+d+lip+1);j2++){
        float v=((float)j2+0.5f-y)/d;         /* 0 at the top of the gap    */
        float mul=1.0f, spec=0.0f;
        if(v<0.0f){
            /* the face above, darkened as it turns down into the gap */
            float t=-v*d/lip; if(t>1.0f) continue;
            mul=1.0f-0.20f*(1.0f-t)*(1.0f-t);
        } else if(v<=1.0f){
            /* inside: darkest just under the overhang, lifting toward the
             * floor, and the far wall picks up a little bounce */
            float u=v;
            mul=0.34f+0.30f*u*u;
        } else {
            /* the base's top edge, catching the key light square on */
            float t=(v-1.0f)*d/lip; if(t>1.0f) continue;
            float e=(1.0f-t)*(1.0f-t);
            mul=1.0f+0.26f*e;
            spec=e*0.075f;
        }
        for(int i2=(int)x;i2<(int)(x+w);i2++)
            px_shade(c,i2,j2,mul,spec);
    }
    /* the hard line where the overhang begins - one dark row, so the top of
     * the gap has an edge instead of fading in */
    for(int i2=(int)x;i2<(int)(x+w);i2++)
        px_blend(c,i2,(int)y,18,17,15,0.55f);
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
    float crad=fminf(hw,hh)*0.16f;      /* rectangular, corners just eased */
    float hp=fmaxf(2.0f,pitch*0.21f);            /* hole pitch  */
    float hr=hp*0.25f;                           /* hole radius */
    /* The grid is CENTRED on the panel, not run left-to-right until it
     * runs out - otherwise the leftover margin differs from the starting
     * one and each grille comes out lopsided.  Rows alternate between n and
     * n-1 columns, which gives the hex offset while keeping every row
     * symmetric about the centre. */
    float rowstep = hp*0.87f;
    int nrow = (int)floorf((hh*2.0f)/rowstep);
    int ncol = (int)floorf((hw*2.0f)/hp);
    if(nrow<1) nrow=1;
    if(ncol<1) ncol=1;
    for(int row=0; row<nrow; row++){
        float cy2 = ccy + (row - (nrow-1)*0.5f)*rowstep;
        int n = (row&1) ? ncol-1 : ncol;
        for(int ci=0; ci<n; ci++){
            float cx2 = ccx + (ci - (n-1)*0.5f)*hp;
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

/* Text MOULDED INTO the plastic rather than printed on it: the same colour
 * as the case, visible only by its shading - lit along the top of the
 * raised stroke, shadowed beneath.  Every label on this machine was painted
 * until now, and a case with no moulded marks reads as a decal sheet. */
static void moulded_text(canvas *c,float x,float y,const char *s,float sc,
                         int debossed){
    /* depth: a struck mark reads deeper than a raised one, so give the
     * debossed case a stronger shadow above and a brighter lit lower lip */
    float lit  = debossed ? -0.30f :  0.19f;
    float dark = debossed ?  0.34f : -0.15f;
    for(int n=0;s[n];n++){
        const uint8_t *gl=font_glyph((unsigned char)s[n]);
        for(int j=0;j<8;j++) for(int i=0;i<8;i++){
            if(!(gl[j]&(0x80>>i))) continue;
            for(int sy=0;sy<(int)sc;sy++) for(int sx=0;sx<(int)sc;sx++){
                int px2=(int)(x+(n*8+i)*sc)+sx, py2=(int)(y+j*sc)+sy;
                px_shade(c,px2,py2,1.0f+dark*0.34f,0.0f);   /* the stroke */
                px_shade(c,px2,py2-1,1.0f+lit,0.0f);        /* upper wall */
                px_shade(c,px2,py2-2,1.0f+lit*0.55f,0.0f);
                px_shade(c,px2,py2+(int)sc,1.0f+dark,0.0f); /* lower wall */
                px_shade(c,px2,py2+(int)sc+1,1.0f+dark*0.55f,0.0f);
            }
        }
    }
}


/* 3.5" drive: same geometry as before (recessed plate, finger cuts, slot
 * through them, protruding eject, inserted diskette), but every edge is now
 * an eased ramp at the LED's fidelity - no hard 1px lines anywhere. */
static float g_fdd_led[4], g_pwr_led[4];   /* recorded LED placeholders */
static void floppy_drive(canvas *c,float x,float y,float w,float h){
    float mm=h/25.4f; (void)w;
    float fw=101.6f*mm;
    /* the drive is a separate moulding: darker and greyer than the case */
    int pr=(int)(PLASTIC_R*0.74f),pg=(int)(PLASTIC_G*0.72f),pb=(int)(PLASTIC_B*0.76f);
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
        if(k==0){
            /* With a diskette inserted the spring door has swung INWARD, so
             * the top cut is an empty hole into the drive - not a moulded
             * floor.  Near-black, lifting a little at the bottom where light
             * from the room reaches in past the lip. */
            rrect(c,cx0,y0,cw0,ch0+rr,rr,17,17,19,0.55f,1.55f);
        }
        else rrect(c,cx0,y0-rr,cw0,ch0+rr,rr,pr,pg,pb,0.46f,0.84f);
        /* eased side walls (the hole needs no lit walls - it is open) */
        if(k==1){
            soft_vedge(c,y0+rr*0.4f,y1-rr*0.4f,cx0+0.2f*mm,1.1f*mm,0.62f,1);
            soft_vedge(c,y0+rr*0.4f,y1-rr*0.4f,cx0+cw0-0.2f*mm,1.1f*mm,0.72f,0);
        }
        /* eased overhang shadow at the top of the cut - deeper on the hole */
        soft_hedge(c,cx0,cx0+cw0,y0,(k==0)?2.4f*mm:1.8f*mm,
                   (k==0)?0.22f:0.42f,0.0f,1);
        /* lit lip at the bottom inner wall; on the hole this is the front
         * face catching light at the opening's edge, so it stays subtle */
        soft_hedge(c,cx0+0.8f*mm,cx0+cw0-0.8f*mm,y1-1,1.3f*mm,
                   (k==1)?1.34f:1.12f,(k==1)?0.05f:0.0f,0);
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
    /* ---- the inserted diskette --------------------------------------
     * A 3.5" disk is a square with rounded corners; inserted, we see its
     * trailing edge end-on, so those corners round in from LEFT and RIGHT.
     * The shell is thicker at the two ends than in the middle, because the
     * centre section is recessed to take the label - and that label wraps
     * around the bottom edge, so a pale band shows along the lower half of
     * the middle. */
    { float dkw=90.0f*mm, dkx=x+(fw-dkw)*0.5f;
      float dky=sly+1.2f*mm, dkh=slh-2.4f*mm;
      float corner=2.6f*mm;                  /* the shell's rounded corners */
      float recess=0.5f*mm;                  /* label recess in the middle  */
      float lab_x0=dkx+11.0f*mm, lab_x1=dkx+dkw-11.0f*mm;
      for(int i2=(int)dkx;i2<(int)(dkx+dkw);i2++){
        float fx=(float)i2-dkx;
        /* The shell's rounded ends curve AWAY from the viewer, so they
         * shade off toward each end exactly as the chassis side bars do -
         * a cosine falloff, not a change in height. */
        float ce = fminf(fx, dkw-1.0f-fx);
        float u  = (ce<corner) ? (1.0f-ce/corner) : 0.0f;   /* 0 flat, 1 end */
        float curve = 0.40f + 0.60f*cosf(u*1.30f);
        /* the ends are FULL height; the middle is recessed for the label */
        int inLabel = (i2>=(int)lab_x0 && i2<(int)lab_x1);
        float top = dky + (inLabel? recess : 0.0f);
        float bot = dky + dkh - (inLabel? recess*0.5f : 0.0f);
        for(int j2=(int)top;j2<(int)bot;j2++){
            float t2=(bot>top)?((float)j2-top)/(bot-top):0.0f;
            float n=hash2(i2,j2,7)*5.0f;
            int r2,g2,b2;
            if(inLabel && t2>0.42f){
                /* the paper label, wrapped around the bottom edge */
                float lt=(t2-0.42f)/0.58f;
                int v=(int)(196.0f-46.0f*lt);
                r2=v; g2=(int)(v*0.985f); b2=(int)(v*0.92f);   /* warm paper */
            } else {
                int v=(int)(60.0f-22.0f*t2);                   /* dark shell */
                r2=v; g2=v; b2=v+4;
            }
            px_blend(c,i2,j2,(int)((r2+n)*curve),(int)((g2+n)*curve),
                     (int)((b2+n)*curve),1.0f);
        }
        /* grazing light along the disk's top edge */
        px_blend(c,i2,(int)top,   (int)(210*curve),(int)(210*curve),
                 (int)(216*curve),0.55f);
        px_blend(c,i2,(int)top+1, (int)(150*curve),(int)(150*curve),
                 (int)(156*curve),0.30f);
        /* and the shadow it casts into the slot below itself */
        px_blend(c,i2,(int)bot-1, 12,12,14,0.45f);
      }
      /* the step where the recessed label area meets the thicker ends */
      for(int e=0;e<2;e++){
        float ex2 = e? lab_x1 : lab_x0;
        for(int j2=(int)(dky);j2<(int)(dky+dkh);j2++)
            px_blend(c,(int)ex2,j2,18,18,20,0.35f);
      }
    }

    /* eject: cut opening + protruding cap, eased shine */
    /* Eject button, matched to a photograph of a real drive: a rounded cap
     * standing in its opening, outlined by a THIN dark gap all round - a
     * fine line, not a frame - with the cap face LIGHTER than the
     * surrounding plastic and carrying a soft top-to-bottom gradient.
     * There is no cast shadow and no exposed stem: the whole cue is that
     * hairline gap plus the cap being brighter than its surroundings. */
    float ew=11.6f*mm, eh=5.3f*mm;
    float ex=x+fw-ew-9.0f*mm, ey=y+16.2f*mm;
    float gap=0.40f*mm;                       /* the dark outline */

    float top=1.45f*mm;                       /* how far it stands proud */
    float rad=0.30f*mm;                       /* barely-there corner ease  */

    /* Only the RECESS ABOVE the cap is dark - that is the opening the
     * button has come out of.  The sides and bottom get no outline: the
     * cap simply sits on the face there, and a soft shadow underneath does
     * the work instead. */
    rrect(c,ex-gap,ey-gap-top,ew+gap*2,gap+top,rad,
          (int)(pr*0.44f),(int)(pg*0.44f),(int)(pb*0.44f),0.92f,1.0f);

    /* soft blurred shadow cast below the extended cap.  Two passes of a
     * widening, fading band read as penumbra rather than a drawn line. */
    for(int k=0;k<(int)(2.2f*mm);k++){
        float u=(float)k/(2.2f*mm);
        float a=(1.0f-u)*(1.0f-u)*0.34f;
        float spread=u*1.4f*mm;
        int yy=(int)(ey+eh)+k;
        for(int i2=(int)(ex-spread);i2<(int)(ex+ew+spread);i2++){
            /* fade the shadow off toward its ends so it has no hard edge */
            float e=1.0f;
            float dl=(i2-(ex-spread))/(1.6f*mm), dr=((ex+ew+spread)-i2)/(1.6f*mm);
            if(dl<1.0f) e=dl;
            if(dr<1.0f) e=fminf(e,dr);
            if(e<=0.0f) continue;
            px_shade(c,i2,yy,1.0f-a*e,0.0f);
        }
    }

    /* PERSPECTIVE: the drive sits below eye level, so we look slightly down
     * on it and see the button's TOP FACE - a thin lit band above the front
     * face, drawn as a shallow trapezoid (narrowing with distance) and
     * bright because it faces up toward the light.  Without this the cap
     * reads as flush no matter how the gap is drawn. */
    for(int k=0;k<(int)top;k++){
        float u=(float)k/top;                 /* 0 at the front, 1 far edge */
        float inset=u*0.9f*mm;                /* the trapezoid narrowing    */
        float sh=1.30f-0.16f*u;               /* lit, easing back           */
        int yy=(int)(ey-k);
        for(int i2=(int)(ex+inset);i2<(int)(ex+ew-inset);i2++){
            float n=plastic_tex(i2,yy);
            px_blend(c,i2,yy,(int)(pr*(sh+n)),(int)(pg*(sh+n)),
                     (int)(pb*(sh+n)),1.0f);
        }
    }
    /* the crease where the top face turns into the front face */
    for(int i2=(int)ex;i2<(int)(ex+ew);i2++)
        px_blend(c,i2,(int)ey,255,252,246,0.30f);

    /* the cap face */
    rrect(c,ex,ey,ew,eh,rad,
          (int)(pr*1.10f),(int)(pg*1.10f),(int)(pb*1.10f),1.10f,0.94f);
    housing_edge(c,ex,ey,ew,eh,rad,0.9f*mm,0.0f,1,0.9f);

    /* activity light: rectangular window, as in the reference */
    led_rect(c,x+16.5f*mm,y+18.6f*mm,4.6f*mm,2.2f*mm, 26,44,26);  /* UNLIT */
    g_fdd_led[0]=x+16.5f*mm-2.3f*mm; g_fdd_led[1]=y+18.6f*mm-1.1f*mm;
    g_fdd_led[2]=4.6f*mm;            g_fdd_led[3]=2.2f*mm;
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
    return warped_rr_sd(L,px,py,rin,0.0f,DXM_WARP);
}
/* the shoulder: same construction, gentler curvature */
static float shoulder_sd(const dxm_layout *L,float px,float py,
                         float rmid,float bz){
    return warped_rr_sd(L,px,py,rmid,bz,DXM_WARP*BEZEL_R_MID_WARP);
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
        float dout=shoulder_sd(L,(float)i,(float)j,rmid,bz);
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
            /* The wall's own shading, at FULL strength across the dish. */
            float dev = lam*0.05f;
            dev += fmaxf(niy,0.0f)*0.13f;       /* top wall: a little bounce */
            dev -= fabsf(nix)*0.32f;            /* side walls rake away      */
            /* (a 'groove' term used to subtract light here wherever the
             * wall faced down - i.e. precisely the top of the dish as it
             * met the shoulder.  That was the dark band; the shoulder is a
             * convex edge and should catch light there, not lose it.) */
            /* ROUNDED SHOULDER: only the last stretch before the flat face
             * rolls off, so the corner is a fillet instead of a crease.  The
             * dish keeps its depth - fading this across the whole wall (an
             * earlier attempt) just flattened the recess. */
            float fu=(t-FILLET_START)/(1.0f-FILLET_START);
            if(fu<0.0f) fu=0.0f; if(fu>1.0f) fu=1.0f;
            float roll=1.0f-fu*fu*(3.0f-2.0f*fu);
            sh = 1.00f + dev*roll;
            /* contact shadow at the glass: narrow on the bottom (the ref
             * jumps from 102 to 155 in one step there), fuller elsewhere */
            float cs = 0.30f*(1.0f-0.62f*fmaxf(-niy,0.0f));
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
        /* The glass sits below the moulding, so there IS a contact line -
         * but it should be exactly that.  At 0.13 of the bezel it was ~10px
         * of near-black, which together with the contact-shadow term above
         * read as a dark blurry band rather than an edge. */
        float rw=fmaxf(1.5f,bz*0.045f);
        if(d<rw) px_blend(c,i,j,14,14,13,0.50f*(1.0f-d/rw));

      }
    /* ---- the reveal SHOULDER ----
     * Where the dished reveal rolls back up onto the flat moulding face.
     * It is a convex fillet, so with the light above it takes a hard catch
     * along the top and drops into shadow along the bottom.  This is the
     * edge that gives the bezel its depth; putting the highlight out at the
     * chassis boundary instead was simply the wrong edge. */
    float fw=fmaxf(2.0f,bz*0.30f);   /* roll width: wider reads rounder, and
                                        past about half the band the moulding
                                        stops looking moulded and starts
                                        looking inflated */
    for(int j=(int)(L->tube_y-m);j<(int)(L->tube_y+L->tube_h+m);j++)
      for(int i=(int)(L->tube_x-m);i<(int)(L->tube_x+L->tube_w+m);i++){
        if(aperture_sd(L,(float)i,(float)j,rin)<=0.0f) continue;
        /* same shoulder the band is cut against, or the highlight sits
         * off the edge it is lighting */
        float dq=shoulder_sd(L,(float)i,(float)j,rmid,bz);
        /* A rounded edge STRADDLES the boundary.  Clamping this to dq<=0
         * meant the profile peaked exactly at the shoulder and stopped
         * dead - which on the side where that peak is a darkening showed
         * as a crisp line against the flat face. */
        float outw=fw*0.55f;
        if(dq<-fw || dq>outw) continue;
        float gx=shoulder_sd(L,(float)i+1,(float)j,rmid,bz)
                -shoulder_sd(L,(float)i-1,(float)j,rmid,bz);
        float gy=shoulder_sd(L,(float)i,(float)j+1,rmid,bz)
                -shoulder_sd(L,(float)i,(float)j-1,rmid,bz);
        float gl=sqrtf(gx*gx+gy*gy); if(gl<1e-4f) continue;
        /* A recessed shoulder catches the light on the side FACING the
         * light source - the BOTTOM shoulder, whose fillet tilts up toward
         * it.  The top shoulder shades itself.  So the shine follows the
         * inward normal, not the outward one. */
        float nx=-gx/gl, ny=-gy/gl;        /* inward, toward the glass */
        float lam=nx*LIGHT_X+ny*LIGHT_Y;
        float prof = (dq<0.0f) ? (1.0f+dq/fw)      /* into the dish  */
                               : (1.0f-dq/outw);   /* onto the face  */
        prof*=prof;
        /* Only ever ADD light.  The signed term darkened the shoulder on
         * the side facing away from the light, which showed as a shadow
         * band along the top of the transition - a fillet that catches a
         * highlight on one side should not carve a shadow on the other. */
        /* A convex fillet presents every angle to the light, so it picks
         * up a catch the whole way round - more on the side facing the
         * source, but never nothing. */
        float sp=fmaxf(lam,0.0f);
        px_shade(c,i,j,1.0f+(0.055f+sp*0.10f)*prof,
                 powf(sp,9.0f)*prof*0.10f);
      }
}

uint8_t *chassis_render(dxm_layout *L,int W,int H){
    canvas C; C.w=W; C.h=H; C.px=calloc((size_t)W*H,4);
    canvas *c=&C;
    g_lbl=fmaxf(1.0f,(float)H/760.0f);
    float inset=H*0.052f, edge=W*0.024f;
    float bz=L->tube_h*BEZEL_BAND;      /* measured off the reference */
    float hous=L->tube_h*BEZEL_HOUSING;

    rrect(c,0,0,(float)W,(float)H,0.0f,PLASTIC_R,PLASTIC_G,PLASTIC_B,1.03f,0.90f);

    /* Edge strips: the case turning away from the viewer.  They are the
     * SIDES of the box, so they fall off in shade toward each outer edge -
     * darker plastic, never black - which is what reads as roundness.  A
     * thin lit line sits where the side meets the front face, the way a
     * moulded corner catches the light. */
    {
      for(int side=0;side<2;side++){
        float x0 = side ? (float)W-edge : 0.0f;
        for(int i2=0;i2<(int)edge;i2++){
            /* u: 0 at the front face, 1 at the outer edge of the machine */
            float u = side ? (float)i2/edge : 1.0f-(float)i2/edge;
            /* cosine falloff - a cylinder turning away from the light */
            float sh = 0.42f + 0.58f*cosf(u*1.28f);
            /* the corner catches a highlight just off the face */
            float lit = expf(-((u-0.10f)*(u-0.10f))/0.0075f)*0.16f;
            int x = (int)x0 + i2;
            for(int j2=0;j2<H;j2++){
                uint8_t *q=c->px+((size_t)j2*c->w+x)*4;
                for(int k=0;k<3;k++){
                    float v=q[k]*sh + lit*255.0f;
                    q[k]=(uint8_t)(v<0?0:v>255?255:v);
                }
            }
        }
      }
      seam(c,edge,0,(float)H,1,fmaxf(1.0f,W*0.0012f));
      seam(c,(float)W-edge,0,(float)H,1,fmaxf(1.0f,W*0.0012f));
    }

    /* bezel band -> aperture -> glass.  The housing face itself is NOT
     * repainted: it is flush with the case, and giving it its own plate with
     * its own shade ramp left a visible tone step against the surrounding
     * plastic.  The base coat IS the housing face. */
    float rin=L->tube_h*BEZEL_R_IN;
    L->aperture_r=rin;   /* the shader cuts its glass to the same shape */
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
            /* Each grille sits in a raised moulded POD - a tall rounded
             * pad standing a fraction proud of the flank, with the holes
             * punched through its middle.  This is how the real cases did
             * it: one moulded feature carrying the speaker, rather than
             * decoration applied around it.  It also gives the space above
             * and below the holes something to be - plateau face - instead
             * of leaving it bare or striping it. */
            float pw=gw*0.88f, ph=L->tube_h*0.94f;
            float py=L->tube_y+(L->tube_h-ph)*0.5f;
            float prad=fminf(pw,ph)*0.11f;
            float pxs[2]={edge+inset*0.45f+(gw-pw)*0.5f,
                          (float)W-edge-inset*0.45f-gw+(gw-pw)*0.5f};
            for(int s2=0;s2<2;s2++){
                float px0=pxs[s2];
                /* the proud face catches marginally more of the key light */
                for(int j2=(int)py;j2<(int)(py+ph);j2++)
                  for(int i2=(int)px0;i2<(int)(px0+pw);i2++)
                    if(rr_sd((float)i2,(float)j2,px0+pw*0.5f,py+ph*0.5f,
                             pw*0.5f,ph*0.5f,prad)<0.0f)
                        px_shade(c,i2,j2,1.022f,0.0f);
                housing_edge(c,px0,py,pw,ph,prad,
                             fmaxf(1.5f,(float)W*0.0022f),
                             fmaxf(2.0f,(float)W*0.0030f),1,0.85f);
                /* moulding bosses tucked into the pod corners */
                /* A real ejector boss is almost invisible EXCEPT at its
                 * rim - the flat top sits flush with the face around it.
                 * Darkening the whole disc instead read as a stamped spot,
                 * the more so because it came out the same radius as the
                 * pod corner it sat in. */
                { float br=1.6f*(float)H/268.0f, in2=prad*0.86f;
                  float bp[4][2]={{in2,in2},{pw-in2,in2},
                                  {in2,ph-in2},{pw-in2,ph-in2}};
                  for(int k=0;k<4;k++){
                    float bx=px0+bp[k][0], by=py+bp[k][1];
                    for(int j2=(int)(by-br-1);j2<=(int)(by+br+1);j2++)
                      for(int i2=(int)(bx-br-1);i2<=(int)(bx+br+1);i2++){
                        float dx2=i2-bx, dy2=j2-by;
                        float dd=sqrtf(dx2*dx2+dy2*dy2);
                        if(dd>br) continue;
                        float rim=1.0f-fabsf(dd-br*0.88f)/(br*0.14f);
                        if(rim<0.0f) rim=0.0f;
                        px_shade(c,i2,j2,1.0f-0.005f+0.030f*rim*(-dy2/br),0.0f);
                      }
                  }
                }
            }
            grille_panel(c,edge+inset*0.45f,gy,gw,gh,H*0.019f);
            grille_panel(c,(float)W-edge-inset*0.45f-gw,gy,gw,gh,H*0.019f);
            /* the sound-card sticker, on the RIGHT pod under the holes */
            { float sw=pw*0.54f;
              float y0=gy+gh*0.94f, y1=py+ph;      /* holes end .. pod ends */
              float sh=sw*0.885f*0.5f+sw*0.115f;   /* what sb_sticker builds */
              if(y1-y0>sh*1.30f)
                sb_sticker(c,pxs[1]+pw*0.5f,(y0+y1)*0.5f,sw);
            }
        }
    }

    /* ---- bottom band: badge | power+LEDs | volume/phones | floppy ---- */
    float band_y=L->tube_y+L->tube_h+hous+inset*0.22f;
    float band_h=(float)H-inset*0.55f-band_y;
    if(band_h>inset*0.8f){
        /* the two mouldings part here, with a real gap between them */
        panel_gap(c,edge,band_y-inset*0.30f,(float)W-2*edge,
                  fmaxf(2.0f,1.7f*(float)H/268.0f));
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
          /* the road mark fills the empty right-hand end of the label,
           * scaled to the badge and vertically centred in it */
          { float availw=pbw*0.34f, availh=pbh*0.80f;
            float sc2=fminf(availw/(float)DXM_ROAD_W, availh/(float)DXM_ROAD_HT);
            int rw=(int)(DXM_ROAD_W*sc2), rh=(int)(DXM_ROAD_HT*sc2);
            int rx=(int)(pbx+pbw-pbw*0.07f-rw), ry=(int)(pby+(pbh-rh)*0.5f);
            for(int y2=0;y2<rh;y2++)
              for(int x2=0;x2<rw;x2++){
                int sxp=(int)(x2/sc2), syp=(int)(y2/sc2);
                if(sxp>=DXM_ROAD_W||syp>=DXM_ROAD_HT) continue;
                const uint8_t *sp=dxm_road+((size_t)syp*DXM_ROAD_W+sxp)*4;
                float al=sp[3]/255.0f;
                if(al<=0.01f) continue;
                px_blend(c,rx+x2,ry+y2,sp[0],sp[1],sp[2],al);
              }
          }
          for(int k=0;k<4;k++)
            rect(c,pbx+pbw*0.10f,pby+pbh*(0.78f+k*0.048f),pbw*0.52f,
                 fmaxf(1.0f,pbh*0.030f),cols[k][0],cols[k][1],cols[k][2]); }
        g_grain=1;

        /* power button + status LEDs */
        float px0=pbx+pbw+inset*0.85f;
        float pw=16.0f*mm;                       /* a 16mm power cap */
        /* painted, and centred over the cap */
        { const char *pl="POWER";
          float tw3=(float)strlen(pl)*8.0f*g_lbl;
          text(c,px0+(pw-tw3)*0.5f, mid-pw*0.39f-g_lbl*11, pl, g_lbl, 86,82,74); }
        /* thin cut around the button, cap nearly filling it */
        rrect(c,px0,mid-pw*0.39f,pw,pw*0.78f,pw*0.10f,64,61,56,0.80f,0.92f);
        /* the cap is moulded in the same darker brown as the drive, not in
         * the case colour - the same multipliers floppy_drive() uses */
        rrect(c,px0+pw*0.045f,mid-pw*0.39f+pw*0.04f,pw*0.91f,pw*0.70f,pw*0.08f,
              (int)(PLASTIC_R*0.74f),(int)(PLASTIC_G*0.72f),
              (int)(PLASTIC_B*0.76f),1.16f,0.84f);
        bevel(c,px0+pw*0.045f,mid-pw*0.39f+pw*0.04f,pw*0.91f,pw*0.70f,
              fmaxf(1.0f,pw*0.06f),1);
        /* power LED: a small round lens under the button */
        { float lr=1.35f*mm;
          float lcx=px0+pw*0.5f;
          float lcy=mid-pw*0.39f+pw*0.78f+fmaxf(4.0f,band_h*0.09f);
          led(c,lcx,lcy,lr, 26,44,26);                        /* UNLIT */
          g_pwr_led[0]=lcx-lr; g_pwr_led[1]=lcy-lr;
          g_pwr_led[2]=lr*2.0f; g_pwr_led[3]=lr*2.0f; }

        /* floppy drive: a real 3.5" face is 101.6 x 25.4 mm, centred
         * vertically between the divider ridge and the case bottom */
        float fh=25.4f*mm, fw2=101.6f*mm;
        float fx=(float)W-edge-inset*0.65f-fw2;
        float fmid=((band_y-inset*0.26f)+(float)H)*0.5f;
        floppy_drive(c,fx,fmid-fh*0.5f,fw2,fh);
    }

    /* ---- moulded marks and manufacturing traces -------------------------
     * Text pressed INTO the tool, not printed on the part, plus the traces
     * every injection moulding carries: the parting line where the two tool
     * halves met, and the ejector-pin circles that pushed the part out. */
    { float mmu=(float)H/268.0f;                    /* same mm as the band */
      float ms=fmaxf(1.0f,g_lbl*0.72f);
      /* the compliance block, low and to the left, where nobody looks */
      moulded_text(c,edge+inset*0.9f, (float)H-inset*0.95f+9.0f*ms,
                   "MADE IN PORTUGAL",ms,0);

      /* ejector pin marks: faint discs on the broad flat areas */
      { float pr2=3.6f*mmu;
        float pts[6][2]={{0.20f,0.16f},{0.20f,0.86f},{0.80f,0.16f},
                         {0.80f,0.86f},{0.50f,0.07f},{0.34f,0.93f}};
        for(int k=0;k<6;k++){
          float ex2=edge+(W-2*edge)*pts[k][0], ey2=H*pts[k][1];
          for(int j2=(int)(ey2-pr2-1);j2<=(int)(ey2+pr2+1);j2++)
            for(int i2=(int)(ex2-pr2-1);i2<=(int)(ex2+pr2+1);i2++){
              float dx2=i2-ex2, dy2=j2-ey2, dd=sqrtf(dx2*dx2+dy2*dy2);
              if(dd>pr2) continue;
              /* very slightly proud, so it catches the light at its rim */
              float rim=1.0f-fabsf(dd-pr2*0.86f)/(pr2*0.16f);
              if(rim<0.0f) rim=0.0f;
              px_shade(c,i2,j2,1.0f-0.018f+0.030f*rim*(-dy2/pr2),0.0f);
            }
        }
      }
    }

    /* ---- wear ------------------------------------------------------
     * Thirty years of being touched.  Scratches on plastic SCATTER light,
     * so they read brighter than the surface around them, and they are not
     * scattered evenly: they gather along the front edges, around the
     * drive slot and on the buttons - wherever hands and diskettes went.
     * Uniform scratching over the whole case looks like film grain; it is
     * the CLUSTERING that tells the story. */
    { float mmu=(float)H/268.0f;
      /* places a hand actually goes, in normalised case coordinates */
      float hot[4][3]={
        {0.50f, 0.86f, 1.00f},      /* the front band, most handled     */
        {0.86f, 0.86f, 0.85f},      /* around the floppy               */
        {0.22f, 0.86f, 0.55f},      /* badge / power end               */
        {0.50f, 0.06f, 0.40f},      /* the top edge, where it is lifted */
      };
      for(int n=0;n<230;n++){
        /* pick a hotspot, then jitter around it */
        float pick=hash2(n,17,31);
        int   hs = (pick<0.42f)?0 : (pick<0.68f)?1 : (pick<0.88f)?2 : 3;
        float sx2=(hash2(n,3,7)-0.5f), sy2=(hash2(n,5,11)-0.5f);
        float spread=0.16f+0.22f*hash2(n,9,13);
        float cx2=(hot[hs][0]+sx2*spread)*(float)W;
        float cy2=(hot[hs][1]+sy2*spread*0.55f)*(float)H;
        if(cx2<0||cy2<0||cx2>=W||cy2>=H) continue;
        /* mostly shallow near-horizontal drags, a few random nicks */
        float ang=(hash2(n,21,23)<0.72f)
                    ? (hash2(n,15,19)-0.5f)*0.55f
                    : (hash2(n,15,19)*6.28318f);
        float len=(1.5f+22.0f*hash2(n,27,29)*hash2(n,28,37))*mmu;
        /* Scratches are SPECULAR - they scatter light back at you - so
         * they have to be added, not just multiplied.  A multiplier alone
         * gets scaled down by the ambient term in the shader until it
         * disappears, which is why the first pass was invisible. */
        float amp=hot[hs][2]*(0.10f+0.22f*hash2(n,33,41));
        float ca=cosf(ang), sa=sinf(ang);
        int steps=(int)fmaxf(2.0f,len);
        for(int t2=0;t2<steps;t2++){
            float f=(float)t2/steps;
            /* fade the ends so nothing starts or stops abruptly */
            float e=sinf(f*3.14159f);
            int i2=(int)(cx2+(f-0.5f)*len*ca);
            int j2=(int)(cy2+(f-0.5f)*len*sa);
            /* a scratch is a bright scatter with a faint dark edge */
            px_shade(c,i2,j2,1.0f+amp*e*0.50f,amp*e*0.13f);
            px_shade(c,i2,j2+1,1.0f-amp*e*0.20f,0.0f);
        }
      }
      /* scuffs: broad patches where the sheen has been rubbed dull */
      for(int j=0;j<H;j++)
        for(int i=0;i<W;i++){
            float s2=vnoise((float)i*0.0018f,(float)j*0.0021f,23);
            float bias=((float)j/(float)H);          /* worse low down */
            float scuff=fmaxf(0.0f,s2-0.58f)*bias*bias*0.20f;
            if(scuff>0.0005f) px_shade(c,i,j,1.0f-scuff,0.0f);
        }
    }

    /* ---- finishing pass: what makes it a photographed object ------------
     * Everything above draws PARTS.  This pass treats the finished case as
     * one object: a light with a POSITION (so illumination falls across the
     * panel instead of being uniform), a grazing-angle sheen, and thirty
     * years of uneven yellowing with dust settled into the recesses.  A
     * purely directional light lights every point on a flat face equally,
     * which is exactly what reads as a cartoon cutout. */
    { float lx=(float)W*0.16f, ly=-(float)H*0.40f;   /* key, above and left */
      float inv=1.0f/(float)(W>H?W:H);
      for(int j=0;j<H;j++){
        for(int i=0;i<W;i++){
            uint8_t *p=C.px+((size_t)j*W+i)*4;
            float dx=((float)i-lx)*inv, dy=((float)j-ly)*inv;
            float d2=dx*dx+dy*dy;
            /* inverse-square-ish falloff across the whole panel */
            float key=1.0f/(1.0f+1.35f*d2);
            /* broad specular lobe - plastic is satin, not matte */
            float sheen=expf(-d2*2.6f)*0.05f;
            /* grazing sheen: the case brightens toward its outer edges */
            float ex=fabsf(((float)i/(float)W)-0.5f)*2.0f;
            float ey=fabsf(((float)j/(float)H)-0.5f)*2.0f;
            float graze=powf(fmaxf(ex,ey),3.5f)*0.07f;
            /* uneven yellowing: large, slow, and never uniform */
            float age=vnoise((float)i*0.0032f,(float)j*0.0032f,11);
            float age2=vnoise((float)i*0.0009f,(float)j*0.0011f,12);
            float yellow=(0.45f*age+0.55f*age2);
            /* dust: settles low and in the corners */
            float low=(float)j/(float)H;
            float dust=vnoise((float)i*0.006f,(float)j*0.006f,13)*low*low*0.05f;

            float m=0.72f+0.26f*key;
            for(int k=0;k<3;k++){
                float v=p[k]*m + (sheen+graze)*255.0f;
                /* warm the reds, hold the greens, pull the blues down */
                float tint = (k==0)? 1.0f+0.055f*yellow
                           : (k==1)? 1.0f+0.022f*yellow
                                   : 1.0f-0.075f*yellow;
                v*=tint;
                v*=1.0f-dust;
                p[k]=(uint8_t)(v<0?0:v>255?255:v);
            }
        }
      }
    }

    /* Encode how strongly each pixel FACES THE TUBE into the alpha channel,
     * which is otherwise unused.  The reveal dish is angled toward the glass
     * and so catches far more of the screen's light than the flat front of
     * the case; the shader has no other way to tell them apart. */
    { float rin2=L->tube_h*BEZEL_R_IN, rmid2=L->tube_h*BEZEL_R_MID;
      float reach=bz*5.0f;                 /* how far the falloff carries */
      for(int j=0;j<H;j++)
        for(int i=0;i<W;i++){
            uint8_t *p=C.px+((size_t)j*W+i)*4;
            /* EXACTLY the test bezel() uses to cut the dish: inside the
             * shoulder and outside the aperture.  A uniform offset from the
             * aperture is a different curve - the shoulder has its own
             * radius and only part of the barrel - so the mask drifted off
             * the moulding at the corners. */
            float din =aperture_sd(L,(float)i,(float)j,rin2);
            float dout=shoulder_sd(L,(float)i,(float)j,rmid2,bz);
            float f;
            if(din<=0.0f)        f=0.0f;                 /* glass          */
            else if(dout<0.0f){
                /* Across the dish itself the light FADES OUTWARD: the wall
                 * is brightest where it meets the glass and turns away as it
                 * climbs to the shoulder.  A flat mask lit the whole dish
                 * evenly, which read as far too hot. */
                float t = din/fmaxf(din-dout,1e-3f);     /* 0 glass, 1 edge */
                float e = 1.0f-t;
                f = 0.30f + 0.70f*e*e;
            }
            else {
                float t=dout/reach;                      /* onto the face  */
                f = (t>=1.0f) ? 0.0f : (1.0f-t)*(1.0f-t)*0.30f;
            }
            p[3]=(uint8_t)(f*255.0f);
        }
    }
    for(int k=0;k<4;k++){ L->fdd_led[k]=g_fdd_led[k]; L->pwr_led[k]=g_pwr_led[k]; }
    return C.px;
}
