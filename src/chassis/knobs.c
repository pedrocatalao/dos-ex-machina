/* knobs.c — the two rotary controls, the one part of the case that moves:
 * drawn last over the finished plastic, and redrawn alone when turned. */
#include "internal.h"

/* The knobs: where they are, and the plastic under each one, kept so a
 * turn redraws only the knob and not the machine. */
static struct {
    float    place[2][3];          /* centre x, y and radius, output px */
    uint8_t *bg[2];                /* the square beneath, RGBA */
    int      bx[2], by[2], bs[2];
    uint8_t *patch;
} K;

/* Where a knob goes.  The knob itself is drawn by knobs_draw, last, so the
 * plastic saved under it is the finished case. */
void knobs_slot(int which,float cx,float cy,float r){
    K.place[which][0]=cx; K.place[which][1]=cy; K.place[which][2]=r;
}

/* A rotary control: a short cylinder of dark plastic standing off the
 * band, knurled round its edge so fingers can turn it, with a flat face
 * carrying a white index line.  The face is lit from above like everything
 * else, and the knurl is what makes it read as round - a plain disc would
 * be a button.  pos 0..1 runs the index through 270 degrees, from seven
 * o'clock round to five. */
static void rotary(canvas *c,float cx,float cy,float r,float pos){
    const float PI=3.14159265f;
    float ang=(-135.0f+270.0f*pos)*PI/180.0f;   /* 0 = twelve o'clock */
    /* Seen a little from above, the way the eject button is: the face is
     * an ellipse, squashed by the viewing angle, and above it shows a band
     * of the cylinder's side - the top of the knob, foreshortened - which
     * faces the key light and is the brightest thing here.  Below the
     * face the side is hidden and a contact shadow takes over.  It is the
     * same knob as before; only the camera has moved. */
    /* The three tones, as multipliers on the case colour.  Each part is
     * its own number so one can be tuned without moving the others. */
    const float TONE_DOME=1.42f;                    /* the face proper */
    const float TONE_RING=1.42f;                   /* the chamfer round it */
    const float TONE_GRIP=1.30f;                   /* the knurled side */
    const float SQ=0.98f;                          /* ellipse: vertical/horizontal */
    float ry=r*SQ;                                 /* face half-height */
    float hs=r*0.20f;                              /* visible side, foreshortened */
    float off=r*0.10f;                             /* face centre sits a touch low */
    int saved=canvas_grain; canvas_grain=0;
    int lim=(int)(r*2.3f+3.0f);
    /* The knob sits in a WELL, built exactly the way the power LED's is,
     * since that is what reads as recessed: a dark ring hugging the base,
     * a soft shadow on the plastic above the hole where the lip throws it,
     * and a lit chamfer lip on the plastic below, all of it reaching well
     * out into the case.  Drawn first; the knob body covers the middle. */
    { float bcy=cy+off-hs;                      /* the base, seen from above */
      float reach=r*1.45f;
      for(int j2=(int)(bcy-reach)-1;j2<=(int)(bcy+reach)+1;j2++)
        for(int i2=(int)(cx-reach)-1;i2<=(int)(cx+reach)+1;i2++){
          float dx=(float)i2+0.5f-cx, dy=(float)j2+0.5f-bcy;
          float d=sqrtf((dx/r)*(dx/r)+(dy/ry)*(dy/ry));   /* 1 at the base */
          if(d<=0.94f || d>1.42f) continue;
          float up=-dy/fmaxf(d*ry,1e-3f);       /* +1 straight above */
          if(d<=1.14f){                         /* the ring itself */
              float a=1.0f-fmaxf(0.0f,(d-1.05f)/0.09f);
              a*=fminf(1.0f,(d-0.92f)/0.06f);
              float top=(dy<0.0f)?1.0f:0.66f;
              px_blend(c,i2,j2,26,22,14,a*0.85f*top);
          } else {
              float t=(d-1.14f)/0.28f;          /* 0 at ring, 1 outside */
              if(up>0.15f)                      /* shadow above the hole */
                  px_shade(c,i2,j2,1.0f-0.20f*(1.0f-t)*up,0.0f);
              /* no lit lip below: the knob's own shadow falls there, and
               * a highlight under a shadow reads as two light sources */
          }
        } }
    for(int j2=(int)cy-lim;j2<=(int)cy+lim;j2++)
      for(int i2=(int)cx-lim;i2<=(int)cx+lim;i2++){
        float dx=(float)i2+0.5f-cx, dy=(float)j2+0.5f-(cy+off);
        float ux=dx/r; if(fabsf(ux)>2.4f) continue;      /* the shadow reaches this far */
        float yr=(fabsf(ux)<1.0f)?ry*sqrtf(1.0f-ux*ux):0.0f;   /* rim height here */
        float rho=sqrtf(ux*ux+(dy/ry)*(dy/ry));                  /* 1 on the face rim */
        int R,G,B; float spec=0.0f, base, cov, tone;

        if(rho>1.0f){
            /* The shadow the knob throws: a blurred copy of itself, dropped
             * a third of a radius, with a gaussian skirt - so it has no
             * outline of its own anywhere, and it fades in across the
             * knob's equator rather than starting there. */
            float sx=ux*1.25f, sy=(dy-r*0.35f)/ry;       /* narrower than tall */
            float d2=sqrtf(sx*sx+sy*sy)-1.0f;          /* radii outside it */
            if(d2<1.3f){
                float f=(d2<=0.0f)?1.0f:expf(-d2*d2*4.0f);
                float v=(dy/r+0.25f)/0.75f; if(v<0.0f)v=0.0f; if(v>1.0f)v=1.0f;
                f*=v*v*(3.0f-2.0f*v);                 /* eases in over the sides */
                if(f>0.003f) px_shade(c,i2,j2,1.0f-0.12f*f,0.0f);
            }
            if(dy>0.0f) continue;                      /* below: only the shadow */
        }
        if(rho>1.0f){
            /* above the face: the cylinder's side, if this pixel is within
             * its foreshortened height; lit by how much it faces up */
            float above=-dy-yr;                    /* distance above the rim */
            if(above>hs+1.0f || fabsf(ux)>=1.0f) continue;
            cov=fminf(1.0f,hs+1.0f-above);
            float up=sqrtf(1.0f-ux*ux);            /* the normal's upward part */
            float th=atan2f(ux,up);                /* angle round the axis */
            float ridge=sinf(th*28.0f);
            float lit=0.5f+0.5f*ridge*(-sinf(th+0.6f));
            base=(0.45f+0.28f*lit)*(0.66f+0.40f*up);
            /* darkest right behind the face, where the chamfer overhangs
             * the side - a crease, not a bright ledge - and falling off
             * again at the far edge into the panel */
            base*=0.76f+0.24f*fminf(1.0f,above/(hs*0.5f));
            base*=1.0f-0.28f*fmaxf(0.0f,(above-hs*0.55f)/(hs*0.45f));
            R=(int)(PLASTIC_R*TONE_GRIP*base); G=(int)(PLASTIC_G*(TONE_GRIP-0.06f)*base);
            B=(int)(PLASTIC_B*(TONE_GRIP+0.04f)*base);
            if(R>255)R=255;
            if(G>255)G=255;
            if(B>255)B=255;
            px_blend(c,i2,j2,R,G,B,cov);
            continue;
        }
        /* the face */
        cov=fminf(1.0f,(1.0f-rho)*ry+0.5f);
        float up=-dy/fmaxf(ry*rho,1e-3f);          /* +1 at the top of the rim */
        float th=atan2f(ux,-dy/ry);
        if(rho>0.78f){
            /* the edge of the face: a rounded chamfer, bright along the top
             * and shaded along the bottom, carrying a faint trace of the
             * knurl.  Not a dark ring - the knurl proper is the side, seen
             * above the face, and from here the face's edge is just the
             * moulding turning away. */
            float ridge=sinf(th*28.0f);
            float k=(rho-0.78f)/0.22f;                 /* 0 inner .. 1 rim */
            /* darker than the face it surrounds: this is the moulding
             * turning away from the viewer toward the grip */
            base=(0.54f+0.08f*up)*(1.0f-0.22f*k)+0.02f*ridge*k;
            spec=0.0f; tone=TONE_RING;
        } else {
            /* the dome: light runs off it toward the lower right */
            float nx=ux*0.75f, ny=(dy/ry)*0.75f;
            float lam=0.55f+0.45f*(-ny*0.85f-nx*0.35f);
            base=(0.40f*lam+0.18f)*(1.0f-0.10f*rho*rho);
            float hx=(ux+0.30f)/0.34f, hy=(dy/ry+0.36f)/0.34f;
            spec=expf(-(hx*hx+hy*hy))*0.16f;
            tone=TONE_DOME;
        }
        R=(int)(PLASTIC_R*tone*base); G=(int)(PLASTIC_G*(tone-0.06f)*base);
        B=(int)(PLASTIC_B*(tone+0.02f)*base);
        if(R>255)R=255;
        if(G>255)G=255;
        if(B>255)B=255;
        px_blend(c,i2,j2,R,G,B,cov);
        if(spec>0.0f) px_shade(c,i2,j2,1.0f,spec);
        /* the index: a painted mark across the dome, an off-white that
         * has seen thirty years of thumbs, sitting in a shallow groove so
         * it reads as filled rather than printed */
        { float px=sinf(ang), py=-cosf(ang);
          float ex=ux*r, ey=(dy/ry)*r;             /* un-squashed */
          float along=ex*px+ey*py, across=fabsf(ex*py-ey*px);
          float hw=r*0.075f+0.5f;
          if(along>r*0.20f && along<r*0.66f){
              if(across<hw){
                  float a=fminf(1.0f,(hw-across))*0.86f;
                  px_blend(c,i2,j2,214,208,192,a);
              } else if(across<hw+1.0f){
                  /* the groove's edge, a hair darker all round */
                  float e=1.0f-(across-hw);
                  px_shade(c,i2,j2,1.0f-0.12f*e,0.0f);
              } } }
      }
    canvas_grain=saved;
}

