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
#define PLASTIC_R 0xC9
#define PLASTIC_G 0xC2
#define PLASTIC_B 0xAC
#define BEZEL_INSET 0.055f      /* of tube height */
#define TUBE_H_FRAC 0.66f

#define BEZEL_BAND    0.056f   /* dished part, next to the glass          */
#define BEZEL_HOUSING 0.095f   /* full surround depth beyond the picture   */
#define BEZEL_R_MID   0.022f   /* where the dished band meets the flat     */
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
/* Moulded ABS is not flat: fine grain, a coarser blotch from the mould, and
 * faint flow lines.  Without this the case reads as vector art. */
static float plastic_tex(int x,int y){
    float fine   = hash2(x,y,1)-0.5f;
    float coarse = hash2(x>>3,y>>3,2)-0.5f;
    float blotch = hash2(x>>6,y>>6,3)-0.5f;
    float flow   = sinf((float)y*0.35f + hash2(x>>7,0,4)*6.28f)*0.5f;
    return fine*0.052f + coarse*0.030f + blotch*0.018f + flow*0.006f;
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
    /* sits in a moulded well, so it gets a dark ring and a hot centre */
    for(int j=(int)(cy-rad*2.2f);j<=(int)(cy+rad*2.2f);j++)
        for(int i=(int)(cx-rad*2.2f);i<=(int)(cx+rad*2.2f);i++){
            float dx=i-cx,dy=j-cy,d=sqrtf(dx*dx+dy*dy);
            if(d<rad*1.65f && d>=rad*0.95f) px_blend(c,i,j,40,38,34,0.55f);
        }
    for(int j=(int)(cy-rad-2);j<=(int)(cy+rad+2);j++)
        for(int i=(int)(cx-rad-2);i<=(int)(cx+rad+2);i++){
            float dx=i-cx,dy=j-cy,d=sqrtf(dx*dx+dy*dy);
            if(d<=rad)          px_blend(c,i,j,r,g,b,1.0f);
            else if(d<rad+2.0f) px_blend(c,i,j,r,g,b,0.28f*(rad+2.0f-d));
        }
    px_blend(c,(int)(cx-rad*0.3f),(int)(cy-rad*0.3f),255,255,255,0.55f);
}

/* ---- moulded modules ---- */

/* A grille is a recessed WELL with a moulded lip, and slots inside it with
 * real cross-section: dark trough, lit lower lip, shadowed upper lip. */
static void housing_edge(canvas *c,float x,float y,float w,float h,float r,
                         float ew,float shadow,int raised);
