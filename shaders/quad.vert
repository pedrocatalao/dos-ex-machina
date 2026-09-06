#version 330 core
layout(location=0) in vec2 p;
out vec2 uv;
void main(){ uv = p*0.5+0.5; gl_Position = vec4(p,0,1); }
