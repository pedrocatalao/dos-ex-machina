/* ui.c — mouse-driven slider panel for the CRT parameters. */
#include "ui.h"
#include "font.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef struct { const char *name; float *val; float lo, hi; } param;

#define MAXP 20
static param P[MAXP];
static int   NP;
static int   shown;
static int   drag = -1;
static uint8_t *buf;
static int   bw, bh;

/* panel geometry, in output pixels; scaled from display height */
static int PX, PY, PW, ROWH, LABW, SLW, PAD;

static void add(const char *n, float *v, float lo, float hi){
    if(NP<MAXP){ P[NP].name=n; P[NP].val=v; P[NP].lo=lo; P[NP].hi=hi; NP++; }
}

void ui_init(gpu_knobs *k){
    NP=0;
    add("BLOOM",        &k->bloom,        0.0f, 1.5f);
    add("BURN IN",      &k->burn_in,      0.0f, 1.0f);
    add("STATIC NOISE", &k->noise,        0.0f, 1.0f);
    add("JITTER",       &k->jitter,       0.0f, 1.0f);
    add("GLOW LINE",    &k->glow_line,    0.0f, 1.0f);
    add("AMBIENT LIGHT",&k->ambient,      0.0f, 1.0f);
    add("FLICKERING",   &k->flicker,      0.0f, 1.0f);
    add("HORIZ SYNC",   &k->hsync,        0.0f, 1.0f);
    add("RGB SHIFT",    &k->rgb_shift,    0.0f, 1.0f);
    add("CHASSIS GLOW", &k->chassis_glow, 0.0f, 1.5f);
    add("PERSISTENCE",  &k->persistence,  0.0f, 1.0f);
    add("SCANLINES",    &k->scan,         0.0f, 1.0f);
    add("PIXEL GRID",   &k->vgrid,        0.0f, 1.0f);
    add("CURVATURE",    &k->warp,         0.0f, 0.45f);
    add("BRIGHTNESS",   &k->brightness,   0.0f, 1.0f);
    add("CONTRAST",     &k->contrast,     0.4f, 1.8f);
}
int  ui_visible(void){ return shown; }
void ui_toggle(void){ shown=!shown; drag=-1; }

static void layout(int out_w,int out_h){
    (void)out_w;
    float s = out_h/1080.0f; if(s<0.75f) s=0.75f;
    ROWH=(int)(26*s); LABW=(int)(190*s); SLW=(int)(260*s); PAD=(int)(16*s);
    PW = LABW+SLW+(int)(70*s)+PAD*2;
    PX = (int)(28*s); PY = (int)(28*s);
}

int ui_mouse(int x,int y,int down,int moving){
    if(!shown) return 0;
    int ph = PAD*2 + (int)(ROWH*1.6f) + NP*ROWH;
    int inside = (x>=PX && x<PX+PW && y>=PY && y<PY+ph);
    if(!down && !moving){ drag=-1; return inside; }
    if(down && !moving){
        drag=-1;
        if(!inside) return 0;
        int y0 = PY+PAD+(int)(ROWH*1.6f);
        for(int i=0;i<NP;i++){
            int ry=y0+i*ROWH;
            if(y>=ry && y<ry+ROWH){ drag=i; break; }
        }
    }
    if(drag>=0){
        int sx = PX+PAD+LABW;
        float f = (float)(x-sx)/(float)SLW;
        if(f<0) f=0; if(f>1) f=1;
        *P[drag].val = P[drag].lo + f*(P[drag].hi-P[drag].lo);
        return 1;
    }
    return inside;
}

