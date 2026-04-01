#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out vec3 fragColor;

#include "globalUbo.glsl"

void main() {
  vec3 pos = position;
  // pos.x /= ubo.aspectRatio;
  gl_Position = vec4(pos, 1.0);
  fragUv = uv;
  fragColor = color;
}
