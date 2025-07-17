#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;

layout(location = 0) out vec3 fragColor;

#include "globalUbo.glsl"

void main() {
  gl_Position = ubo.projView * vec4(position, 1.0);
  gl_PointSize = 120.0 / gl_Position.w;
  fragColor = color;
}
