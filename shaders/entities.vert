#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec3 fragPosWorld;
layout(location = 1) out vec3 fragNormalWorld;
layout(location = 2) out vec2 fragUV;

#include "globalUbo.glsl"

layout(push_constant) uniform Push {
  mat4 modelMatrix;
  mat3 normalMatrix; // Changed from mat4 to mat3 to match C++ struct
} pc;

void main() {
  vec4 positionWorld = pc.modelMatrix * vec4(position, 1.0);
  gl_Position = ubo.projView * positionWorld;
  fragNormalWorld = normalize(pc.normalMatrix * normal); // mat3 used directly
  fragPosWorld = positionWorld.xyz;
  fragUV = uv;
}
