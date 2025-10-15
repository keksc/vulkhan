#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;

layout (location = 0) out vec4 outColor;

#include "globalUbo.glsl"

float rand(vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
  // outColor = vec4(color * rand(vec2(position.x + ubo.time*0.000001, position.y)), 1.0);
  float r = rand(position.xy) > 0.2 + pow(sin(ubo.time), 4.0) * 0.3 ? 1.0 : 0.0;
  outColor = vec4(color * r, 1.0);
}
