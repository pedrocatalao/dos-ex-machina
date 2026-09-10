/* internal.h — shared between the files of the chassis renderer, and
 * nothing outside src/chassis.  The public interface is src/chassis.h. */
#ifndef DXM_CHASSIS_INTERNAL_H
#define DXM_CHASSIS_INTERNAL_H
#include "chassis.h"
#include "params.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef struct {
    uint8_t *px;
    int w, h;
} canvas;

/* What the assembly solves once and every part is placed against. */
typedef struct {
    float mm;             /* one millimetre, in output pixels (H/268) */
    float inset, edge;    /* the case's margins */
    float bz, hous;       /* bezel band and full housing depth */
    float gap_d;          /* the partings' depth */
    float gap_lo, gap_hi; /* the lower and upper parting */
} chassis_geom;

/* All moulded lettering shares one scale so the machine reads as one
 * product at every resolution (SPEC 6.3: shared metrics stay shared). */
extern float canvas_lbl;
/* 1: shading carries the plastic's grain; 0 while drawing a printed label */
extern int canvas_grain;

/* canvas.c */
float hash2(int x, int y, int s);
float plastic_tex(int x, int y);
float rr_sd(float px, float py, float cx, float cy, float hw, float hh, float r);
float vnoise(float x, float y, int seed);
float warped_rr_sd(const dxm_layout *L, float px, float py, float r, float grow, float warp);
void bevel(canvas *c, float x, float y, float w, float h, float t, int up);
void chamfer_ring(canvas *c, float x, float y, float w, float h, float r, float cw);
void housing_edge(canvas *c, float x, float y, float w, float h, float r, float ew, float shadow,
                  int raised, float gain);
void moulded_text(canvas *c, float x, float y, const char *s, float sc, int debossed);
void panel_gap(canvas *c, float x, float y, float w, float d);
void px_blend(canvas *c, int x, int y, int r, int g, int b, float a);
void px_set(canvas *c, int x, int y, int r, int g, int b);
void px_shade(canvas *c, int x, int y, float mul, float spec);
void rrect(canvas *c, float x, float y, float w, float h, float rad, int r, int g, int b,
           float shade_top, float shade_bot);
/* a dark join line; lit>0 adds the light flank past it (right/below),
 * lit<0 before it (left/above), 0 none */
void seam(canvas *c, float x, float y, float len, int vertical, float w, int lit);
void soft_hedge(canvas *c, float x0, float x1, float y, float span, float mul_peak, float spec_peak,
                int downward);
void soft_vedge(canvas *c, float y0, float y1, float x, float span, float mul_peak, int rightward);
void text(canvas *c, float x, float y, const char *s, float sc, int r, int g, int b);
void text_smooth(canvas *c, float x, float y, const char *s, float sc, int r, int g, int b);
void text_smooth16(canvas *c, float x, float y, const char *s, float sc, int r, int g, int b);

/* surface.c */
void surface_base(canvas *c, int W, int H);
void surface_edges(canvas *c, int W, int H, const chassis_geom *G);
void surface_finish(canvas *c, int W, int H);
void surface_monitor_recess(canvas *c, int W, int H, const chassis_geom *G);
void surface_moulding_traces(canvas *c, int W, int H, const chassis_geom *G);
void surface_top_roll(canvas *c, int W, const chassis_geom *G);
void surface_wear(canvas *c, int W, int H);
void surface_side_louvres(canvas *c, int W, int H, const chassis_geom *G);

/* bezel.c */
void bezel_cut(canvas *c, dxm_layout *L, const chassis_geom *G);
void bezel_facing_alpha(canvas *c, const dxm_layout *L, int W, int H, const chassis_geom *G);

/* parts.c */
void floppy_drive(canvas *c, float x, float y, float w, float h, float led_out[4]);
void grille_panel(canvas *c, float x, float y, float w, float h, float pitch);
void power_button(canvas *c, float px0, float pw, float mid, float mm, float band_h, dxm_layout *L);
void turbo_module(canvas *c, float x, float pw, float mid, float mm, float seg[4],
                  float btn[3][4], float mode_led[2][4]);
void vent_slot(canvas *c, float x, float y, float w, float h);
void vent_slot_r(canvas *c, float x, float y, float w, float h, float rad, float deep,
                 float rim);
void louvre_slot(canvas *c, float x, float y, float w, float h, float rad);

/* marks.c */
void badge(canvas *c, float pbx, float pby, float pbw, float pbh);
void corner_engraving(canvas *c, float cx, float cy, float w);
void engrave_field(canvas *c, float x, float y, int dw, int dh, const float *dep);
void engrave_mark(canvas *c, float cx, float cy, float w, float fill);
void sb_sticker(canvas *c, float cx, float cy, float w);

/* knobs.c */
void knob_icons(canvas *c, float bx, float by, float cx2, float cy2, float s);
void knobs_draw(canvas *c);
void knobs_layout(dxm_layout *L);
void knobs_slot(int which, float cx, float cy, float r);

#endif