static void px(int x,int y,int r,int g,int b,int a){
    if(x<0||y<0||x>=bw||y>=bh) return;
    uint8_t *p=buf+((size_t)y*bw+x)*4;
    float A=a/255.0f;
    p[0]=(uint8_t)(p[0]*(1-A)+r*A); p[1]=(uint8_t)(p[1]*(1-A)+g*A);
    p[2]=(uint8_t)(p[2]*(1-A)+b*A); p[3]=(uint8_t)(p[3]+(255-p[3])*A);
}
static void box(int x,int y,int w,int h,int r,int g,int b,int a){
    for(int j=y;j<y+h;j++) for(int i=x;i<x+w;i++) px(i,j,r,g,b,a);
}
static void label(int x,int y,const char *s,float sc,int r,int g,int b,int a){
    for(int n=0;s[n];n++){
        const uint8_t *gl=font_glyph((unsigned char)s[n]);
        for(int j=0;j<8;j++) for(int i=0;i<8;i++)
            if(gl[j]&(0x80>>i))
                for(int sy=0;sy<(int)sc;sy++) for(int sx=0;sx<(int)sc;sx++)
                    px(x+(int)((n*8+i)*sc)+sx, y+(int)(j*sc)+sy, r,g,b,a);
    }
}

const uint8_t *ui_render(int out_w,int out_h,int *w,int *h){
    if(!shown) return NULL;
    layout(out_w,out_h);
    if(!buf || bw!=out_w || bh!=out_h){
        free(buf); bw=out_w; bh=out_h;
        buf=malloc((size_t)bw*bh*4);
    }
    memset(buf,0,(size_t)bw*bh*4);
    float s = out_h/1080.0f; if(s<0.75f) s=0.75f;
    float tsc = s*1.5f; if(tsc<1.0f) tsc=1.0f;
    int ph = PAD*2 + (int)(ROWH*1.6f) + NP*ROWH;

    box(PX,PY,PW,ph, 10,12,14, 214);                 /* panel */
    box(PX,PY,PW,(int)(2*s), 90,220,110,200);        /* top rule */
    label(PX+PAD, PY+PAD, "CRT ADJUST", tsc, 120,235,140,255);
    label(PX+PW-PAD-(int)(8*tsc*4), PY+PAD, "F1", tsc, 90,110,95,255);

    int y0 = PY+PAD+(int)(ROWH*1.6f);
    for(int i=0;i<NP;i++){
        int ry=y0+i*ROWH;
        int sx=PX+PAD+LABW, sy=ry+ROWH/2-(int)(3*s);
        float f=(*P[i].val - P[i].lo)/(P[i].hi-P[i].lo);
        if(f<0)f=0; if(f>1)f=1;
        label(PX+PAD, ry+ROWH/2-(int)(4*tsc), P[i].name, tsc, 176,190,180,255);
        box(sx, sy, SLW, (int)(4*s), 42,50,46, 235);          /* track  */
        box(sx, sy, (int)(SLW*f), (int)(4*s), 70,190,95, 245);/* filled */
        int kx=sx+(int)(SLW*f);
        box(kx-(int)(3*s), ry+ROWH/2-(int)(9*s), (int)(6*s), (int)(18*s),
            (drag==i)?230:170, 245, (drag==i)?190:180, 255);  /* handle */
        char v[16]; snprintf(v,sizeof v,"%.2f",*P[i].val);
        label(sx+SLW+(int)(12*s), ry+ROWH/2-(int)(4*tsc), v, tsc, 140,160,150,255);
    }
    *w=bw; *h=bh;
    return buf;
}

void ui_save(const char *path){
    FILE *f=fopen(path,"w"); if(!f) return;
    for(int i=0;i<NP;i++) fprintf(f,"%s=%.4f\n",P[i].name,*P[i].val);
    fclose(f);
}
void ui_load(const char *path){
    FILE *f=fopen(path,"r"); if(!f) return;
    char line[128];
    while(fgets(line,sizeof line,f)){
        char *eq=strchr(line,'='); if(!eq) continue;
        *eq=0;
        for(int i=0;i<NP;i++)
            if(!strcmp(P[i].name,line)){ *P[i].val=(float)atof(eq+1); break; }
    }
    fclose(f);
}
