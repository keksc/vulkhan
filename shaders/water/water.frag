#version 450

layout(location = 0) in vec4 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec4 outColor;

#include "../globalUbo.glsl"

layout(set = 1, binding = 0) uniform VertexUBO {
    mat4 model;
    float scale;
} vertexUBO;

layout(set = 1, binding = 1) uniform sampler2D displacementFoamMap;
layout(set = 2, binding = 0) uniform samplerCube skybox;

void main() {
  float foamMask = texture(displacementFoamMap, inUV).a;

  vec3 c = texture(displacementFoamMap, inUV).rgb;
  
  vec3 camPos = vec3(ubo.inverseView[3]);
  vec3 worldPos = inPos.xyz;
  vec3 V = normalize(camPos - worldPos);
  vec3 N = normalize(inNormal);
  vec3 R = reflect(-V, N);
  
  vec3 reflection = texture(skybox, R).rgb;
  vec3 waterColor = vec3(0.01, 0.05, 0.1);
  vec3 foamColor = vec3(0.95);

  float F0 = 0.02;
  float base = 1.0 - max(dot(N, V), 0.0);
  float fresnel = F0 + (1.0 - F0) * pow(base, 5.0);
  vec3 finalColor = mix(waterColor, reflection, fresnel);

  finalColor = mix(finalColor, foamColor, foamMask);

  outColor = vec4(finalColor, 1.0);
}
