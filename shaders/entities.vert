#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in uvec4 jointIndices;
layout(location = 4) in vec4 jointWeights;

layout(location = 0) out vec3 fragPosWorld;
layout(location = 1) out vec3 fragNormalWorld;
layout(location = 2) out vec2 fragUV;
layout(location = 3) flat out vec4 fragColor;
layout(location = 4) flat out int fragTexIndex;

#include "globalUbo.glsl"

struct ObjectData {
  mat4 model;
  mat4 normal;
  vec4 color;
  int textureIndex;
  int jointOffset; // Replaced pad0
  int pad1;
  int pad2;
};

layout(std430, set = 2, binding = 0) readonly buffer ObjectBuffer {
  ObjectData objects[];
} objectBuffer;

layout(std430, set = 2, binding = 1) readonly buffer JointBuffer {
  mat4 jointMatrices[];
} jointBuffer;

void main() {
  ObjectData obj = objectBuffer.objects[gl_InstanceIndex];
  mat4 modelMatrix = obj.model;
  mat3 normalMatrix = mat3(obj.normal);

  vec4 positionWorld;
  vec3 normalWorld;

  if (obj.jointOffset >= 0) {
      vec4 totalPosition = vec4(0.0);
      vec3 totalNormal = vec3(0.0);
      
      for(int i = 0; i < 4; i++) {
        float weight = jointWeights[i];
        if(weight > 0.0) {
            mat4 jointMat = jointBuffer.jointMatrices[obj.jointOffset + jointIndices[i]];
            
            totalPosition += (jointMat * vec4(position, 1.0)) * weight;
            totalNormal += (mat3(jointMat) * normal) * weight;
        }
      }
      
      positionWorld = modelMatrix * totalPosition;
      normalWorld = normalMatrix * totalNormal;
  } else {
      positionWorld = modelMatrix * vec4(position, 1.0);
      normalWorld = normalMatrix * normal;
  }

  gl_Position = ubo.projView * positionWorld;
  fragNormalWorld = normalize(normalWorld);
  fragPosWorld = positionWorld.xyz;
  fragUV = uv;
  
  fragColor = obj.color;
  fragTexIndex = obj.textureIndex;
}