/* The monitor's own symbols, cut into the band under each knob: a sun for
 * brightness, a half-filled disc for contrast.  Distance fields, like the
 * power mark, so they stay crisp at any size. */
void knob_icons(canvas *c,float bx,float by,float cx2,float cy2,float s){
    int dw=(int)(s*2.6f)+4, dh=dw;
    float *dep=calloc((size_t)dw*dh,sizeof *dep);
    if(!dep) return;
    float t=fmaxf(1.0f,s*0.16f);
    /* sun: a ring and eight rays */
    for(int j=0;j<dh;j++) for(int i=0;i<dw;i++){
        float x=(float)i+0.5f-dw*0.5f, y=(float)j+0.5f-dh*0.5f, r=sqrtf(x*x+y*y);
        float sd=fabsf(r-s*0.45f)-t*0.5f;
        float ang=atan2f(y,x);
        float k=roundf(ang/(3.14159265f/4.0f))*(3.14159265f/4.0f);
        float rx=cosf(k), ry=sinf(k);
        float along=x*rx+y*ry, across=fabsf(x*ry-y*rx);
        float ray=fmaxf(fmaxf(s*0.72f-along,along-s*1.15f),across-t*0.5f);
        sd=fminf(sd,ray);
        float cov=0.5f-sd; dep[(size_t)j*dw+i]=cov<0?0:(cov>1?1:cov);
    }
    engrave_field(c,bx-dw*0.5f,by-dh*0.5f,dw,dh,dep);
    /* contrast: a ring, its right half filled */
    for(int j=0;j<dh;j++) for(int i=0;i<dw;i++){
        float x=(float)i+0.5f-dw*0.5f, y=(float)j+0.5f-dh*0.5f, r=sqrtf(x*x+y*y);
        float sd=fabsf(r-s*0.78f)-t*0.5f;
        float half=fmaxf(r-s*0.78f,-x);          /* inside the disc, x>0 */
        sd=fminf(sd,half);
        float cov=0.5f-sd; dep[(size_t)j*dw+i]=cov<0?0:(cov>1?1:cov);
    }
    engrave_field(c,cx2-dw*0.5f,cy2-dh*0.5f,dw,dh,dep);
    free(dep);
}

