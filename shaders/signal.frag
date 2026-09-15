#version 330 core
/* ---- pass 0: the monitor's input.  The PC's picture as the machine sent
 * it, and the monitor's own OSD mixed into it, the way a monitor's OSD
 * generator switched its display into the video signal ahead of the tube.
 * Everything after - persistence, burn-in, bloom, the light on the case,
 * the beam - sees this one picture, and cannot tell the two apart.
 *
 * The signal is the picture scaled up by whole numbers until it is at
 * least the OSD's raster (gpu.c), each source pixel copied exactly, so the
 * PC's picture comes through untouched and the OSD has lines enough to be
 * read.  v = 0 is the top row, here as in the passes after. */
in vec2 uv; out vec4 o;
uniform sampler2D src;   // the PC's picture
uniform sampler2D osd;   // the monitor's display, RGBA over the whole picture
uniform ivec2 srcsize;
uniform float osd_on;
void main(){
  ivec2 p = clamp(ivec2(uv*vec2(srcsize)), ivec2(0), srcsize - 1);
  vec3 c = texelFetch(src, p, 0).rgb;
  if (osd_on > 0.5) {
    vec4 d = texture(osd, uv);
    c = mix(c, d.rgb, d.a);
  }
  o = vec4(c, 1.0);
}