static void grille_panel(canvas *c,float x,float y,float w,float h,float pitch){
    rrect(c,x,y,w,h,h*0.045f,PLASTIC_R,PLASTIC_G,PLASTIC_B,0.90f,0.96f);
    housing_edge(c,x,y,w,h,h*0.045f,fmaxf(2.0f,h*0.055f),fmaxf(2.0f,h*0.05f),0);
    float ix=x+w*0.045f, iy=y+h*0.075f, iw=w*0.91f, ih=h*0.85f;
    if(pitch<3.0f) pitch=3.0f;
    float slot=fmaxf(1.5f,pitch*0.46f);
    for(float sy=iy; sy<iy+ih-slot; sy+=pitch){
        for(int t=0;t<(int)slot;t++){
            float f=(float)t/slot;
            int v=(int)(26+30*f);                    /* trough lightens downward */
            for(int i=(int)ix;i<(int)(ix+iw);i++){
                float e=1.0f;                        /* round the slot ends */
                float dl=(i-ix)/slot, dr=((ix+iw)-i)/slot;
                if(dl<1.0f) e=dl; if(dr<1.0f) e=fminf(e,dr);
                if(e<=0) continue;
                px_blend(c,i,(int)sy+t,v,v-1,v-3,0.88f*e);
            }
        }
        for(int i=(int)ix;i<(int)(ix+iw);i++){
            px_blend(c,i,(int)sy-1,20,19,17,0.42f);          /* upper shadow */
            px_blend(c,i,(int)(sy+slot),255,252,244,0.30f);  /* lower lit lip */
        }
    }
}
/* thin vent slots straight into flat plastic (case breathing holes) */
static void vents(canvas *c,float x,float y,float w,float h,int n,int vertical){
    if(n<1) n=1;
    float step=(vertical?w:h)/n, slot=fmaxf(1.2f,step*0.40f);
    for(int k=0;k<n;k++){
        float o=(vertical?x:y)+k*step+step*0.30f;
        for(int t=0;t<(int)slot;t++){
            float f=(float)t/slot; int v=(int)(34+26*f);
            if(vertical) for(int j=(int)y;j<(int)(y+h);j++) px_blend(c,(int)o+t,j,v,v-1,v-3,0.80f);
            else        for(int i=(int)x;i<(int)(x+w);i++) px_blend(c,i,(int)o+t,v,v-1,v-3,0.80f);
        }
        if(vertical) for(int j=(int)y;j<(int)(y+h);j++) px_blend(c,(int)(o+slot),j,255,252,244,0.26f);
        else        for(int i=(int)x;i<(int)(x+w);i++) px_blend(c,i,(int)(o+slot),255,252,244,0.26f);
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
static void thumbwheel(canvas *c,float x,float y,float w,float h){
    rrect(c,x,y,w,h,h*0.22f,84,81,74,0.55f,0.62f);
    bevel(c,x,y,w,h,fmaxf(1.0f,h*0.14f),0);
    rrect(c,x+w*0.08f,y+h*0.13f,w*0.84f,h*0.74f,h*0.18f,
          PLASTIC_R,PLASTIC_G,PLASTIC_B,1.08f,0.78f);
    for(float i=x+w*0.16f;i<x+w*0.86f;i+=fmaxf(2.0f,w*0.11f))
        for(int j=(int)(y+h*0.20f);j<(int)(y+h*0.80f);j++){
            px_blend(c,(int)i,j,58,55,50,0.48f);
            px_blend(c,(int)i+1,j,255,252,244,0.20f);
        }
}
/* 3.5" drive: face plate, slotted door with a steel shutter lip, sprung
 * eject button in its own recess, activity LED in a moulded well. */
static void floppy_drive(canvas *c,float x,float y,float w,float h){
    rrect(c,x,y,w,h,h*0.09f,PLASTIC_R,PLASTIC_G,PLASTIC_B,0.99f,1.04f);
    housing_edge(c,x,y,w,h,h*0.09f,fmaxf(2.0f,h*0.10f),fmaxf(2.0f,h*0.09f),0);
    seam(c,x+w*0.02f,y+h*0.06f,w*0.96f,0,fmaxf(1.0f,h*0.02f));
    /* the disk slot: recess, then the drive door behind it */
    float sx=x+w*0.055f, sy=y+h*0.30f, sw=w*0.63f, sh=h*0.19f;
    rrect(c,sx-w*0.012f,sy-h*0.05f,sw+w*0.024f,sh+h*0.10f,sh*0.30f,72,69,63,0.80f,0.92f);
    rrect(c,sx,sy,sw,sh,sh*0.25f,26,25,23,1.0f,1.0f);
    for(int i=(int)sx;i<(int)(sx+sw);i++){
        px_blend(c,i,(int)(sy+sh),255,252,244,0.34f);      /* lit lower lip  */
        px_blend(c,i,(int)sy-1,14,13,12,0.55f);            /* shadow above   */
    }
    /* steel shutter glint just inside the slot */
    for(int i=(int)(sx+sw*0.06f);i<(int)(sx+sw*0.94f);i++)
        px_blend(c,i,(int)(sy+sh*0.30f),190,193,198,0.30f);
    /* eject button, recessed */
    float ex=x+w*0.80f, ey=y+h*0.52f, ew=w*0.14f, eh=h*0.24f;
    rrect(c,ex-ew*0.12f,ey-eh*0.14f,ew*1.24f,eh*1.28f,eh*0.28f,84,81,74,0.62f,0.70f);
    rrect(c,ex,ey,ew,eh,eh*0.24f,PLASTIC_R,PLASTIC_G,PLASTIC_B,1.14f,0.86f);
    bevel(c,ex,ey,ew,eh,fmaxf(1.0f,eh*0.16f),1);
    led(c,x+w*0.085f,y+h*0.72f,fmaxf(1.5f,h*0.055f),70,235,95);
}
/* CD-ROM: taller face, tray seam with a finger recess, its own volume and
 * headphone jack — which is exactly what these drives had. */
static void cd_drive(canvas *c,float x,float y,float w,float h,float lbl){
    rrect(c,x,y,w,h,h*0.07f,PLASTIC_R,PLASTIC_G,PLASTIC_B,0.99f,1.04f);
    housing_edge(c,x,y,w,h,h*0.07f,fmaxf(2.0f,h*0.075f),fmaxf(2.0f,h*0.07f),0);
    float tx=x+w*0.045f, ty=y+h*0.14f, tw=w*0.91f, th=h*0.34f;
    rrect(c,tx,ty,tw,th,th*0.12f,PLASTIC_R,PLASTIC_G,PLASTIC_B,0.92f,1.00f);
    bevel(c,tx,ty,tw,th,fmaxf(1.0f,th*0.10f),0);
    for(int i=(int)tx;i<(int)(tx+tw);i++){
        px_blend(c,i,(int)(ty+th*0.62f),30,29,27,0.72f);     /* tray split   */
        px_blend(c,i,(int)(ty+th*0.62f)+1,255,252,244,0.30f);
    }
    rrect(c,tx+tw*0.36f,ty+th*0.66f,tw*0.28f,th*0.26f,th*0.10f,70,67,61,0.7f,0.85f);
    float ex=x+w*0.86f, ey=y+h*0.62f, ew=w*0.10f, eh=h*0.18f;
    rrect(c,ex,ey,ew,eh,eh*0.26f,PLASTIC_R,PLASTIC_G,PLASTIC_B,1.14f,0.86f);
    bevel(c,ex,ey,ew,eh,fmaxf(1.0f,eh*0.18f),1);
    led(c,x+w*0.07f,y+h*0.74f,fmaxf(1.5f,h*0.045f),255,180,45);
    slider(c,x+w*0.18f,y+h*0.66f,w*0.26f,h*0.16f,0.55f);
    rrect(c,x+w*0.52f,y+h*0.64f,h*0.20f,h*0.20f,h*0.10f,64,61,56,0.7f,0.8f);
    led(c,x+w*0.52f+h*0.10f,y+h*0.74f,h*0.055f,18,17,16);
    (void)lbl;
}

dxm_layout chassis_layout(int W,int H){
    dxm_layout L; float aspect=(float)W/(float)H;
    L.variant = aspect<1.45f?LAY_COMPACT : (aspect<1.85f?LAY_STANDARD:LAY_STEREO);
    L.cx=0; L.cy=0; L.cw=(float)W; L.ch=(float)H;
    float edge=W*0.016f, inset=H*0.052f;
    /* the monitor housing is chunky: it wraps the picture by BEZEL_HOUSING
     * on every side, and the layout has to budget for it */
    float th=((float)H-inset)*0.68f, tw=th*4.0f/3.0f;
    float hous=th*BEZEL_HOUSING;
    float avail=W-2*edge-tw-2*hous-inset*0.5f, min_bay=tw*0.24f;
    if(avail<min_bay){
        float k=(W-2*edge-min_bay-inset*0.5f)/(tw+2*th*BEZEL_HOUSING*4.0f/3.0f);
        if(k<0.35f) k=0.35f;
        tw*=k; th*=k; hous=th*BEZEL_HOUSING;
        avail=W-2*edge-tw-2*hous-inset*0.5f;
    }
    float spk=(L.variant==LAY_STEREO)?avail*0.16f:0.0f;
    if(spk*2>avail-min_bay) spk=(avail-min_bay)*0.5f;
    if(spk<0) spk=0;
    L.tube_w=tw; L.tube_h=th;
    L.tube_x=edge+hous+inset*0.20f+spk;
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

/* One light, from above and slightly left, as in the reference photo.  y runs
 * DOWN in canvas space, so "up" is negative y. */
#define LIGHT_X (-0.42f)
#define LIGHT_Y (-0.91f)

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
/* signed distance to the aperture, in pixels; <=0 is glass */
static float aperture_sd(const dxm_layout *L,float px,float py,float rin){
    return warped_rr_sd(L,px,py,rin,0.0f);
}

/* The housing's outer edge: a rolled lip that catches a hard specular line
 * along the top and upper-left, falls into shadow along the bottom, and casts
 * a soft contact shadow onto the flat case beneath it.  This is what gives
 * the monitor its depth against the rest of the machine. */
static void housing_edge(canvas *c,float x,float y,float w,float h,float r,
                         float ew,float shadow,int raised){
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
            float mul=1.0f+lam*prof*0.50f;
            float sp=fmaxf(lam,0.0f);
            /* the shine: a hard, narrow catch along the top of the roll */
            px_shade(c,i,j,mul,powf(sp,10.0f)*prof*0.52f);
        } else {
            /* contact shadow cast onto the case, opposite the light */
            float t=sd/shadow;
            float occl=fmaxf(raised?-lam:lam,0.0f);
            px_shade(c,i,j,1.0f-occl*(1.0f-t)*(1.0f-t)*0.46f,0.0f);
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
            /* Measured off the reference: its moulding is essentially the
             * SAME tone on all four sides (~160-175 of 255), with the depth
             * carried almost entirely by a dark contact band at the glass.
             * A strong directional term makes the top read as a recess and
             * the other three sides read wrong, because it darkens one side
             * while brightening the opposite one. Keep it subtle. */
            sh = 1.00f + lam*0.07f;
            float k=1.0f-t;                   /* 1 at the glass, 0 at shoulder */
            sh *= 1.0f - 0.52f*k*k*(0.55f+0.45f*k);
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
    float inset=H*0.052f, edge=W*0.016f;
    float bz=L->tube_h*BEZEL_BAND;      /* measured off the reference */
    float hous=L->tube_h*BEZEL_HOUSING;

    rrect(c,0,0,(float)W,(float)H,0.0f,PLASTIC_R,PLASTIC_G,PLASTIC_B,1.03f,0.90f);

    /* ribbed edge strips */
    vents(c,edge*0.20f,inset*0.5f,edge*0.60f,H-inset,(int)((H-inset)/(H*0.017f)),1);
    vents(c,W-edge*0.80f,inset*0.5f,edge*0.60f,H-inset,(int)((H-inset)/(H*0.017f)),1);
    seam(c,edge,0,(float)H,1,fmaxf(1.0f,W*0.0012f));
    seam(c,W-edge,0,(float)H,1,fmaxf(1.0f,W*0.0012f));

    /* monitor housing -> bezel band -> aperture -> glass */
    float rin=L->tube_h*BEZEL_R_IN, rout=L->tube_h*BEZEL_R_OUT;
    float sx=L->tube_x-hous, sy=L->tube_y-hous;
    float sw=L->tube_w+hous*2.0f, sh2=L->tube_h+hous*2.0f;
    rrect(c,sx,sy,sw,sh2,rout,PLASTIC_R,PLASTIC_G,PLASTIC_B,1.00f,0.96f);
    /* The shine belongs on the REVEAL SHOULDER, not out here where the
     * moulding meets the flat chassis - that boundary is nearly flush and
     * should barely register.  bezel() lights the shoulder itself. */
    bezel(c,L,bz,rin,L->tube_h*BEZEL_R_MID);
    for(int j=(int)(L->tube_y-hous);j<(int)(L->tube_y+L->tube_h+hous);j++)
      for(int i=(int)(L->tube_x-hous);i<(int)(L->tube_x+L->tube_w+hous);i++)
        if(aperture_sd(L,(float)i,(float)j,rin)<=0.0f) px_set(c,i,j,5,6,5);

    if(L->variant==LAY_STEREO){
        float sw2=L->tube_x-edge-inset*0.55f;
        if(sw2>inset*0.8f){
            grille_panel(c,edge+inset*0.30f,L->tube_y,sw2*0.74f,L->tube_h,H*0.021f);
            grille_panel(c,L->tube_x+L->tube_w+hous+inset*0.20f,L->tube_y,
                         sw2*0.74f,L->tube_h,H*0.021f);
        }
    }

    /* ---- right bay ---- */
    float bayx=L->tube_x+L->tube_w+hous+inset*0.35f;
    if(L->variant==LAY_STEREO) bayx+=(L->tube_x-edge-inset*0.55f)*0.78f;
    float bayw=W-edge-inset*0.70f-bayx;
    if(bayw<inset*2.5f) bayw=inset*2.5f;
    seam(c,bayx-inset*0.40f,inset*0.35f,H-inset*0.70f,1,fmaxf(1.0f,W*0.0010f));

    float pbh=fminf(L->tube_h*0.150f,bayw*0.30f);
    rrect(c,bayx,inset*0.75f,bayw,pbh,pbh*0.10f,0x22,0x26,0x30,1.0f,0.82f);
    housing_edge(c,bayx,inset*0.75f,bayw,pbh,pbh*0.10f,
                 fmaxf(2.0f,pbh*0.09f),fmaxf(2.0f,pbh*0.08f),0);
    { float s=fminf(fmaxf(1.0f,bayw/170.0f),pbh/30.0f);
      text(c,bayx+bayw*0.08f,inset*0.75f+pbh*0.20f,"PC-486",s*1.45f,0xEC,0xE8,0xDC);
      text(c,bayx+bayw*0.08f,inset*0.75f+pbh*0.60f,"MULTIMEDIA SYSTEM",s*0.62f,0xA8,0xB0,0xC6);
      int cols[4][3]={{0x2E,0x4C,0xA8},{0x2E,0x8C,0x50},{0xC8,0x9A,0x28},{0xB8,0x3C,0x34}};
      for(int k=0;k<4;k++)
        rect(c,bayx+bayw*0.08f,inset*0.75f+pbh*(0.80f+k*0.042f),bayw*0.55f,
             fmaxf(1.0f,pbh*0.026f),cols[k][0],cols[k][1],cols[k][2]); }

    vents(c,bayx+bayw*0.04f,inset*0.75f+pbh+inset*0.45f,bayw*0.92f,L->tube_h*0.075f,6,0);

    float py=inset*0.75f+pbh+L->tube_h*0.20f;
    float pw=fminf(bayw*0.34f,L->tube_h*0.125f);
    text(c,bayx,py-g_lbl*11,"POWER",g_lbl,86,82,74);
    rrect(c,bayx,py,pw,pw*0.78f,pw*0.10f,88,85,78,0.62f,0.70f);
    bevel(c,bayx,py,pw,pw*0.78f,fmaxf(1.5f,pw*0.08f),0);
    rrect(c,bayx+pw*0.10f,py+pw*0.08f,pw*0.80f,pw*0.62f,pw*0.08f,
          PLASTIC_R,PLASTIC_G,PLASTIC_B,1.16f,0.84f);
    bevel(c,bayx+pw*0.10f,py+pw*0.08f,pw*0.80f,pw*0.62f,fmaxf(1.0f,pw*0.07f),1);

    float ly=py+pw*0.78f+L->tube_h*0.055f;
    float lstep=fmaxf(L->tube_h*0.050f,13.0f*g_lbl);
    const char *lab[3]={"POWER","H.D.D.","FLOPPY"};
    int lc[3][3]={{60,255,90},{255,175,40},{60,255,90}};
    for(int k=0;k<3;k++){
        text(c,bayx,ly+k*lstep,lab[k],g_lbl,86,82,74);
        led(c,bayx+fminf(bayw*0.62f,L->tube_h*0.26f),ly+k*lstep+4*g_lbl,
            fmaxf(2.0f,inset*0.070f),lc[k][0],lc[k][1],lc[k][2]);
    }
    float fy=ly+3*lstep+L->tube_h*0.055f;
    float fw=fminf(bayw*0.96f,L->tube_h*0.66f), fh=fw*0.21f;
    floppy_drive(c,bayx,fy,fw,fh);
    float cy0=fy+fh+L->tube_h*0.040f, chh=fw*0.30f;
    cd_drive(c,bayx,cy0,fw,chh,g_lbl);
    text(c,bayx,cy0+chh+5*g_lbl,"CD-ROM",g_lbl,92,88,80);
    { float vy=cy0+chh+L->tube_h*0.090f, vh=((float)H-inset*0.70f)-vy;
      if(vh>inset*0.5f) vents(c,bayx,vy,bayw*0.92f,vh,(int)fmaxf(4.0f,vh/(H*0.018f)),0); }

    /* ---- bottom band ---- */
    float band_y=L->tube_y+L->tube_h+hous+inset*0.20f;
    float band_h=H-inset*0.55f-band_y;
    if(band_h>inset*0.6f){
        seam(c,edge,band_y-inset*0.22f,bayx-inset*0.40f-edge,0,fmaxf(1.0f,H*0.0012f));
        if(L->variant!=LAY_STEREO){
            float gw=(bayx-edge)*0.26f;
            grille_panel(c,edge+inset*0.55f,band_y+band_h*0.10f,gw,band_h*0.72f,H*0.017f);
            grille_panel(c,bayx-inset*0.85f-gw,band_y+band_h*0.10f,gw,band_h*0.72f,H*0.017f);
        }
        float cxm=(edge+bayx)*0.5f;
        text(c,cxm-g_lbl*8*6.0f,band_y+band_h*0.12f,"STEREO SOUND",g_lbl,96,92,84);
        rect(c,cxm-g_lbl*8*6.0f,band_y+band_h*0.12f+g_lbl*11,g_lbl*8*7.0f,
             fmaxf(1.0f,g_lbl*1.6f),0x2E,0x4C,0xA8);
        slider(c,cxm-g_lbl*8*5.0f,band_y+band_h*0.42f,g_lbl*8*10.0f,band_h*0.30f,0.62f);
        text(c,cxm-g_lbl*8*2.0f,band_y+band_h*0.80f,"VOLUME",g_lbl,96,92,84);
        float jx=cxm+g_lbl*8*8.0f;
        rrect(c,jx,band_y+band_h*0.38f,band_h*0.26f,band_h*0.26f,band_h*0.13f,64,61,56,0.7f,0.8f);
        bevel(c,jx,band_y+band_h*0.38f,band_h*0.26f,band_h*0.26f,fmaxf(1.0f,band_h*0.03f),0);
        led(c,jx+band_h*0.13f,band_y+band_h*0.51f,band_h*0.070f,18,17,16);
        text(c,jx-g_lbl*8*1.0f,band_y+band_h*0.80f,"PHONES",g_lbl,96,92,84);
    }
    { float twx=L->tube_x+L->tube_w-inset*3.6f;
      float twy=L->tube_y+L->tube_h+hous*0.34f;
      float tww=inset*1.30f, twh=inset*0.44f;
      if(twy+twh<band_y){
        thumbwheel(c,twx,twy,tww,twh);
        thumbwheel(c,twx+tww*1.22f,twy,tww,twh);
        text(c,twx,twy+twh+3*g_lbl,"BRIGHT",g_lbl,100,96,88);
        text(c,twx+tww*1.22f,twy+twh+3*g_lbl,"CONTR",g_lbl,100,96,88);
      } }
    return C.px;
}
