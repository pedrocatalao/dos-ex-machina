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

struct gpu {
    int out_w, out_h;
    GLuint vao, vbo;
    GLuint prog_persist, prog_blur, prog_composite;
    GLuint tex_tube, tex_chassis;
    int tube_w, tube_h, chassis_w, chassis_h;
    GLuint fbo_persist[2], tex_persist[2];  int persist_cur;
    GLuint fbo_bloom, tex_bloom, fbo_bloom2, tex_bloom2;
    GLuint fbo_spill, tex_spill;
    double last_t; int have_last;
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
"uniform sampler2D tube, bloom, chassis, spillsrc;\n"
"uniform vec4  rect;        // tube x,y,w,h in 0..1 output space\n"
"uniform vec2  outsize;\n"
"uniform float warp, bright, contrast, ambient, glow, scan, margin;\n"
"uniform float crt_lines;\n"
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
"void main(){\n"
"  vec3 plastic = texture(chassis, vec2(uv.x, 1.0-uv.y)).rgb;\n"
"  plastic = pow(plastic, vec3(2.2));\n"
"  vec2 t = (uv - rect.xy) / rect.zw;\n"
"  vec3 col = vec3(0.0);\n"
"  float inside = 0.0;\n"
"  if (t.x>-0.15 && t.x<1.15 && t.y>-0.15 && t.y<1.15) {\n"
"    vec2 b = barrel(t);\n"
"    if (b.x>=0.0 && b.x<=1.0 && b.y>=0.0 && b.y<=1.0) {\n"
"      inside = 1.0;\n"
"      // The raster is inset inside the aperture.  The ring around it is\n"
"      // UNLIT GLASS, not black: it is the same sheet of phosphor, so it\n"
"      // carries the shadow mask, the glow bleeding past the raster edge,\n"
"      // the ambient black-level lift and the sheen.\n"
"      vec2 sb = (b - 0.5)/max(1.0 - 2.0*margin, 1e-3) + 0.5;\n"
"      vec2 cb = clamp(sb, 0.0, 1.0);\n"
"      vec2 od = max(max(-sb, sb - vec2(1.0)), vec2(0.0));\n"
"      float outd = length(od);          // 0 inside the raster\n"
"      float px = 1.0/max(rect.w*outsize.y,1.0);\n"
"      vec3 s = vec3(0.0);\n"
"      float bm = 1.0;\n"
"      if (outd < 1e-6) {\n"
"        s = texture(tube, vec2(sb.x, 1.0-sb.y)).rgb;\n"
"        s = (s - 0.5)*contrast + 0.5 + (bright-0.5)*0.6;\n"
"        bm = beam(sb.y, px);\n"
"      }\n"
"      // aperture-grille triad, pinned to OUTPUT pixels, across the whole\n"
"      // faceplate - the phosphor grid does not stop at the raster edge\n"
"      float gx = fract(uv.x*outsize.x/3.0);\n"
"      vec3 mask = vec3(0.90);\n"
"      mask.r += 0.28*step(gx,0.333); mask.g += 0.28*step(0.333,gx)*step(gx,0.666);\n"
"      mask.b += 0.28*step(0.666,gx);\n"
"      col = max(s,0.0)*bm*mask;\n"
"      // glow spills past the raster onto the unlit border, fading with\n"
"      // distance - this is what stops the margin reading as dead black\n"
"      vec3 bl = texture(bloom, vec2(cb.x,1.0-cb.y)).rgb;\n"
"      col += bl*glow*0.62*exp(-outd*11.0)*mask;\n"
"      vec2 c2 = b*2.0-1.0;\n"
"      col *= 1.0 - 0.30*dot(c2,c2)*0.5;\n"
"      col += ambient*0.016*vec3(0.9,0.95,1.0);\n"
"      float sheen = smoothstep(0.42,0.0, distance(b, vec2(0.28,0.16)));\n"
"      col += sheen*(0.008+0.022*ambient);\n"
"    }\n"
"  }\n"
"  // bezel spill: heavily blurred tube lights the surrounding plastic (6.6)\n"
"  vec2 sp = clamp((uv-rect.xy)/rect.zw, 0.0, 1.0);\n"
"  vec3 spill = texture(spillsrc, vec2(sp.x,1.0-sp.y)).rgb;\n"
"  vec2 outv = max(max(rect.xy-uv, uv-(rect.xy+rect.zw)), vec2(0.0));\n"
"  outv.x *= outsize.x/outsize.y;   // uv.x and uv.y are not the same distance\n"
"  float d = length(outv)/max(rect.w,1e-4);      // in tube-heights\n"
"  float fall = exp(-d*9.0);      // tight: spill lights the moulding,\n"
"                                 // it must not wash it out\n"
"  vec3 lit = plastic*(0.46+0.66*ambient) + spill*fall*glow*0.20;\n"
"  vec3 fin = mix(lit, col, inside);\n"
"  o = vec4(pow(max(fin,0.0), vec3(1.0/2.2)), 1.0);\n"   /* linear -> sRGB */
"}\n";

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
    glGenTextures(1,&g->tex_tube);   glBindTexture(GL_TEXTURE_2D,g->tex_tube);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
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
    return g;
}
void gpu_destroy(gpu *g){ if(g) free(g); }
void gpu_resize(gpu *g,int w,int h){ g->out_w=w; g->out_h=h; }

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
    /* pass 4a: bloom downsample+blur (fixed internal res, SPEC §6.7) */
    glUseProgram(g->prog_blur);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g->tex_persist[cur]);
    glUniform1i(glGetUniformLocation(g->prog_blur,"src"),0);
    glUniform2f(glGetUniformLocation(g->prog_blur,"dir"),1.4f/BLOOM_W,0);
    pass(g,g->prog_blur,g->fbo_bloom,BLOOM_W,BLOOM_H);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g->tex_bloom);
    glUniform2f(glGetUniformLocation(g->prog_blur,"dir"),0,1.4f/BLOOM_H);
    pass(g,g->prog_blur,g->fbo_bloom2,BLOOM_W,BLOOM_H);
    /* spill: downsample hard, then blur again — soft enough not to streak */
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g->tex_bloom2);
    glUniform2f(glGetUniformLocation(g->prog_blur,"dir"),1.6f/SPILL_W,0);
    pass(g,g->prog_blur,g->fbo_spill,SPILL_W,SPILL_H);
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
    glUniform4f(glGetUniformLocation(p,"rect"),tx,ty,tw,th);
    glUniform2f(glGetUniformLocation(p,"outsize"),(float)g->out_w,(float)g->out_h);
    glUniform1f(glGetUniformLocation(p,"warp"),k->warp);
    glUniform1f(glGetUniformLocation(p,"bright"),k->brightness);
    glUniform1f(glGetUniformLocation(p,"contrast"),k->contrast);
    glUniform1f(glGetUniformLocation(p,"ambient"),k->ambient);
    glUniform1f(glGetUniformLocation(p,"glow"),k->glow);
    glUniform1f(glGetUniformLocation(p,"scan"),k->scan);
    glUniform1f(glGetUniformLocation(p,"margin"),k->margin);
    glUniform1f(glGetUniformLocation(p,"crt_lines"),(float)k->crt_lines);
    glBindVertexArray(g->vao); glDrawArrays(GL_TRIANGLES,0,3);
}
uint8_t *gpu_readback(gpu *g,int *w,int *h){
    *w=g->out_w; *h=g->out_h;
    uint8_t *px=malloc((size_t)g->out_w*g->out_h*3);
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,g->out_w,g->out_h,GL_RGB,GL_UNSIGNED_BYTE,px);
    return px;
}
