#version 450
layout(location=2) in vec4 uv;
layout(location=3) in vec4 normal;
layout(location=4) in vec4 tangent;
layout(location=6) in vec4 world;
layout(location=0) out vec4 color;
void main(){ color=vec4(uv.xy+normal.xy*.03,world.z*.1+tangent.x*.05,1); }
