#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec2 fragUv;

#include "globalUbo.glsl"

void main() {
  vec2 pos = position;
  // pos.x /= ubo.aspectRatio;
  gl_Position = vec4(pos, 0.0, 1.0);
  fragUv = uv;
}
