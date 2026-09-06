/* canvas.c — the drawing primitives the machine is built from: pixels,
 * shading with the plastic's grain, distance fields, rounded boxes,
 * bevels, seams and the moulded lettering. */
#include "internal.h"
#include "font.h"
#include "crt.h"

float canvas_lbl = 1.0f;
int   canvas_grain = 1;

void px_set(canvas *c,int x,int y,int r,int g,int b){
    if(x<0||y<0||x>=c->w||y>=c->h) return;
    uint8_t *p=c->px+((size_t)y*c->w+x)*4;
    p[0]=(uint8_t)(r<0?0:r>255?255:r); p[1]=(uint8_t)(g<0?0:g>255?255:g);
    p[2]=(uint8_t)(b<0?0:b>255?255:b); p[3]=255;
}

void px_blend(canvas *c,int x,int y,int r,int g,int b,float a){
    if(x<0||y<0||x>=c->w||y>=c->h||a<=0) return;
    if(a>1) a=1;
    /* CLAMP before the uint8_t cast.  Shading factors >1 (the lit dish wall)
     * pushed channels past 255, and the unclamped cast WRAPPED them - red
     * wrapped first (the plastic's largest channel), leaving teal/blue
     * speckles across the brightest parts of the bezel. */
    if(r>255)r=255;
    if(r<0)r=0;
    if(g>255)g=255;
    if(g<0)g=0;
    if(b>255)b=255;
    if(b<0)b=0;
    uint8_t *p=c->px+((size_t)y*c->w+x)*4;
    p[0]=(uint8_t)(p[0]*(1-a)+r*a); p[1]=(uint8_t)(p[1]*(1-a)+g*a);
    p[2]=(uint8_t)(p[2]*(1-a)+b*a); p[3]=255;
}

/* deterministic integer hash — identical on all three platforms (SPEC 6.7) */
/* The mixing constants are chosen to overflow 32 bits - that IS the mix - so
 * every product has to be taken in UNSIGNED arithmetic, where wrapping is
 * defined.  Multiplying the signed int and casting afterwards is signed
 * overflow, and GCC is entitled to assume that never happens: at -O2 it read
 * `x*374761393` in the scratch loop below as proof that x could never reach 6,
 * deleted that loop's `n<230` exit test as unreachable, and left the chassis
 * worker spinning forever behind a splash that never ended.  Clang does not
 * draw the same conclusion, which is why macOS never showed it.  The values
 * are unchanged - it is the same wrap, spelled legally. */
float hash2(int x,int y,int s){
    unsigned h=(unsigned)x*374761393u+(unsigned)y*668265263u+(unsigned)s*1442695041u;
    h=(h^(h>>13))*1274126177u; h^=h>>16;
    return (float)(h&1023)/1023.0f;
}

/* Smoothly interpolated value noise - the blocky nearest-neighbour hash it
 * replaces is what made the plastic look mushy. */
float vnoise(float x,float y,int seed){
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
float plastic_tex(int x,int y){
    if(!canvas_grain) return 0.0f;
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
float rr_sd(float px,float py,float cx,float cy,float hw,float hh,float r){
    float qx=fabsf(px-cx)-(hw-r), qy=fabsf(py-cy)-(hh-r);
    float ax=fmaxf(qx,0.0f), ay=fmaxf(qy,0.0f);
    return sqrtf(ax*ax+ay*ay)+fminf(fmaxf(qx,qy),0.0f)-r;
}

/* read-modify-write: scale a pixel and add a specular term */
void px_shade(canvas *c,int x,int y,float mul,float spec){
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
float warped_rr_sd(const dxm_layout *L,float px,float py,
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

void rrect(canvas *c,float x,float y,float w,float h,float rad,
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

void bevel(canvas *c,float x,float y,float w,float h,float t,int up){
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

void text(canvas *c,float x,float y,const char *s,float sc,int r,int g,int b){
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

/* The same lettering at ANY scale.  text() blows the 8x8 glyphs up by a
 * whole number, which is right for moulded type but leaves no size between
 * 1x and 2x.  This one box-filters the bitmap into each destination pixel
 * - sixteen samples a pixel - so a label can be 1.4x and stay even. */
void text_smooth(canvas *c,float x,float y,const char *s,float sc,int r,int g,int b){
    if(sc<0.5f) sc=0.5f;
    float gw=8.0f*sc;
    for(int n=0;s[n];n++){
        const uint8_t *gl=font_glyph((unsigned char)s[n]);
        float gx=x+n*gw;
        for(int j=0;j<(int)ceilf(gw);j++)
          for(int i=0;i<(int)ceilf(gw);i++){
            int on=0;
            for(int sy=0;sy<4;sy++)
              for(int sx=0;sx<4;sx++){
                float u=((float)i+(sx+0.5f)/4.0f)/sc, v=((float)j+(sy+0.5f)/4.0f)/sc;
                int ui=(int)u, vi=(int)v;
                if(ui>=0&&ui<8&&vi>=0&&vi<8 && (gl[vi]&(0x80>>ui))) on++;
              }
            if(!on) continue;
            float a=(float)on/16.0f;
            px_blend(c,(int)(gx+i),(int)(y+j)+1,255,255,255,0.13f*a);  /* the lit lower edge */
            px_blend(c,(int)(gx+i),(int)(y+j),r,g,b,a);
          }
    }
}

/* The parting between two mouldings.  It is a COVE, not a notch: the face
 * curves down into it and back out again with no edge anywhere, which is
 * what a moulded gap in a case actually looks like - the tool has a radius
 * and cannot leave a corner.  The earlier version cut a square channel and
 * put a hard dark row along its top lip, and that hard row is exactly what
 * read as a scratch rather than as a join.
 *
 * `d` is the FULL width of the cove.  Depth is a raised cosine across it,
 * so the shading falls straight out of the profile's slope: the flank you
 * meet first turns away from the key light, the far flank turns into it,
 * and the trough between sits in its own shade. */
void panel_gap(canvas *c,float x,float y,float w,float d){
    for(int j2=(int)y;j2<(int)(y+d);j2++){
        float t=(((float)j2+0.5f)-y)/d;
        if(t<0.0f||t>1.0f) continue;
        float prof=0.5f-0.5f*cosf(t*6.28318f);   /* 0 at the lips, 1 mid  */
        float g   =sinf(t*6.28318f);             /* +descending, -rising  */
        float lam =-g*0.91f;                     /* the key light is above */
        float sp  =fmaxf(lam,0.0f);
        for(int i2=(int)x;i2<(int)(x+w);i2++)
            px_shade(c,i2,j2,1.0f+lam*0.40f-prof*0.15f,sp*sp*0.085f);
    }
}

void seam(canvas *c,float x,float y,float len,int vertical,float w){
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
void chamfer_ring(canvas *c,float x,float y,float w,float h,float r,
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
void soft_hedge(canvas *c,float x0,float x1,float y,float span,
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
void soft_vedge(canvas *c,float y0,float y1,float x,float span,
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
void moulded_text(canvas *c,float x,float y,const char *s,float sc,
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

/* The housing's outer edge: a rolled lip that catches a hard specular line
 * along the top and upper-left, falls into shadow along the bottom, and casts
 * a soft contact shadow onto the flat case beneath it.  This is what gives
 * the monitor its depth against the rest of the machine. */
void housing_edge(canvas *c,float x,float y,float w,float h,float r,
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
