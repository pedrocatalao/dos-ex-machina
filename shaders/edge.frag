#version 330 core
/* The picture's light at its four edges, for the case around it.  One
 * profile per edge: each point of it is the picture integrated inward
 * from that edge, the near rows counting and the far ones not, so a
 * bright line just inside the bottom edge lights the bottom dish under it
 * and little else, and lights the sides at its height as it climbs.
 * Row 0 is the bottom edge along x, row 1 the top, row 2 the left edge
 * along y with 0 at the bottom, row 3 the right.  A white picture reads 1. */
in vec2 uv; out vec4 o;
uniform sampler2D src;   // the persisted picture; v = 0 is its top row
uniform float reach;     // how far in the light is gathered, of the picture
void main(){
  int side = int(uv.y*4.0);
  float a = uv.x;        // along the edge
  vec3 sum = vec3(0.0);
  float wsum = 0.0;
  const int N = 96;
  for (int n = 0; n < N; n++) {
    float d = (float(n)+0.5)/float(N);   // inward from the edge, 0..1
    float w = exp(-d/reach);
    vec2 p;
    if      (side == 0) p = vec2(a, 1.0-d);      // bottom edge, upward
    else if (side == 1) p = vec2(a, d);          // top edge, downward
    else if (side == 2) p = vec2(d, 1.0-a);      // left edge, inward
    else                p = vec2(1.0-d, 1.0-a);  // right edge, inward
    // a few rows averaged per tap: the mip chain, so motion is continuous
    sum += textureLod(src, p, 2.0).rgb * w;
    wsum += w;
  }
  o = vec4(sum/wsum, 1.0);
}
