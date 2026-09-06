#version 330 core
/* ---- pass 1: phosphor persistence + burn-in, time-based decay (SPEC §6.7) */
in vec2 uv; out vec4 o;
uniform sampler2D src, prev;
uniform float dt, persist;
void main(){
  vec3 cur = texture(src, uv).rgb;
  vec3 old = texture(prev, uv).rgb;
  cur = pow(cur, vec3(2.2));   // to linear
  float hl = mix(0.010, 0.075, persist);   // half-life in SECONDS, not frames
  vec3 k = vec3(pow(0.5, dt/hl), pow(0.5, dt/(hl*1.25)), pow(0.5, dt/(hl*0.8)));
  o = vec4(max(cur, old*k), 1.0);   // green persists longest (P22)
}
