#version 330 core
/* Startup splash: the wordmark on black, before the machine exists.  It is
 * drawn PREMULTIPLIED - the source is baked that way so that scaling it up
 * with linear filtering cannot pull the transparent pixels' black into the
 * edges as a dark fringe. */
in vec2 uv; out vec4 o;
uniform sampler2D src;
uniform vec4  rect;      // where the logo sits, in 0..1 output space
uniform float alpha;
void main(){
  vec2 t = (uv - rect.xy) / rect.zw;
  o = vec4(0.0);
  if (t.x>=0.0 && t.x<=1.0 && t.y>=0.0 && t.y<=1.0)
    o = texture(src, vec2(t.x, 1.0-t.y)) * alpha;
}
