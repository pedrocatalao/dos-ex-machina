#version 330 core
in vec2 uv; out vec4 o;
uniform sampler2D src; uniform vec2 dir;
void main(){
  vec3 s = texture(src,uv).rgb*0.227;
  s += (texture(src,uv+dir*1.38).rgb + texture(src,uv-dir*1.38).rgb)*0.316;
  s += (texture(src,uv+dir*3.23).rgb + texture(src,uv-dir*3.23).rgb)*0.070;
  o = vec4(s,1.0);
}
