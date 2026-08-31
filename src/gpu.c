/* gpu.c — OpenGL 3.3 core implementation of the tube pipeline (SPEC §6.4).
 *
 * Pass order, all in linear light until the final encode:
 *   0 upload -> 1 persistence (ping-pong) -> 2 curvature -> 3 beam/mask/bleed
 *   -> 4 bloom -> 5 glass+vignette -> 6 bezel spill -> 7 sRGB encode
 * Passes 2..7 are fused into one output-resolution shader; persistence and
 * bloom are separate because they need their own targets. */
#include "gpu.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef __APPLE__
#  define GL_SILENCE_DEPRECATION 1
#  include <OpenGL/gl3.h>
#else
#  include <SDL3/SDL_opengl.h>
#endif

#define PERSIST_W 640
#define PERSIST_H 400
#define BLOOM_W   160
#define BLOOM_H   100
/* Spill source is deliberately tiny: sampling a sharp blur outside the
 * tube and clamping streaks the bright rows sideways across the room. */
#define SPILL_W    24
#define SPILL_H    15
/* A near-average of the whole picture: how much light the tube is
 * actually throwing into the room, regardless of where you are on the
 * chassis.  The edge-local spill alone cannot express that. */
#define ROOM_W      3
#define ROOM_H      2

struct gpu {
    int out_w, out_h;
    GLuint vao, vbo;
    GLuint prog_persist, prog_blur, prog_composite;
    GLuint tex_tube, tex_chassis;
    int tube_w, tube_h, chassis_w, chassis_h;
    GLuint fbo_persist[2], tex_persist[2];  int persist_cur;
    GLuint fbo_bloom, tex_bloom, fbo_bloom2, tex_bloom2;
    GLuint fbo_spill, tex_spill;
    GLuint fbo_room, tex_room;
    GLuint fbo_burn[2], tex_burn[2]; int burn_cur;
    GLuint prog_burn, prog_overlay, tex_overlay;
    int    ov_w, ov_h;
    double last_t; int have_last;
    float led[2][4], led_col[2][3], led_on[2], led_round[2];
};

static const char *VS =
"#version 330 core\n"
"layout(location=0) in vec2 p;\n"
"out vec2 uv;\n"
"void main(){ uv = p*0.5+0.5; gl_Position = vec4(p,0,1); }\n";

/* ---- pass 1: phosphor persistence + burn-in, time-based decay (SPEC §6.7) */
static const char *FS_PERSIST =
"#version 330 core\n"
"in vec2 uv; out vec4 o;\n"
"uniform sampler2D src, prev;\n"
"uniform float dt, persist;\n"
"void main(){\n"
"  vec3 cur = texture(src, uv).rgb;\n"
"  vec3 old = texture(prev, uv).rgb;\n"
"  cur = pow(cur, vec3(2.2));\n"            /* to linear */
"  float hl = mix(0.010, 0.075, persist);\n" /* half-life in SECONDS, not frames */
"  vec3 k = vec3(pow(0.5, dt/hl), pow(0.5, dt/(hl*1.25)), pow(0.5, dt/(hl*0.8)));\n"
"  o = vec4(max(cur, old*k), 1.0);\n"        /* green persists longest (P22) */
"}\n";

static const char *FS_BLUR =
"#version 330 core\n"
"in vec2 uv; out vec4 o;\n"
"uniform sampler2D src; uniform vec2 dir;\n"
"void main(){\n"
"  vec3 s = texture(src,uv).rgb*0.227;\n"
"  s += (texture(src,uv+dir*1.38).rgb + texture(src,uv-dir*1.38).rgb)*0.316;\n"
"  s += (texture(src,uv+dir*3.23).rgb + texture(src,uv-dir*3.23).rgb)*0.070;\n"
"  o = vec4(s,1.0);\n"
"}\n";

