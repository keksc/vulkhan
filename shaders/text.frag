#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 color;

layout(location = 0) out vec4 outColor;

#include "globalUbo.glsl"

layout(set = 1, binding = 0) uniform sampler2D texSampler;

void main() {
  outColor = vec4(color, texture(texSampler, uv).r);
}
