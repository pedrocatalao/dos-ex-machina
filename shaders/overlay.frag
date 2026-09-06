#version 330 core
in vec2 uv; out vec4 o;
uniform sampler2D src;
void main(){ o = texture(src, vec2(uv.x, 1.0-uv.y)); }