/* ---- passes 2..7 fused: curvature, beam/mask, bloom add, glass, spill ---- */
static const char *FS_COMPOSITE =
"#version 330 core\n"
"in vec2 uv; out vec4 o;\n"
"uniform sampler2D tube, bloom, chassis, spillsrc, roomsrc, burnsrc;\n"
"uniform vec4  rect;        // tube x,y,w,h in 0..1 output space\n"
"uniform vec2  outsize;\n"
"uniform float warp, bright, contrast, ambient, scan, margin;\n"
"uniform float u_bloom, u_burn, u_noise, u_jitter, u_glowline;\n"
"uniform float u_flicker, u_hsync, u_rgb, u_chassis;\n"
"uniform float aper_r, time;\n"
"uniform vec4  led[2];\n"
"uniform vec3  ledcol[2];\n"
"uniform float ledon[2];\n"
"uniform float ledround[2];\n"
"uniform float crt_lines, crt_cols, vgrid;\n"
"uniform vec2  texsize, texelpx;   // tube texture, and one output pixel\n"
"uniform float u_sharp;            // 1 on the DOS screen, 0 in a game\n"
"uniform float u_overscan;         // picture overflow, in OUTPUT pixels\n"
"\n"
"float hash(vec2 p){ return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }\n"
"\n"
"// Two sampling regimes, because the two sources want opposite things.\n"
"// TEXT (8x16 glyphs at ~3x) needs each source pixel to have an edge\n"
"// exactly one OUTPUT pixel wide, or nearest-neighbour rounds strokes to\n"
"// whole pixels unevenly and the same glyph gets a fat left edge here\n"
"// and a fat right edge there.  GAME ART (320x200 at ~6x) wants honest\n"
"// hard pixels - any smoothing there just reads as lost resolution.\n"
"// The edge width is passed in rather than taken from fwidth(), which is\n"
"// undefined inside the non-uniform control flow this runs in.\n"
"vec2 tap(vec2 c){\n"
"  vec2 p = c*texsize;\n"
"  vec2 i = floor(p) + 0.5;\n"
"  if (u_sharp < 0.5) return i/texsize;      // exactly nearest\n"
"  vec2 d = p - i;\n"
"  vec2 w = max(texelpx*0.5, vec2(1e-5));\n"
"  return (i + clamp(d/w, -1.0, 1.0)*0.5) / texsize;\n"
"}\n"
"\n"
"vec2 barrel(vec2 p){\n"
"  vec2 c = p*2.0-1.0;\n"
"  float r2 = dot(c,c);\n"
"  c *= 1.0 + warp*0.30*r2;      // DXM_WARP_K  (crt.h)\n"
"  c /= 1.0 + warp*0.32;         // DXM_WARP_NORM (crt.h) - keep in sync\n"
"  return c*0.5+0.5;\n"
"}\n"
"// beam profile INTEGRATED over the pixel footprint, so scanlines do not\n"
"// alias when tube height is not a multiple of crt_lines (SPEC 6.4).\n"
"float beam(float y, float px){\n"
"  float l = y*crt_lines;\n"
"  float d = abs(fract(l)-0.5)*2.0;\n"
"  float w = clamp(px*crt_lines, 0.6, 4.0);\n"
"  float g = exp(-d*d*3.0/ (w*0.55));\n"
"  return mix(1.0, g, scan);\n"
"}\n"
"float column(float x, float px){\n"
"  float c = x*crt_cols;\n"
"  float d = abs(fract(c)-0.5)*2.0;\n"
"  float w = clamp(px*crt_cols, 0.6, 4.0);\n"
"  float g = exp(-d*d*3.0/ (w*0.62));\n"
"  return mix(1.0, g, vgrid);\n"
"}\n"
"void main(){\n"
"  vec4 chas = texture(chassis, vec2(uv.x, 1.0-uv.y));\n"
"  vec3 plastic = chas.rgb;\n"
"  // alpha carries how much this surface faces the tube: the reveal dish\n"
"  // is angled at the glass and catches far more light than the flat case\n"
"  float facing = 0.22 + 1.55*chas.a;\n"
"  plastic = pow(plastic, vec3(2.2));\n"
"  vec2 t = (uv - rect.xy) / rect.zw;\n"
"  vec3 col = vec3(0.0);\n"
"  float inside = 0.0;\n"
"  if (t.x>-0.15 && t.x<1.15 && t.y>-0.15 && t.y<1.15) {\n"
"    // ---- deflection errors, applied BEFORE the barrel so they behave\n"
"    // like real deflection rather than like a moving texture ----\n"
"    // JITTER: the whole raster twitching frame to frame\n"
"    vec2 jit = vec2(hash(vec2(floor(time*60.0),1.0))-0.5,\n"
"                    hash(vec2(floor(time*60.0),7.0))-0.5);\n"
"    t += jit * u_jitter * 0.010;\n"
"    // HSYNC: each LINE starts at slightly the wrong place, drifting\n"
"    float lineno = floor(t.y*crt_lines);\n"
"    float hs = (hash(vec2(lineno, floor(time*24.0)))-0.5)\n"
"             + 0.6*sin(t.y*38.0 + time*5.0);\n"
"    t.x += hs * u_hsync * 0.012;\n"
"\n"
"    vec2 b = barrel(t);\n"
"    // The glass must be cut to the SAME rounded box the chassis carved,\n"
"    // evaluated in the same warped space.\n"
"    vec2 halfpx = rect.zw*outsize*0.5;\n"
"    vec2 apx    = abs(b*2.0-1.0)*halfpx;\n"
"    vec2 qq     = apx - (halfpx - aper_r);\n"
"    float asd   = (qq.x>0.0 && qq.y>0.0) ? length(qq)-aper_r\n"
"                                         : max(apx.x-halfpx.x, apx.y-halfpx.y);\n"
"    // The GLASS REGION opens outward by the overscan, so lit content\n"
"    // actually reaches under the moulding.  Scaling the sample alone did\n"
"    // nothing visible: the boundary stayed exactly where it was.\n"
"    if (asd <= u_overscan) {\n"
"      inside = 1.0;\n"
"      // Overscan: push the picture a little PAST the aperture so its\n"
"      // edge is tucked under the moulding instead of ending exactly at\n"
"      // it.  Real sets always overscanned; it also means no seam can\n"
"      // show between the last lit pixel and the dish.\n"
"      vec2 e = u_overscan / max(rect.zw*outsize*0.5, vec2(1.0));\n"
"      vec2 sb = (b - 0.5)/(1.0 + e)/max(1.0 - 2.0*margin, 1e-3) + 0.5;\n"
"      vec2 cb = clamp(sb, 0.0, 1.0);\n"
"      vec2 od = max(max(-sb, sb - vec2(1.0)), vec2(0.0));\n"
"      float outd = length(od);          // 0 inside the raster\n"
"      float px = 1.0/max(rect.w*outsize.y,1.0);\n"
"      vec3 s = vec3(0.0);\n"
"      float bm = 1.0;\n"
"      if (outd < 1e-6) {\n"
"        // RGB SHIFT: the three guns landing at slightly different places,\n"
"        // splayed outward from the centre the way real convergence errors\n"
"        // grow toward the edges of the tube\n"
"        vec2 ctr = sb - 0.5;\n"
"        vec2 sep = ctr * u_rgb * 0.020;\n"
"        s.r = texture(tube, tap(vec2(sb.x+sep.x, 1.0-(sb.y+sep.y)))).r;\n"
"        s.g = texture(tube, tap(vec2(sb.x,       1.0- sb.y      ))).g;\n"
"        s.b = texture(tube, tap(vec2(sb.x-sep.x, 1.0-(sb.y-sep.y)))).b;\n"
"        s = (s - 0.5)*contrast + 0.5 + (bright-0.5)*0.6;\n"
"        bm = beam(sb.y, px);\n"
"        bm *= column(sb.x, 1.0/max(rect.z*outsize.x,1.0));\n"
"      }\n"
"      // Aperture-grille triad locked to the SOURCE pixel grid: one full\n"
"      // R|G|B triad per source pixel.  Pinning it to output pixels on a\n"
"      // 3px period meant every character cell landed on a different\n"
"      // sub-phase of the stripes, so the same glyph came out with a\n"
"      // bright left edge in one column and a bright right edge in the\n"
"      // next, with colour fringing that changed across the screen.\n"
"      float gx = fract(cb.x*crt_cols);\n"
"      vec3 mask = vec3(0.94);\n"
"      mask.r += 0.20*step(gx,0.333); mask.g += 0.20*step(0.333,gx)*step(gx,0.666);\n"
"      mask.b += 0.20*step(0.666,gx);\n"
"      col = max(s,0.0)*bm*mask;\n"
"\n"
"      // BURN-IN: the slow accumulator, added as a faint ghost\n"
"      vec3 burn = texture(burnsrc, vec2(cb.x,1.0-cb.y)).rgb;\n"
"      col += burn * u_burn * 0.55 * mask;\n"
"\n"
"      // BLOOM: light bleeding between lit pixels\n"
"      vec3 bl = texture(bloom, vec2(cb.x,1.0-cb.y)).rgb;\n"
"      col += bl*u_bloom*0.34*exp(-outd*11.0)*mask;\n"
"\n"
"      // GLOW LINE: the bright band drifting slowly down the tube, left by\n"
"      // the refresh beating against the eye\n"
"      // sb.y == 1 is the TOP of the picture, so the phase must ADVANCE\n"
"      // with time for the band to drift downward, the way the refresh\n"
"      // beating against mains actually rolls.\n"
"      float gl = fract(cb.y*0.5 + time*0.10);\n"
"      col += vec3(0.55,0.85,1.0) * u_glowline * 0.055\n"
"           * exp(-pow((gl-0.5)/0.06, 2.0));\n"
"\n"
"      vec2 c2 = b*2.0-1.0;\n"
"      col *= 1.0 - 0.30*dot(c2,c2)*0.5;\n"
"      col += ambient*0.016*vec3(0.9,0.95,1.0);\n"
"      float sheen = smoothstep(0.42,0.0, distance(b, vec2(0.28,0.16)));\n"
"      col += sheen*(0.008+0.022*ambient);\n"
"\n"
"      // STATIC NOISE: snow on the phosphor\n"
"      float n = hash(uv*outsize + vec2(time*371.0, time*137.0)) - 0.5;\n"
"      col += n * u_noise * 0.16;\n"
"\n"
"      // FLICKER: the mains-rate brightness wobble of an old set\n"
"      float fl = 1.0 + u_flicker*0.10*(sin(time*47.0)*0.6 + sin(time*113.0)*0.4)\n"
"               + u_flicker*0.05*(hash(vec2(floor(time*50.0),3.0))-0.5);\n"
"      col *= fl;\n"
"    }\n"
"  }\n"
"  // bezel spill: heavily blurred tube lights the surrounding plastic (6.6)\n"
"  vec2 sp = clamp((uv-rect.xy)/rect.zw, 0.0, 1.0);\n"
"  vec3 spill = texture(spillsrc, vec2(sp.x,1.0-sp.y)).rgb;\n"
"  vec2 outv = max(max(rect.xy-uv, uv-(rect.xy+rect.zw)), vec2(0.0));\n"
"  outv.x *= outsize.x/outsize.y;   // uv.x and uv.y are not the same distance\n"
"  float d = length(outv)/max(rect.w,1e-4);      // in tube-heights\n"
"  float fall = exp(-d*5.0);      // reaches further across the moulding\n"
"  // ambient is PERCEPTUAL: the sRGB encode at the end compresses linear\n"
"  // factors toward 1, so a linear ramp here looks nearly flat.\n"
"  float amb = pow(0.16 + 0.98*ambient, 2.2);\n"
"  // What the tube throws into the ROOM: a near-average of the whole\n"
"  // picture, lighting the entire chassis and falling off only slowly.\n"
"  vec3 room = texture(roomsrc, vec2(0.5,0.5)).rgb;\n"
"  float roomfall = exp(-d*1.1);\n"
"  vec3 lit = plastic*amb\n"
"           + spill*fall*u_chassis*facing*(0.30+0.26*(1.0-ambient))\n"
"           + room*roomfall*u_chassis*facing*(0.34+0.40*(1.0-ambient));\n"
"  vec3 fin = mix(lit, col, inside);\n"
"  for (int i = 0; i < 2; ++i) {\n"
"    if (ledon[i] <= 0.001) continue;\n"
"    vec2 lc = led[i].xy + led[i].zw*0.5;\n"
"    vec2 dd  = (uv - lc) / (led[i].zw*0.5);\n"
"    float m = mix(max(abs(dd.x),abs(dd.y)), length(dd), ledround[i]);\n"
"    float lens = 1.0 - smoothstep(0.82, 1.02, m);\n"
"    float bleed = exp(-dot(dd,dd)*0.75);\n"
"    fin += ledcol[i] * (lens*1.45 + bleed*0.22) * ledon[i];\n"
"  }\n"
"  o = vec4(pow(max(fin,0.0), vec3(1.0/2.2)), 1.0);\n"
"}\n";

