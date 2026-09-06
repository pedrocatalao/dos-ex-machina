#version 330 core
/* A plain black veil, for bringing the whole machine up out of nothing
 * after the splash.  It goes over everything - chassis, tube and panel - so
 * what fades in is the room, not just the picture. */
out vec4 o; uniform float a;
void main(){ o = vec4(0.0,0.0,0.0,a); }
