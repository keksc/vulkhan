#version 450

layout(location = 0) in vec3 color;

layout(location = 0) out vec4 outColor;

#include "globalUbo.glsl"

void main() {
  vec2 coord = gl_PointCoord - vec2(0.5);

  outColor = vec4(color, 0.5 - length(coord));
}