/* burn-in accumulator: a very slow exponential average of the picture.  Its
 * time constant is in TENS OF SECONDS, which is what makes static content
 * (a prompt, a HUD) etch in while moving content leaves nothing. */
static const char *FS_BURN =
"#version 330 core\n"
"in vec2 uv; out vec4 o;\n"
"uniform sampler2D src, prev;\n"
"uniform float dt, rate;\n"
"void main(){\n"
"  vec3 cur = pow(texture(src, uv).rgb, vec3(2.2));\n"
"  vec3 old = texture(prev, uv).rgb;\n"
"  float k = 1.0 - exp(-dt/max(rate,0.001));   // seconds, time-based\n"
"  o = vec4(mix(old, cur, k), 1.0);\n"
"}\n";

/* the settings panel, straight alpha over the finished frame */
static const char *FS_OVERLAY =
"#version 330 core\n"
"in vec2 uv; out vec4 o;\n"
"uniform sampler2D src;\n"
"void main(){ o = texture(src, vec2(uv.x, 1.0-uv.y)); }\n";

static GLuint mkshader(GLenum t, const char *src){
    GLuint s=glCreateShader(t); glShaderSource(s,1,&src,NULL); glCompileShader(s);
    GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){ char log[4096]; glGetShaderInfoLog(s,sizeof log,NULL,log);
             fprintf(stderr,"shader compile failed:\n%s\n",log); }
    return s;
}
static GLuint mkprog(const char *fs){
    GLuint p=glCreateProgram();
    GLuint v=mkshader(GL_VERTEX_SHADER,VS), f=mkshader(GL_FRAGMENT_SHADER,fs);
    glAttachShader(p,v); glAttachShader(p,f); glLinkProgram(p);
    GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){ char log[4096]; glGetProgramInfoLog(p,sizeof log,NULL,log);
             fprintf(stderr,"link failed:\n%s\n",log); }
    glDeleteShader(v); glDeleteShader(f); return p;
}
static void mktarget(GLuint *fbo, GLuint *tex, int w, int h){
    glGenTextures(1,tex); glBindTexture(GL_TEXTURE_2D,*tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1,fbo); glBindFramebuffer(GL_FRAMEBUFFER,*fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,*tex,0);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
}

