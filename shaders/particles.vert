#version 450
layout(location=0) in vec4 position;
layout(location=1) in vec4 color;
layout(location=2) in vec4 params;
layout(location=0) out vec4 fragColor;
layout(location=1) out vec2 uv;
layout(location=2) flat out float kind;
#include "globalUbo.glsl"
void main() {
  const vec2 corners[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),vec2(-1,-1),vec2(1,1),vec2(-1,1));
  uv=corners[gl_VertexIndex]; kind=params.x; fragColor=color;
  if(color.a<=.001) { gl_Position=vec4(2,2,2,1); return; }
  float d=length(position.xyz-ubo.inverseView[3].xyz);
  float radius=max(position.w,d*1.5/max(abs(ubo.proj[1][1])*ubo.resolution.y,1.0));
  vec2 q=uv;
  if(kind==1.0) q=mat2(cos(params.y),sin(params.y),-sin(params.y),cos(params.y))*vec2(q.x*.45,q.y);
  vec3 right=ubo.inverseView[0].xyz, up=ubo.inverseView[1].xyz;
  if(kind==2.0) { right=normalize(vec3(right.x,0,right.z)+vec3(.00001,0,0)); up=vec3(0,1,0); }
  gl_Position=ubo.projView*vec4(position.xyz+(right*q.x+up*q.y)*radius,1);
}
