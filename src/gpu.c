/* gpu.c — OpenGL 3.3 core implementation of the tube pipeline (SPEC §6.4).
 *
 * Pass order, all in linear light until the final encode:
 *   0 upload -> 1 persistence (ping-pong) -> 2 curvature -> 3 beam/mask/bleed
 *   -> 4 bloom -> 5 glass+vignette -> 6 bezel spill -> 7 sRGB encode
 * Passes 2..7 are fused into one output-resolution shader; persistence and
 * bloom are separate because they need their own targets. */
#include "gpu.h"
#include "crt.h"
#include "segdisp.h"
/* The GLSL lives in the shaders directory, one file per pass; the build
 * bakes each into a string in this header (tools/embed.cmake).  Nothing is
 * loaded from disk at run time, so a release is still one binary. */
#include "shaders.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

static void (*g_logfn)(const char *);
void gpu_set_log(void (*fn)(const char *)) {
    g_logfn = fn;
}
static void gpu_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void gpu_logf(const char *fmt, ...) {
    char buf[4600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (g_logfn)
        g_logfn(buf);
    else
        fprintf(stderr, "[dxm] %s\n", buf);
}

/* Names the loader could not resolve, kept so the failure can be SHOWN to
 * the person in front of the machine, not just written to a stderr nobody
 * is looking at.  Empty on macOS, where nothing is loaded. */
static char gl_missing[1024];

#ifdef __APPLE__
#    define GL_SILENCE_DEPRECATION 1
#    include <OpenGL/gl3.h>
#    define gl_load() (1)
#else
/* Everywhere but macOS, the system GL library exports only 1.1 and the rest
 * has to come from the driver at run time.  glfuncs.h lists what we use; the
 * pointers and the loader are both generated from it, so the list cannot
 * drift out of step with either. */
#    include <SDL3/SDL_opengl.h>
#    include <SDL3/SDL.h>
static void gl_note_missing(const char *n) {
    size_t l = strlen(gl_missing);
    snprintf(gl_missing + l, sizeof gl_missing - l, "%s%s", l ? ", " : "", n);
}
#    define GLF(ret, name, args) static ret(APIENTRY *p_##name) args;
#    include "glfuncs.h"
#    undef GLF
#    define GLF(ret, name, args)                                                                   \
        p_##name = (ret(APIENTRY *) args)SDL_GL_GetProcAddress(#name);                             \
        if (!p_##name) {                                                                           \
            gpu_logf("GL: no %s", #name);                                                          \
            ok = 0;                                                                                \
            gl_note_missing(#name);                                                                \
        }
static int gl_load(void) {
    int ok = 1;
#    include "glfuncs.h"
    return ok;
}
#    undef GLF
/* From here the plain names mean the loaded pointers, so every call site in
 * this file is unchanged.  Renaming rather than shadowing matters: some
 * system GL headers do declare the 1.2/1.3 entry points, and a pointer with
 * the same name would collide with the prototype. */
#    define glActiveTexture p_glActiveTexture
#    define glAttachShader p_glAttachShader
#    define glBindBuffer p_glBindBuffer
#    define glBindFramebuffer p_glBindFramebuffer
#    define glBindVertexArray p_glBindVertexArray
#    define glBufferData p_glBufferData
#    define glCompileShader p_glCompileShader
#    define glCreateProgram p_glCreateProgram
#    define glCreateShader p_glCreateShader
#    define glDeleteShader p_glDeleteShader
#    define glEnableVertexAttribArray p_glEnableVertexAttribArray
#    define glFramebufferTexture2D p_glFramebufferTexture2D
#    define glGenBuffers p_glGenBuffers
#    define glGenFramebuffers p_glGenFramebuffers
#    define glGenVertexArrays p_glGenVertexArrays
#    define glGenerateMipmap p_glGenerateMipmap
#    define glGetProgramInfoLog p_glGetProgramInfoLog
#    define glGetProgramiv p_glGetProgramiv
#    define glGetShaderInfoLog p_glGetShaderInfoLog
#    define glGetShaderiv p_glGetShaderiv
#    define glGetUniformLocation p_glGetUniformLocation
#    define glLinkProgram p_glLinkProgram
#    define glShaderSource p_glShaderSource
#    define glUniform1f p_glUniform1f
#    define glUniform1fv p_glUniform1fv
#    define glUniform1i p_glUniform1i
#    define glUniform2f p_glUniform2f
#    define glUniform3fv p_glUniform3fv
#    define glUniform4f p_glUniform4f
#    define glUniform4fv p_glUniform4fv
#    define glUseProgram p_glUseProgram
#    define glVertexAttribPointer p_glVertexAttribPointer
#endif

/* The persistence and burn-in targets take the SOURCE's own size - they are
 * a memory of the picture, texel for texel, and the composite reads the
 * picture from them.  They start at this size and follow the tube texture
 * from the first frame on.  A fixed 640x400 here was wrong for everything
 * that was not 320x200 or 640x400: the 668-column text screen resampled
 * to 640 and then read back as if it were 680, a beat every seventeen
 * columns; a 640x480 picture lost eighty rows. */
#define PERSIST_W 640
#define PERSIST_H 400
#define BLOOM_W 160
#define BLOOM_H 100
/* The picture's light at its edges, for the case: four profiles, one per
 * edge, each point the picture integrated inward with distance (edge.frag).
 * The case reads the profile of the edge it is nearest, at its own place
 * along it, so what lights the bottom dish is what is near the bottom of
 * the picture, and what lights a side is what is at that height. */
#define EDGE_W 96
#define EDGE_H 4
/* and the field those profiles throw on the case (glow.frag): the picture
 * and a margin of GLOW_EXT around it, in the picture's coordinates */
#define GLOW_W 128
#define GLOW_H 96
#define GLOW_EXT 0.30f

struct gpu {
    int out_w, out_h;
    GLuint vao, vbo;
    GLuint prog_persist, prog_blur, prog_composite;
    GLuint tex_tube, tex_chassis;
    int tube_w, tube_h, chassis_w, chassis_h;
    GLuint fbo_persist[2], tex_persist[2];
    int persist_cur;
    int persist_w, persist_h; /* the targets' size: the tube texture's */
    GLuint fbo_bloom, tex_bloom, fbo_bloom2, tex_bloom2;
    GLuint fbo_edge, tex_edge;
    GLuint fbo_glow, tex_glow;
    GLuint fbo_edgef[2], tex_edgef[2]; /* the eased profiles, this frame and last */
    int edgef_cur;
    GLuint prog_edge, prog_ease, prog_glow;
    GLuint fbo_burn[2], tex_burn[2];
    int burn_cur;
    GLuint prog_burn, prog_overlay, tex_overlay;
    GLuint prog_splash, tex_splash;
    int spl_w, spl_h;
    GLuint prog_fade;
    int ov_w, ov_h;
    double last_t;
    int have_last;
    float led[4][4], led_col[4][3], led_on[4], led_round[4], led_clip[4];
    float seg[4], seg_on, seg_lvl[21];
    float raster_h, raster_v, tube_gain;
};

/* the settings panel, straight alpha over the finished frame */

static GLuint mkshader(GLenum t, const char *src) {
    GLuint s = glCreateShader(t);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        gpu_logf("shader compile failed:\n%s", log);
    }
    return s;
}
static GLuint mkprog(const char *fs) {
    GLuint p = glCreateProgram();
    GLuint v = mkshader(GL_VERTEX_SHADER, shader_quad_vert), f = mkshader(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        gpu_logf("shader link failed:\n%s", log);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}
/* Every target is CLEARED the moment it exists.  A texture created with
 * NULL data has undefined contents, and on Linux/AMD that means whatever
 * the previous owner of that VRAM left there - typically 8-bit desktop
 * pixels.  Read as half floats, an opaque BGRA pixel puts 0xFF in the high
 * byte of every second value: NaN or -Inf, in the same channel of every
 * texel.  Persistence and burn-in feed back through max() and mix(), so a
 * NaN in there never leaves; the composite then clamps it to 0, and one
 * channel of the whole picture simply vanishes - green, on the machine
 * this was seen on - while any stray +Inf shows as a speckle that never
 * decays.  Apple and Windows hand out zeroed memory, which is why it
 * never showed there. */
static void mktarget(GLuint *fbo, GLuint *tex, int w, int h) {
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* A target that the bloom and spill passes shrink FROM samples through a
 * mip chain, so that a texel of the small picture is the average of all
 * the source under it.  Five taps of the blur over a 4x or 7x reduction
 * miss most of the source, and a bright line scrolling through the picture
 * drifted in and out of the taps: the light on the case moved in steps
 * while the picture moved smoothly. */
static void mipmapped(GLuint tex) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
}
static void remip(GLuint tex) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glGenerateMipmap(GL_TEXTURE_2D);
}

gpu *gpu_create(int w, int h) {
    if (!gl_load()) {
        gpu_logf("this GL context is missing functions DXM needs");
        return NULL;
    }
    gpu *g = calloc(1, sizeof *g);
    if (!g) {
        gpu_logf("out of memory for the GPU state");
        return NULL;
    }
    g->out_w = w;
    g->out_h = h;
    g->raster_h = g->raster_v = g->tube_gain = 1.0f;
    static const float quad[] = {-1, -1, 3, -1, -1, 3};
    glGenVertexArrays(1, &g->vao);
    glBindVertexArray(g->vao);
    glGenBuffers(1, &g->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    g->prog_persist = mkprog(shader_persist_frag);
    g->prog_blur = mkprog(shader_blur_frag);
    g->prog_edge = mkprog(shader_edge_frag);
    g->prog_ease = mkprog(shader_ease_frag);
    g->prog_glow = mkprog(shader_glow_frag);
    g->prog_composite = mkprog(shader_composite_frag);
    g->prog_burn = mkprog(shader_burn_frag);
    g->prog_overlay = mkprog(shader_overlay_frag);
    g->prog_splash = mkprog(shader_splash_frag);
    g->prog_fade = mkprog(shader_fade_frag);
    glGenTextures(1, &g->tex_splash);
    glBindTexture(GL_TEXTURE_2D, g->tex_splash);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenTextures(1, &g->tex_tube);
    glBindTexture(GL_TEXTURE_2D, g->tex_tube);
    /* LINEAR, but the shader snaps to texel centres when sharp() is off,
     * which reproduces NEAREST exactly - so the filter never has to change
     * between the DOS screen and a running game. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenTextures(1, &g->tex_chassis);
    glBindTexture(GL_TEXTURE_2D, g->tex_chassis);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g->persist_w = PERSIST_W;
    g->persist_h = PERSIST_H;
    mktarget(&g->fbo_persist[0], &g->tex_persist[0], PERSIST_W, PERSIST_H);
    mktarget(&g->fbo_persist[1], &g->tex_persist[1], PERSIST_W, PERSIST_H);
    mktarget(&g->fbo_bloom, &g->tex_bloom, BLOOM_W, BLOOM_H);
    mktarget(&g->fbo_bloom2, &g->tex_bloom2, BLOOM_W, BLOOM_H);
    mktarget(&g->fbo_edge, &g->tex_edge, EDGE_W, EDGE_H);
    mktarget(&g->fbo_glow, &g->tex_glow, GLOW_W, GLOW_H);
    mktarget(&g->fbo_edgef[0], &g->tex_edgef[0], EDGE_W, EDGE_H);
    mktarget(&g->fbo_edgef[1], &g->tex_edgef[1], EDGE_W, EDGE_H);
    mktarget(&g->fbo_burn[0], &g->tex_burn[0], PERSIST_W, PERSIST_H);
    mktarget(&g->fbo_burn[1], &g->tex_burn[1], PERSIST_W, PERSIST_H);
    mipmapped(g->tex_persist[0]);
    mipmapped(g->tex_persist[1]);
    glGenTextures(1, &g->tex_overlay);
    glBindTexture(GL_TEXTURE_2D, g->tex_overlay);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return g;
}
void gpu_destroy(gpu *g) {
    if (g)
        free(g);
}
void gpu_resize(gpu *g, int w, int h) {
    g->out_w = w;
    g->out_h = h;
}
void gpu_set_led(gpu *g, int idx, float x, float y, float w, float h, float on, float r, float gr,
                 float b, int round, float clip) {
    if (idx < 0 || idx > 3)
        return;
    g->led[idx][0] = x;
    g->led[idx][1] = y;
    g->led[idx][2] = w;
    g->led[idx][3] = h;
    g->led_col[idx][0] = r;
    g->led_col[idx][1] = gr;
    g->led_col[idx][2] = b;
    g->led_on[idx] = on;
    g->led_round[idx] = round ? 1.0f : 0.0f;
    g->led_clip[idx] = clip;
}

void gpu_set_segdisp(gpu *g, float x, float y, float w, float h, const float lvl[21], float on) {
    g->seg[0] = x;
    g->seg[1] = y;
    g->seg[2] = w;
    g->seg[3] = h;
    memcpy(g->seg_lvl, lvl, sizeof g->seg_lvl);
    g->seg_on = on;
}

void gpu_set_tube_power(gpu *g, float h, float v, float gain) {
    g->raster_h = h;
    g->raster_v = v;
    g->tube_gain = gain;
}

void gpu_set_chassis(gpu *g, const uint8_t *rgba, int w, int h) {
    g->chassis_w = w;
    g->chassis_h = h;
    glBindTexture(GL_TEXTURE_2D, g->tex_chassis);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}
void gpu_patch_chassis(gpu *g, int x, int y, int w, int h, const uint8_t *rgba) {
    if (!rgba || w <= 0 || h <= 0)
        return;
    /* clip to the texture - a knob near an edge on a tiny window - while
     * keeping the patch's own row stride */
    int stride = w, sx = 0, sy = 0;
    if (x < 0) {
        sx = -x;
        w += x;
        x = 0;
    }
    if (y < 0) {
        sy = -y;
        h += y;
        y = 0;
    }
    if (x + w > g->chassis_w)
        w = g->chassis_w - x;
    if (y + h > g->chassis_h)
        h = g->chassis_h - y;
    if (w <= 0 || h <= 0)
        return;
    glBindTexture(GL_TEXTURE_2D, g->tex_chassis);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE,
                    rgba + ((size_t)sy * (size_t)stride + (size_t)sx) * 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}
/* Give an existing target new storage at a new size, cleared: the
 * framebuffer keeps its attachment, only the texels change. */
static void retarget(GLuint fbo, GLuint tex, int w, int h) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
void gpu_set_tube(gpu *g, const uint8_t *rgb, int w, int h) {
    if (w != g->persist_w || h != g->persist_h) {
        /* a mode change: the phosphor's memory restarts at this size, black,
         * as the picture did on a real tube while the monitor re-synced */
        for (int i = 0; i < 2; i++) {
            retarget(g->fbo_persist[i], g->tex_persist[i], w, h);
            remip(g->tex_persist[i]);
            retarget(g->fbo_burn[i], g->tex_burn[i], w, h);
        }
        g->persist_w = w;
        g->persist_h = h;
    }
    g->tube_w = w;
    g->tube_h = h;
    glBindTexture(GL_TEXTURE_2D, g->tex_tube);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
}
static void pass(gpu *g, GLuint prog, GLuint fbo, int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glUseProgram(prog);
    glBindVertexArray(g->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}
void gpu_draw(gpu *g, float tx, float ty, float tw, float th, const gpu_knobs *k, double t) {
    float dt = g->have_last ? (float)(t - g->last_t) : 1.0f / 60.0f;
    if (dt <= 0.0f || dt > 0.25f)
        dt = 1.0f / 60.0f;
    g->last_t = t;
    g->have_last = 1;
    int prev = g->persist_cur, cur = 1 - prev;
    /* pass 1: persistence */
    glUseProgram(g->prog_persist);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g->tex_tube);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g->tex_persist[prev]);
    glUniform1i(glGetUniformLocation(g->prog_persist, "src"), 0);
    glUniform1i(glGetUniformLocation(g->prog_persist, "prev"), 1);
    glUniform1f(glGetUniformLocation(g->prog_persist, "dt"), dt);
    glUniform1f(glGetUniformLocation(g->prog_persist, "persist"), k->persistence);
    pass(g, g->prog_persist, g->fbo_persist[cur], g->persist_w, g->persist_h);
    remip(g->tex_persist[cur]);
    g->persist_cur = cur;
    /* the picture's light at its edges, for the case */
    glUseProgram(g->prog_edge);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g->tex_persist[cur]);
    glUniform1i(glGetUniformLocation(g->prog_edge, "src"), 0);
    glUniform1f(glGetUniformLocation(g->prog_edge, "reach"), 0.18f);
    pass(g, g->prog_edge, g->fbo_edge, EDGE_W, EDGE_H);
    /* and a little inertia: the plastic's light eases toward the picture
     * over a tenth of a second, in and out alike, rather than following
     * it frame by frame */
    {
        int ep = g->edgef_cur, ec = 1 - ep;
        glUseProgram(g->prog_ease);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g->tex_edge);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g->tex_edgef[ep]);
        glUniform1i(glGetUniformLocation(g->prog_ease, "src"), 0);
        glUniform1i(glGetUniformLocation(g->prog_ease, "prev"), 1);
        glUniform1f(glGetUniformLocation(g->prog_ease, "dt"), dt);
        glUniform1f(glGetUniformLocation(g->prog_ease, "tau"), 0.10f);
        pass(g, g->prog_ease, g->fbo_edgef[ec], EDGE_W, EDGE_H);
        g->edgef_cur = ec;
    }
    /* the field: every point of every edge a source, summed over the case */
    glUseProgram(g->prog_glow);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g->tex_edgef[g->edgef_cur]);
    glUniform1i(glGetUniformLocation(g->prog_glow, "edgesrc"), 0);
    glUniform1f(glGetUniformLocation(g->prog_glow, "lambda"), 0.10f);
    glUniform1f(glGetUniformLocation(g->prog_glow, "ext"), GLOW_EXT);
    glUniform1f(glGetUniformLocation(g->prog_glow, "aspect"),
                (tw * (float)g->out_w) / fmaxf(th * (float)g->out_h, 1.0f));
    pass(g, g->prog_glow, g->fbo_glow, GLOW_W, GLOW_H);
    /* burn-in: a much slower average of the same signal */
    {
        int bp = g->burn_cur, bc = 1 - bp;
        glUseProgram(g->prog_burn);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g->tex_persist[cur]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g->tex_burn[bp]);
        glUniform1i(glGetUniformLocation(g->prog_burn, "src"), 0);
        glUniform1i(glGetUniformLocation(g->prog_burn, "prev"), 1);
        glUniform1f(glGetUniformLocation(g->prog_burn, "dt"), dt);
        glUniform1f(glGetUniformLocation(g->prog_burn, "rate"), 28.0f);
        pass(g, g->prog_burn, g->fbo_burn[bc], g->persist_w, g->persist_h);
        g->burn_cur = bc;
    }
    /* pass 4a: bloom downsample+blur (fixed internal res, SPEC §6.7) */
    glUseProgram(g->prog_blur);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g->tex_persist[cur]);
    glUniform1i(glGetUniformLocation(g->prog_blur, "src"), 0);
    glUniform2f(glGetUniformLocation(g->prog_blur, "dir"), 0.9f / BLOOM_W, 0);
    pass(g, g->prog_blur, g->fbo_bloom, BLOOM_W, BLOOM_H);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g->tex_bloom);
    glUniform2f(glGetUniformLocation(g->prog_blur, "dir"), 0, 0.9f / BLOOM_H);
    pass(g, g->prog_blur, g->fbo_bloom2, BLOOM_W, BLOOM_H);
    /* A single pass at this resolution still carries the glyph shapes - a
     * character is a few bloom texels across, so the kernel cannot round it
     * off.  Two more, wider passes turn the glow into a soft halo that no
     * longer traces the letterforms. */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g->tex_bloom2);
    glUniform2f(glGetUniformLocation(g->prog_blur, "dir"), 0.55f / BLOOM_W, 0);
    pass(g, g->prog_blur, g->fbo_bloom, BLOOM_W, BLOOM_H);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g->tex_bloom);
    glUniform2f(glGetUniformLocation(g->prog_blur, "dir"), 0, 0.55f / BLOOM_H);
    pass(g, g->prog_blur, g->fbo_bloom2, BLOOM_W, BLOOM_H);
    /* composite */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g->out_w, g->out_h);
    glUseProgram(g->prog_composite);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g->tex_persist[cur]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g->tex_bloom2);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, g->tex_chassis);
    GLuint p = g->prog_composite;
    glUniform1i(glGetUniformLocation(p, "tube"), 0);
    glUniform1i(glGetUniformLocation(p, "bloom"), 1);
    glUniform1i(glGetUniformLocation(p, "chassis"), 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, g->tex_glow);
    glUniform1i(glGetUniformLocation(p, "glowsrc"), 3);
    glUniform1f(glGetUniformLocation(p, "u_glow_ext"), GLOW_EXT);
    glUniform4f(glGetUniformLocation(p, "rect"), tx, ty, tw, th);
    glUniform2f(glGetUniformLocation(p, "outsize"), (float)g->out_w, (float)g->out_h);
    glUniform1f(glGetUniformLocation(p, "warp"), k->warp);
    glUniform1f(glGetUniformLocation(p, "bright"), k->brightness);
    glUniform1f(glGetUniformLocation(p, "contrast"), k->contrast);
    glUniform1f(glGetUniformLocation(p, "ambient"), k->ambient);
    glUniform1f(glGetUniformLocation(p, "scan"), k->scan);
    glUniform1f(glGetUniformLocation(p, "margin"), k->margin);
    glUniform1f(glGetUniformLocation(p, "aper_r"), k->aperture_r);
    glUniform4fv(glGetUniformLocation(p, "led"), 4, &g->led[0][0]);
    glUniform3fv(glGetUniformLocation(p, "ledcol"), 4, &g->led_col[0][0]);
    glUniform1fv(glGetUniformLocation(p, "ledon"), 4, g->led_on);
    glUniform1fv(glGetUniformLocation(p, "ledround"), 4, g->led_round);
    glUniform1fv(glGetUniformLocation(p, "ledclip"), 4, g->led_clip);
    glUniform4fv(glGetUniformLocation(p, "seg_rect"), 1, g->seg);
    glUniform1fv(glGetUniformLocation(p, "seglvl"), 21, g->seg_lvl);
    glUniform1f(glGetUniformLocation(p, "seg_on"), g->seg_on);
    glUniform4f(glGetUniformLocation(p, "seg_geom"), SEG_DH, SEG_WR, SEG_PITCH, SEG_T);
    glUniform2f(glGetUniformLocation(p, "seg_lean"), SEG_SLANT, SEG_GAP);
    glUniform2f(glGetUniformLocation(p, "u_raster"), g->raster_h, g->raster_v);
    glUniform1f(glGetUniformLocation(p, "u_gain"), g->tube_gain);
    glUniform1f(glGetUniformLocation(p, "crt_lines"), (float)k->crt_lines);
    glUniform1f(glGetUniformLocation(p, "crt_cols"), (float)k->crt_cols);
    glUniform2f(glGetUniformLocation(p, "texsize"), (float)g->tube_w, (float)g->tube_h);
    glUniform2f(glGetUniformLocation(p, "texelpx"),
                (float)g->tube_w / fmaxf(tw * (float)g->out_w, 1.0f),
                (float)g->tube_h / fmaxf(th * (float)g->out_h, 1.0f));
    glUniform1f(glGetUniformLocation(p, "u_sharp"), k->sharp_text);
    glUniform1f(glGetUniformLocation(p, "u_overscan"), k->overscan);
    glUniform1f(glGetUniformLocation(p, "u_shoulder"), DXM_BEZEL_BAND);
    glUniform1f(glGetUniformLocation(p, "u_shoulder_r"), DXM_BEZEL_R_MID);
    glUniform1f(glGetUniformLocation(p, "u_shoulder_warp"), DXM_WARP * DXM_BEZEL_R_MID_WARP);
    glUniform1f(glGetUniformLocation(p, "u_dish_rin"), DXM_BEZEL_R_IN);
    glUniform1f(glGetUniformLocation(p, "u_dish_warp"), DXM_WARP);
    glUniform1f(glGetUniformLocation(p, "u_fillet"), DXM_FILLET_START);
    glUniform1f(glGetUniformLocation(p, "vgrid"), k->vgrid);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, g->tex_burn[g->burn_cur]);
    glUniform1i(glGetUniformLocation(p, "burnsrc"), 5);
    glUniform1f(glGetUniformLocation(p, "time"), (float)t);
    glUniform1f(glGetUniformLocation(p, "u_bloom"), k->bloom);
    glUniform1f(glGetUniformLocation(p, "u_burn"), k->burn_in);
    glUniform1f(glGetUniformLocation(p, "u_noise"), k->noise);
    glUniform1f(glGetUniformLocation(p, "u_jitter"), k->jitter);
    glUniform1f(glGetUniformLocation(p, "u_glowline"), k->glow_line);
    glUniform1f(glGetUniformLocation(p, "u_flicker"), k->flicker);
    glUniform1f(glGetUniformLocation(p, "u_hsync"), k->hsync);
    glUniform1f(glGetUniformLocation(p, "u_rgb"), k->rgb_shift);
    glUniform1f(glGetUniformLocation(p, "u_chassis"), k->chassis_glow);
    glBindVertexArray(g->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}