gpu *gpu_create(int w,int h){
    gpu *g=calloc(1,sizeof *g); g->out_w=w; g->out_h=h;
    static const float quad[]={-1,-1, 3,-1, -1,3};
    glGenVertexArrays(1,&g->vao); glBindVertexArray(g->vao);
    glGenBuffers(1,&g->vbo); glBindBuffer(GL_ARRAY_BUFFER,g->vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof quad,quad,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);
    g->prog_persist=mkprog(FS_PERSIST);
    g->prog_blur=mkprog(FS_BLUR);
    g->prog_composite=mkprog(FS_COMPOSITE);
    g->prog_burn=mkprog(FS_BURN);
    g->prog_overlay=mkprog(FS_OVERLAY);
    glGenTextures(1,&g->tex_tube);   glBindTexture(GL_TEXTURE_2D,g->tex_tube);
    /* LINEAR, but the shader snaps to texel centres when sharp() is off,
     * which reproduces NEAREST exactly - so the filter never has to change
     * between the DOS screen and a running game. */
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenTextures(1,&g->tex_chassis);glBindTexture(GL_TEXTURE_2D,g->tex_chassis);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    mktarget(&g->fbo_persist[0],&g->tex_persist[0],PERSIST_W,PERSIST_H);
    mktarget(&g->fbo_persist[1],&g->tex_persist[1],PERSIST_W,PERSIST_H);
    mktarget(&g->fbo_bloom,&g->tex_bloom,BLOOM_W,BLOOM_H);
    mktarget(&g->fbo_bloom2,&g->tex_bloom2,BLOOM_W,BLOOM_H);
    mktarget(&g->fbo_spill,&g->tex_spill,SPILL_W,SPILL_H);
    mktarget(&g->fbo_room,&g->tex_room,ROOM_W,ROOM_H);
    mktarget(&g->fbo_burn[0],&g->tex_burn[0],PERSIST_W,PERSIST_H);
    mktarget(&g->fbo_burn[1],&g->tex_burn[1],PERSIST_W,PERSIST_H);
    glGenTextures(1,&g->tex_overlay); glBindTexture(GL_TEXTURE_2D,g->tex_overlay);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    return g;
}
void gpu_destroy(gpu *g){ if(g) free(g); }
void gpu_resize(gpu *g,int w,int h){ g->out_w=w; g->out_h=h; }
void gpu_set_led(gpu *g,int idx,float x,float y,float w,float h,
                 float on,float r,float gr,float b,int round){
    if(idx<0||idx>1) return;
    g->led[idx][0]=x; g->led[idx][1]=y; g->led[idx][2]=w; g->led[idx][3]=h;
    g->led_col[idx][0]=r; g->led_col[idx][1]=gr; g->led_col[idx][2]=b;
    g->led_on[idx]=on; g->led_round[idx]=round?1.0f:0.0f;
}

