#version 330 core
/* A little inertia: the new picture blended into the old with a time
 * constant, in seconds, so what it feeds eases toward the picture rather
 * than snapping to it each frame.  Symmetric - it eases in and out alike -
 * unlike the phosphor, which only decays. */
in vec2 uv; out vec4 o;
uniform sampler2D src, prev;
uniform float dt, tau;
void main(){
  float k = 1.0 - exp(-dt/max(tau, 0.001));
  o = vec4(mix(texture(prev, uv).rgb, texture(src, uv).rgb, k), 1.0);
}
