#version 330 core
/* ---- passes 2..7 fused: curvature, beam/mask, bloom add, glass, spill ---- */
in vec2 uv; out vec4 o;
uniform sampler2D tube, bloom, chassis, spillsrc, roomsrc, burnsrc;
uniform vec4  rect;        // tube x,y,w,h in 0..1 output space
uniform vec2  outsize;
uniform float warp, bright, contrast, ambient, scan, margin;
uniform float u_bloom, u_burn, u_noise, u_jitter, u_glowline;
uniform float u_flicker, u_hsync, u_rgb, u_chassis;
uniform float aper_r, time;
uniform vec4  led[2];
uniform vec3  ledcol[2];
uniform float ledon[2];
uniform float ledround[2];
uniform float ledclip[2];
uniform vec2  u_raster;         // deflection: fraction of the raster drawn
uniform float u_gain;           // beam drive
uniform float crt_lines, crt_cols, vgrid;
uniform vec2  texsize, texelpx;   // tube texture, and one output pixel
uniform float u_sharp;            // 1 on the DOS screen, 0 in a game
uniform float u_overscan;         // picture overflow, in OUTPUT pixels
uniform float u_shoulder, u_shoulder_r, u_shoulder_warp; // the bezel's shoulder (crt.h)
uniform float u_dish_rin, u_dish_warp, u_fillet;         // the dish, and where it rolls off

// A sin() hash breaks down once its argument gets large: the range
// reduction inside sin() loses precision, and what comes out is not
// noise but coherent bands running along dot(p,k)=const - a sharp
// diagonal sweeping the tube as the time offset grows.  At full-screen
// pixel coordinates the argument is already in the millions.  This is
// the integer-style hash instead: no trig, no magnitude sensitivity.
float hash(vec2 p){
  vec3 q = fract(vec3(p.xyx) * 0.1031);
  q += dot(q, q.yzx + 33.33);
  return fract((q.x + q.y) * q.z);
}

// Two sampling regimes, because the two sources want opposite things.
// TEXT (8x16 glyphs at ~3x) needs each source pixel to have an edge
// exactly one OUTPUT pixel wide, or nearest-neighbour rounds strokes to
// whole pixels unevenly and the same glyph gets a fat left edge here
// and a fat right edge there.  GAME ART (320x200 at ~6x) wants honest
// hard pixels - any smoothing there just reads as lost resolution.
// The edge width is passed in rather than taken from fwidth(), which is
// undefined inside the non-uniform control flow this runs in.
vec2 tap(vec2 c){
  vec2 p = c*texsize;
  vec2 i = floor(p) + 0.5;
  if (u_sharp < 0.5) return i/texsize;      // exactly nearest
  vec2 d = p - i;
  vec2 w = max(texelpx*0.5, vec2(1e-5));
  return (i + clamp(d/w, -1.0, 1.0)*0.5) / texsize;
}