void gpu_set_chassis(gpu *g,const uint8_t *rgba,int w,int h){
    g->chassis_w=w; g->chassis_h=h;
    glBindTexture(GL_TEXTURE_2D,g->tex_chassis);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
}
void gpu_set_tube(gpu *g,const uint8_t *rgb,int w,int h){
    g->tube_w=w; g->tube_h=h;
    glBindTexture(GL_TEXTURE_2D,g->tex_tube);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,w,h,0,GL_RGB,GL_UNSIGNED_BYTE,rgb);
}
static void pass(gpu *g,GLuint prog,GLuint fbo,int w,int h){
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glViewport(0,0,w,h); glUseProgram(prog); glBindVertexArray(g->vao);
    glDrawArrays(GL_TRIANGLES,0,3);
}
void gpu_draw(gpu *g,float tx,float ty,float tw,float th,const gpu_knobs *k,double t){
    float dt = g->have_last ? (float)(t-g->last_t) : 1.0f/60.0f;
    if(dt<=0.0f||dt>0.25f) dt=1.0f/60.0f;
    g->last_t=t; g->have_last=1;
    int prev=g->persist_cur, cur=1-prev;
    /* pass 1: persistence */
    glUseProgram(g->prog_persist);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g->tex_tube);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,g->tex_persist[prev]);
    glUniform1i(glGetUniformLocation(g->prog_persist,"src"),0);
    glUniform1i(glGetUniformLocation(g->prog_persist,"prev"),1);
    glUniform1f(glGetUniformLocation(g->prog_persist,"dt"),dt);
    glUniform1f(glGetUniformLocation(g->prog_persist,"persist"),k->persistence);
    pass(g,g->prog_persist,g->fbo_persist[cur],PERSIST_W,PERSIST_H);
    g->persist_cur=cur;
    /* burn-in: a much slower average of the same signal */
    { int bp=g->burn_cur, bc=1-bp;
      glUseProgram(g->prog_burn);
      glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g->tex_persist[cur]);
      glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,g->tex_burn[bp]);
      glUniform1i(glGetUniformLocation(g->prog_burn,"src"),0);
      glUniform1i(glGetUniformLocation(g->prog_burn,"prev"),1);
      glUniform1f(glGetUniformLocation(g->prog_burn,"dt"),dt);
      glUniform1f(glGetUniformLocation(g->prog_burn,"rate"),28.0f);
      pass(g,g->prog_burn,g->fbo_burn[bc],PERSIST_W,PERSIST_H);
      g->burn_cur=bc;
    }
    /* pass 4a: bloom downsample+blur (fixed internal res, SPEC §6.7) */
    glUseProgram(g->prog_blur);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g->tex_persist[cur]);
    glUniform1i(glGetUniformLocation(g->prog_blur,"src"),0);
    glUniform2f(glGetUniformLocation(g->prog_blur,"dir"),0.9f/BLOOM_W,0);
    pass(g,g->prog_blur,g->fbo_bloom,BLOOM_W,BLOOM_H);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g->tex_bloom);
    glUniform2f(glGetUniformLocation(g->prog_blur,"dir"),0,0.9f/BLOOM_H);
    pass(g,g->prog_blur,g->fbo_bloom2,BLOOM_W,BLOOM_H);
    /* A single pass at this resolution still carries the glyph shapes - a
     * character is a few bloom texels across, so the kernel cannot round it
     * off.  Two more, wider passes turn the glow into a soft halo that no
     * longer traces the letterforms. */
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g->tex_bloom2);
    glUniform2f(glGetUniformLocation(g->prog_blur,"dir"),0.55f/BLOOM_W,0);
    pass(g,g->prog_blur,g->fbo_bloom,BLOOM_W,BLOOM_H);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g->tex_bloom);
    glUniform2f(glGetUniformLocation(g->prog_blur,"dir"),0,0.55f/BLOOM_H);
    pass(g,g->prog_blur,g->fbo_bloom2,BLOOM_W,BLOOM_H);
    /* spill: downsample hard, then blur again — soft enough not to streak */
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g->tex_bloom2);
    glUniform2f(glGetUniformLocation(g->prog_blur,"dir"),1.6f/SPILL_W,0);
    pass(g,g->prog_blur,g->fbo_spill,SPILL_W,SPILL_H);
    /* down again to almost nothing: the room-light term */
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g->tex_spill);
    glUniform2f(glGetUniformLocation(g->prog_blur,"dir"),1.0f/ROOM_W,0);
    pass(g,g->prog_blur,g->fbo_room,ROOM_W,ROOM_H);
    /* composite */
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glViewport(0,0,g->out_w,g->out_h);
    glUseProgram(g->prog_composite);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g->tex_persist[cur]);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,g->tex_bloom2);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D,g->tex_chassis);
    GLuint p=g->prog_composite;
    glUniform1i(glGetUniformLocation(p,"tube"),0);
    glUniform1i(glGetUniformLocation(p,"bloom"),1);
    glUniform1i(glGetUniformLocation(p,"chassis"),2);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D,g->tex_spill);
    glUniform1i(glGetUniformLocation(p,"spillsrc"),3);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D,g->tex_room);
    glUniform1i(glGetUniformLocation(p,"roomsrc"),4);
    glUniform4f(glGetUniformLocation(p,"rect"),tx,ty,tw,th);
    glUniform2f(glGetUniformLocation(p,"outsize"),(float)g->out_w,(float)g->out_h);
    glUniform1f(glGetUniformLocation(p,"warp"),k->warp);
    glUniform1f(glGetUniformLocation(p,"bright"),k->brightness);
    glUniform1f(glGetUniformLocation(p,"contrast"),k->contrast);
    glUniform1f(glGetUniformLocation(p,"ambient"),k->ambient);
    glUniform1f(glGetUniformLocation(p,"scan"),k->scan);
    glUniform1f(glGetUniformLocation(p,"margin"),k->margin);
    glUniform1f(glGetUniformLocation(p,"aper_r"),k->aperture_r);
    glUniform4fv(glGetUniformLocation(p,"led"),2,&g->led[0][0]);
    glUniform3fv(glGetUniformLocation(p,"ledcol"),2,&g->led_col[0][0]);
    glUniform1fv(glGetUniformLocation(p,"ledon"),2,g->led_on);
    glUniform1fv(glGetUniformLocation(p,"ledround"),2,g->led_round);
    glUniform1f(glGetUniformLocation(p,"crt_lines"),(float)k->crt_lines);
    glUniform1f(glGetUniformLocation(p,"crt_cols"),(float)k->crt_cols);
    glUniform2f(glGetUniformLocation(p,"texsize"),
                (float)g->tube_w,(float)g->tube_h);
    glUniform2f(glGetUniformLocation(p,"texelpx"),
                (float)g->tube_w /fmaxf(tw*(float)g->out_w,1.0f),
                (float)g->tube_h /fmaxf(th*(float)g->out_h,1.0f));
    glUniform1f(glGetUniformLocation(p,"u_sharp"),k->sharp_text);
    glUniform1f(glGetUniformLocation(p,"u_overscan"),k->overscan);
    glUniform1f(glGetUniformLocation(p,"vgrid"),k->vgrid);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D,g->tex_burn[g->burn_cur]);
    glUniform1i(glGetUniformLocation(p,"burnsrc"),5);
    glUniform1f(glGetUniformLocation(p,"time"),(float)t);
    glUniform1f(glGetUniformLocation(p,"u_bloom"),k->bloom);
    glUniform1f(glGetUniformLocation(p,"u_burn"),k->burn_in);
    glUniform1f(glGetUniformLocation(p,"u_noise"),k->noise);
    glUniform1f(glGetUniformLocation(p,"u_jitter"),k->jitter);
    glUniform1f(glGetUniformLocation(p,"u_glowline"),k->glow_line);
    glUniform1f(glGetUniformLocation(p,"u_flicker"),k->flicker);
    glUniform1f(glGetUniformLocation(p,"u_hsync"),k->hsync);
    glUniform1f(glGetUniformLocation(p,"u_rgb"),k->rgb_shift);
    glUniform1f(glGetUniformLocation(p,"u_chassis"),k->chassis_glow);
    glBindVertexArray(g->vao); glDrawArrays(GL_TRIANGLES,0,3);
}
uint8_t *gpu_readback(gpu *g,int *w,int *h){
    *w=g->out_w; *h=g->out_h;
    uint8_t *px=malloc((size_t)g->out_w*g->out_h*3);
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,g->out_w,g->out_h,GL_RGB,GL_UNSIGNED_BYTE,px);
    return px;
}

void gpu_set_overlay(gpu *g,const uint8_t *rgba,int w,int h){
    if(!rgba || w<=0 || h<=0){ g->ov_w=0; g->ov_h=0; return; }
    g->ov_w=w; g->ov_h=h;
    glBindTexture(GL_TEXTURE_2D,g->tex_overlay);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
}
void gpu_draw_overlay(gpu *g){
    if(g->ov_w<=0) return;
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glViewport(0,0,g->out_w,g->out_h);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g->prog_overlay);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g->tex_overlay);
    glUniform1i(glGetUniformLocation(g->prog_overlay,"src"),0);
    glBindVertexArray(g->vao); glDrawArrays(GL_TRIANGLES,0,3);
    glDisable(GL_BLEND);
}
