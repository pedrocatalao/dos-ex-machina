#version 330 core
/* burn-in accumulator: a very slow exponential average of the picture.  Its
 * time constant is in TENS OF SECONDS, which is what makes static content
 * (a prompt, a HUD) etch in while moving content leaves nothing. */
in vec2 uv; out vec4 o;
uniform sampler2D src, prev;
uniform float dt, rate;
void main(){
  vec3 cur = pow(texture(src, uv).rgb, vec3(2.2));
  vec3 old = texture(prev, uv).rgb;
  float k = 1.0 - exp(-dt/max(rate,0.001));   // seconds, time-based
  o = vec4(mix(old, cur, k), 1.0);
}
