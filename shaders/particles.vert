#version 450

layout(location = 0) in vec4 position;
layout(location = 1) in vec4 color;

layout(location = 0) out vec3 fragColor;
#include "globalUbo.glsl"

void main() {
  gl_Position = ubo.projView * position;
  gl_PointSize = 8.0 / gl_Position.w;
  fragColor = color.rgb;
}
