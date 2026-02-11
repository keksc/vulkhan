#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec3 fragPosWorld;
layout(location = 1) out vec3 fragNormalWorld;
layout(location = 2) out vec2 fragUV;

#include "globalUbo.glsl"

struct ObjectData {
  mat4 model;
  mat4 normal;
};

layout(std430, set = 2, binding = 0) readonly buffer ObjectBuffer {
  ObjectData objects[];
} objectBuffer;

void main() {
  mat4 modelMatrix = objectBuffer.objects[gl_InstanceIndex].model;
  mat3 normalMatrix = mat3(objectBuffer.objects[gl_InstanceIndex].normal);

  vec4 positionWorld = modelMatrix * vec4(position, 1.0);
  
  gl_Position = ubo.projView * positionWorld;
  fragNormalWorld = normalize(normalMatrix * normal);
  fragPosWorld = positionWorld.xyz;
  fragUV = uv;
}