vec2 barrel_k(vec2 p, float k){
  vec2 c = p*2.0-1.0;
  float r2 = dot(c,c);
  c *= 1.0 + k*0.30*r2;         // DXM_WARP_K  (crt.h)
  c /= 1.0 + k*0.32;            // DXM_WARP_NORM (crt.h) - keep in sync
  return c*0.5+0.5;
}
vec2 barrel(vec2 p){ return barrel_k(p, warp); }
// Signed distance to the bezel's shoulder, in output pixels, built exactly
// as the chassis builds it (bezel.c shoulder_sd): the band's outer curve
// with its own corner radius, following the barrel only partly.  The
// chassis is drawn once with the default curvature, so this uses that too,
// not the knob.
float warped_rr_px(vec2 t, float r, float grow, float k){
  vec2 b = barrel_k(t, k);
  vec2 halfpx = rect.zw*outsize*0.5 + grow;
  vec2 apx = abs(b*2.0-1.0)*rect.zw*outsize*0.5;
  vec2 qq = apx - (halfpx - r);
  return (qq.x>0.0 && qq.y>0.0) ? length(qq)-r : max(apx.x-halfpx.x, apx.y-halfpx.y);
}
// How far past the dish's visible edge a point is, in output pixels: 0 on
// the dish, growing beyond.  The chassis rolls the last stretch of the
// dish into the shoulder from u_fillet of the way across, and that roll is
// the edge the eye sees, so the light drops from there, not from the
// shoulder curve itself.
// .x: how far past the dish's visible edge, in output pixels, 0 on the dish.
// .y: how far across the dish, 0 at the glass, 1 at the roll.
vec2 dish(vec2 t){
  float th = rect.w*outsize.y;
  float din  = warped_rr_px(t, u_dish_rin*th, 0.0, u_dish_warp);          // the aperture
  float dout = warped_rr_px(t, u_shoulder_r*th, u_shoulder*th, u_shoulder_warp); // the shoulder
  float edge = u_fillet*(din - dout);
  return vec2(max(din - edge, 0.0), clamp(din/max(edge, 1e-3), 0.0, 1.0));
}
// beam profile INTEGRATED over the pixel footprint, so scanlines do not
// alias when tube height is not a multiple of crt_lines (SPEC 6.4).
float beam(float y, float px){
  float l = y*crt_lines;
  float d = abs(fract(l)-0.5)*2.0;
  float w = clamp(px*crt_lines, 0.6, 4.0);
  float g = exp(-d*d*3.0/ (w*0.55));
  return mix(1.0, g, scan);
}
float column(float x, float px){
  float c = x*crt_cols;
  float d = abs(fract(c)-0.5)*2.0;
  float w = clamp(px*crt_cols, 0.6, 4.0);
  float g = exp(-d*d*3.0/ (w*0.62));
  return mix(1.0, g, vgrid);
}
void main(){
  vec4 chas = texture(chassis, vec2(uv.x, 1.0-uv.y));
  vec3 plastic = chas.rgb;
  // alpha carries how much this surface faces the tube: the reveal dish
  // is angled at the glass and catches far more light than the flat case
  float facing = 0.22 + 1.55*chas.a;
  plastic = pow(plastic, vec3(2.2));
  vec2 t = (uv - rect.xy) / rect.zw;
  vec3 col = vec3(0.0);
  float inside = 0.0;
  if (t.x>-0.15 && t.x<1.15 && t.y>-0.15 && t.y<1.15) {
    // ---- deflection errors, applied BEFORE the barrel so they behave
    // like real deflection rather than like a moving texture ----
    // JITTER: the whole raster twitching frame to frame
    vec2 jit = vec2(hash(vec2(floor(time*60.0),1.0))-0.5,
                    hash(vec2(floor(time*60.0),7.0))-0.5);
    t += jit * u_jitter * 0.010;
    // HSYNC: each LINE starts at slightly the wrong place, drifting
    float lineno = floor(t.y*crt_lines);
    float hs = (hash(vec2(lineno, floor(time*24.0)))-0.5)
             + 0.6*sin(t.y*38.0 + time*5.0);
    t.x += hs * u_hsync * 0.012;

    vec2 b = barrel(t);
    // The glass must be cut to the SAME rounded box the chassis carved,
    // evaluated in the same warped space.
    vec2 halfpx = rect.zw*outsize*0.5;
    vec2 apx    = abs(b*2.0-1.0)*halfpx;
    vec2 qq     = apx - (halfpx - aper_r);
    float asd   = (qq.x>0.0 && qq.y>0.0) ? length(qq)-aper_r
                                         : max(apx.x-halfpx.x, apx.y-halfpx.y);
    // The GLASS REGION opens outward by the overscan, so lit content
    // actually reaches under the moulding.  Scaling the sample alone did
    // nothing visible: the boundary stayed exactly where it was.
    if (asd <= u_overscan) {
      inside = 1.0;
      // Overscan: push the picture a little PAST the aperture so its
      // edge is tucked under the moulding instead of ending exactly at
      // it.  Real sets always overscanned; it also means no seam can
      // show between the last lit pixel and the dish.
      vec2 e = u_overscan / max(rect.zw*outsize*0.5, vec2(1.0));
      vec2 sb = (b - 0.5)/(1.0 + e)/max(1.0 - 2.0*margin, 1e-3) + 0.5;
      // the deflection: a smaller raster means the same picture drawn
      // in less of the glass, with the rest unlit
      sb = 0.5 + (sb - 0.5)/max(u_raster, vec2(1e-3));
      vec2 cb = clamp(sb, 0.0, 1.0);
      vec2 od = max(max(-sb, sb - vec2(1.0)), vec2(0.0));
      float outd = length(od);          // 0 inside the raster
      float px = 1.0/max(rect.w*outsize.y,1.0);
      vec3 s = vec3(0.0);
      float bm = 1.0;
      if (outd < 1e-6) {
        // RGB SHIFT: the three guns landing at slightly different places,
        // splayed outward from the centre the way real convergence errors
        // grow toward the edges of the tube
        vec2 ctr = sb - 0.5;
        vec2 sep = ctr * u_rgb * 0.020;
        s.r = texture(tube, tap(vec2(sb.x+sep.x, 1.0-(sb.y+sep.y)))).r;
        s.g = texture(tube, tap(vec2(sb.x,       1.0- sb.y      ))).g;
        s.b = texture(tube, tap(vec2(sb.x-sep.x, 1.0-(sb.y-sep.y)))).b;
        s = (s - 0.5)*contrast + 0.5 + (bright-0.5)*0.6;
        bm = beam(sb.y, px);
        bm *= column(sb.x, 1.0/max(rect.z*outsize.x,1.0));
      }
      // Aperture-grille triad locked to the SOURCE pixel grid: one full
      // R|G|B triad per source pixel.  Pinning it to output pixels on a
      // 3px period meant every character cell landed on a different
      // sub-phase of the stripes, so the same glyph came out with a
      // bright left edge in one column and a bright right edge in the
      // next, with colour fringing that changed across the screen.
      float gx = fract(cb.x*crt_cols);
      vec3 mask = vec3(0.94);
      mask.r += 0.20*step(gx,0.333); mask.g += 0.20*step(0.333,gx)*step(gx,0.666);
      mask.b += 0.20*step(0.666,gx);
      col = max(s,0.0)*bm*mask*u_gain;

      // BURN-IN: the slow accumulator, added as a faint ghost
      vec3 burn = texture(burnsrc, vec2(cb.x,1.0-cb.y)).rgb;
      col += burn * u_burn * 0.55 * mask * u_gain;

      // BLOOM: light bleeding between lit pixels
      vec3 bl = texture(bloom, vec2(cb.x,1.0-cb.y)).rgb;
      col += bl*u_bloom*0.34*exp(-outd*11.0)*mask*u_gain;

      // GLOW LINE: the bright band drifting slowly down the tube, left by
      // the refresh beating against the eye
      // sb.y == 1 is the TOP of the picture, so the phase must ADVANCE
      // with time for the band to drift downward, the way the refresh
      // beating against mains actually rolls.
      float gl = fract(cb.y*0.5 + time*0.10);
      col += vec3(0.55,0.85,1.0) * u_glowline * 0.055
           * exp(-pow((gl-0.5)/0.06, 2.0));

      vec2 c2 = b*2.0-1.0;
      col *= 1.0 - 0.30*dot(c2,c2)*0.5;
      col += ambient*0.016*vec3(0.9,0.95,1.0);
      float sheen = smoothstep(0.42,0.0, distance(b, vec2(0.28,0.16)));
      col += sheen*(0.008+0.022*ambient);

      // STATIC NOISE: snow on the phosphor
      // the animation offset is WRAPPED - letting it grow without bound
      // walks the hash input off into the range where any hash starts
      // to lose resolution
      float n = hash(floor(uv*outsize)
                     + vec2(mod(time*371.0,977.0), mod(time*137.0,743.0)))
              - 0.5;
      col += n * u_noise * 0.16;

      // FLICKER: the mains-rate brightness wobble of an old set
      float fl = 1.0 + u_flicker*0.10*(sin(time*47.0)*0.6 + sin(time*113.0)*0.4)
               + u_flicker*0.05*(hash(vec2(floor(time*50.0),3.0))-0.5);
      col *= fl;
    }
  }
  // bezel spill: heavily blurred tube lights the surrounding plastic (6.6)
  vec2 sp = clamp((uv-rect.xy)/rect.zw, 0.0, 1.0);
  vec3 spill = texture(spillsrc, vec2(sp.x,1.0-sp.y)).rgb;
  vec2 outv = max(max(rect.xy-uv, uv-(rect.xy+rect.zw)), vec2(0.0));
  outv.x *= outsize.x/outsize.y;   // uv.x and uv.y are not the same distance
  float d = length(outv)/max(rect.w,1e-4);      // in tube-heights
  // The picture's light on the case: the dish faces the glass and takes it
  // in full, then from the shoulder outward it drops away sharply - past
  // the rim the moulding turns from the tube and the picture is a small
  // source seen at a grazing angle.
  // The shoulder is where the chassis put it, not a fixed distance from
  // the picture.
  // Across the dish the light eases away from the glass - the plastic is
  // lit by a source it is moving away from, a haze rather than a strip -
  // and at the roll it drops.
  vec2 dsh = dish((uv - rect.xy)/rect.zw);
  float across = 1.0 - 0.55*smoothstep(0.0, 1.0, dsh.y);
  float fall = across * exp(-dsh.x/max(rect.w*outsize.y,1.0)*36.0);
  // ambient is PERCEPTUAL: the sRGB encode at the end compresses linear
  // factors toward 1, so a linear ramp here looks nearly flat.
  float amb = pow(0.16 + 0.98*ambient, 2.2);
  // What the tube throws into the ROOM: a near-average of the whole
  // picture, lighting the entire chassis and falling off only slowly.
  vec3 room = texture(roomsrc, vec2(0.5,0.5)).rgb;
  float roomfall = exp(-d*1.1);
  vec3 lit = plastic*amb
           + spill*fall*u_chassis*facing*(0.30+0.26*(1.0-ambient))
           + room*roomfall*u_chassis*facing*(0.34+0.40*(1.0-ambient));
  vec3 fin = mix(lit, col, inside);
  for (int i = 0; i < 2; ++i) {
    if (ledon[i] <= 0.001) continue;
    vec2 lc = led[i].xy + led[i].zw*0.5;
    vec2 dd  = (uv - lc) / (led[i].zw*0.5);
    float m = mix(max(abs(dd.x),abs(dd.y)), length(dd), ledround[i]);
    float lens = 1.0 - smoothstep(0.82, 1.02, m);
    // A frosted light pipe does not emit as a flat rectangle: it is
    // brightest over the die and carries the same striations the unlit
    // face shows.  Without this the LED lights up as a colour swatch.
    lens *= mix(1.0,
                (0.80 + 0.30*exp(-dot(dd,dd)*1.30))
                * (1.0 + 0.055*sin(dd.y*9.0)),
                1.0 - ledround[i]);
    // A lit LED throws real light onto the plastic around it: a tight
    // core plus a much wider, softer halo.  A single narrow falloff
    // made the lens glow but left the case around it untouched.
    float d2  = dot(dd,dd);
    float core  = exp(-d2*0.75);
    float wide  = exp(-d2*0.10);
    float bleed = core*0.30 + wide*0.16;
    // What sits above the LED - the power cap - is a separate face at a
    // different height.  Its underside shadows the light, so the bleed
    // stops there; a lamp does not light the front of a button above it.
    bleed *= 1.0 - smoothstep(ledclip[i] - 1.5/outsize.y, ledclip[i], uv.y);
    fin += ledcol[i] * (lens*1.50 + bleed) * ledon[i];
  }
  o = vec4(pow(max(fin,0.0), vec3(1.0/2.2)), 1.0);
}
