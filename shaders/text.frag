#version 450

layout (location = 0) in vec2 uv;

layout (location = 0) out vec4 outColor;

#include "globalUbo.glsl"

layout(set = 1, binding = 0) uniform sampler2D texSampler;

void main() {
  vec4 color = vec4(vec3(1.0), texture(texSampler, uv).r);
  outColor = color;
}