uint8_t *gpu_readback(gpu *g, int *w, int *h) {
    *w = g->out_w;
    *h = g->out_h;
    uint8_t *px = malloc((size_t)g->out_w * g->out_h * 3);
    if (!px)
        return NULL;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, g->out_w, g->out_h, GL_RGB, GL_UNSIGNED_BYTE, px);
    return px;
}

void gpu_set_splash(gpu *g, const uint8_t *rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) {
        g->spl_w = 0;
        g->spl_h = 0;
        return;
    }
    g->spl_w = w;
    g->spl_h = h;
    glBindTexture(GL_TEXTURE_2D, g->tex_splash);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}
void gpu_draw_splash(gpu *g, float alpha) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g->out_w, g->out_h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (g->spl_w <= 0 || alpha <= 0.0f)
        return;
    /* fit to a share of the width, but never let a wide display push it
     * past a share of the height */
    float ow = (float)g->out_w, oh = (float)g->out_h;
    float w = ow * 0.56f, h = w * (float)g->spl_h / (float)g->spl_w;
    if (h > oh * 0.34f) {
        h = oh * 0.34f;
        w = h * (float)g->spl_w / (float)g->spl_h;
    }
    float rx = (ow - w) * 0.5f / ow, ry = (oh - h) * 0.5f / oh;
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); /* premultiplied */
    glUseProgram(g->prog_splash);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g->tex_splash);
    glUniform1i(glGetUniformLocation(g->prog_splash, "src"), 0);
    glUniform4f(glGetUniformLocation(g->prog_splash, "rect"), rx, ry, w / ow, h / oh);
    glUniform1f(glGetUniformLocation(g->prog_splash, "alpha"), alpha);
    glBindVertexArray(g->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);
}
void gpu_draw_fade(gpu *g, float a) {
    if (a <= 0.0f)
        return;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g->out_w, g->out_h);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g->prog_fade);
    glUniform1f(glGetUniformLocation(g->prog_fade, "a"), a > 1.0f ? 1.0f : a);
    glBindVertexArray(g->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);
}
void gpu_set_overlay(gpu *g, const uint8_t *rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) {
        g->ov_w = 0;
        g->ov_h = 0;
        return;
    }
    g->ov_w = w;
    g->ov_h = h;
    glBindTexture(GL_TEXTURE_2D, g->tex_overlay);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}
void gpu_draw_overlay(gpu *g) {
    if (g->ov_w <= 0)
        return;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g->out_w, g->out_h);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g->prog_overlay);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g->tex_overlay);
    glUniform1i(glGetUniformLocation(g->prog_overlay, "src"), 0);
    glBindVertexArray(g->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);
}

const char *gpu_describe(void) {
    static char buf[512];
    const char *v = (const char *)glGetString(GL_VENDOR);
    const char *r = (const char *)glGetString(GL_RENDERER);
    const char *ver = (const char *)glGetString(GL_VERSION);
    snprintf(buf, sizeof buf, "%s / %s / GL %s", v ? v : "?", r ? r : "?", ver ? ver : "?");
    return buf;
}
const char *gpu_missing(void) {
    return gl_missing;
}
