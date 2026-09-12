#version 330 core
/* The picture's light on the case as a field: every point along each edge
 * of the glass is a small source with the edge profile's brightness
 * (edge.frag), and every point around the picture takes the sum of them
 * all, each falling off with distance.  Soft in every direction, the
 * corners blending on their own, and at the glass exactly the profile.
 * The field covers the picture and a margin of `ext` around it, in the
 * picture's own coordinates, y up; `aspect` is the picture's width over
 * its height, so distances are in tube-heights; `lambda` is the reach of
 * a source, the one softness knob.  A white picture reads 1 at the glass. */
in vec2 uv; out vec4 o;
uniform sampler2D edgesrc;
uniform float lambda, ext, aspect;
void main(){
  vec2 q = uv*(1.0+2.0*ext) - ext;
  const int N = 64;
  float ds = 1.0/float(N);
  vec3 acc = vec3(0.0);
  for (int n = 0; n < N; n++) {
    float s = (float(n)+0.5)*ds;
    // bottom and top edges: the source at (s, 0) and (s, 1), profile rows 0 and 1
    float dx = (q.x - s)*aspect;
    float kb = exp(-length(vec2(dx, q.y))/lambda);
    float kt = exp(-length(vec2(dx, q.y-1.0))/lambda);
    acc += texture(edgesrc, vec2(s, 0.125)).rgb*kb + texture(edgesrc, vec2(s, 0.375)).rgb*kt;
    // left and right edges: the source at (0, s) and (1, s), rows 2 and 3
    float dy = q.y - s;
    float kl = exp(-length(vec2(q.x*aspect, dy))/lambda);
    float kr = exp(-length(vec2((q.x-1.0)*aspect, dy))/lambda);
    acc += (texture(edgesrc, vec2(s, 0.625)).rgb*kl + texture(edgesrc, vec2(s, 0.875)).rgb*kr)/aspect;
  }
  // an edge of sources integrates to 2*lambda at a point on it; the
  // horizontal edges are measured in x scaled by the aspect, the vertical
  // ones were divided by it above, so one normalisation serves all four
  o = vec4(acc*ds*aspect/(2.0*lambda), 1.0);
}