/* keep the plastic under a knob, draw the knob, and put the band's facing
 * alpha back - a moulded control does not catch the tube's light as if it
 * were the reveal dish */
static void knob_place(canvas *c,int which,float cx,float cy,float r,float pos){
    /* the square must hold everything rotary() draws - side band above,
     * shadow ring below - or a redraw clips them with straight edges */
    int bs=(int)(2.0f*(r*2.35f+4.0f))+2, bx=(int)(cx-bs*0.5f), by=(int)(cy-bs*0.5f);
    free(K.bg[which]);
    K.bg[which]=malloc((size_t)bs*bs*4);
    K.bx[which]=bx; K.by[which]=by; K.bs[which]=bs;
    if(!K.bg[which]) return;
    for(int j=0;j<bs;j++) for(int i=0;i<bs;i++){
        int x=bx+i, y=by+j; uint8_t *d=K.bg[which]+((size_t)j*bs+i)*4;
        if(x<0||y<0||x>=c->w||y>=c->h){ memset(d,0,4); continue; }
        memcpy(d,c->px+((size_t)y*c->w+x)*4,4);
    }
    rotary(c,cx,cy,r,pos);
    for(int j=0;j<bs;j++) for(int i=0;i<bs;i++){
        int x=bx+i, y=by+j; if(x<0||y<0||x>=c->w||y>=c->h) continue;
        c->px[((size_t)y*c->w+x)*4+3]=K.bg[which][((size_t)j*bs+i)*4+3];
    }
    K.place[which][0]=cx; K.place[which][1]=cy; K.place[which][2]=r;
}

const uint8_t *chassis_knob_set(int which,float pos,int *x,int *y,int *w,int *h){
    if(which<0||which>1||!K.bg[which]) return NULL;
    int bs=K.bs[which];
    K.patch=realloc(K.patch,(size_t)bs*bs*4);
    if(!K.patch) return NULL;
    memcpy(K.patch,K.bg[which],(size_t)bs*bs*4);
    canvas P; P.w=bs; P.h=bs; P.px=K.patch;
    if(pos<0.0f) pos=0.0f;
    if(pos>1.0f) pos=1.0f;
    rotary(&P,K.place[which][0]-K.bx[which],K.place[which][1]-K.by[which],
           K.place[which][2],pos);
    for(size_t k=0;k<(size_t)bs*bs;k++) K.patch[k*4+3]=K.bg[which][k*4+3];
    *x=K.bx[which]; *y=K.by[which]; *w=bs; *h=bs;
    return K.patch;
}

void knobs_draw(canvas *c){
    for(int k=0;k<2;k++)
        knob_place(c,k,K.place[k][0],K.place[k][1],K.place[k][2],0.5f);
}
void knobs_layout(dxm_layout *L){
    for(int k=0;k<2;k++) for(int m=0;m<3;m++) L->knob[k][m]=K.place[k][m];
}
