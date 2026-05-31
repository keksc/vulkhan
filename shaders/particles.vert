#version 450

layout(location = 0) in vec4 position;
layout(location = 1) in vec4 color;

layout(location = 0) out vec3 fragColor;
#include "globalUbo.glsl"

void main() {
  vec3 pos = position.xyz;
  pos.y += 10.f;
  pos.xz *= 8.0;
  gl_Position = ubo.projView * vec4(pos, 1.0);
  gl_PointSize = 4.0 / gl_Position.w;
  fragColor = color.rgb;
}
