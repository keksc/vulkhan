#version 450

layout(location = 0) in vec4 inPos;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec4 outColor;

#include "../globalUbo.glsl"

layout(set = 2, binding = 0) uniform samplerCube skybox;

void main() {
  vec3 camPos = vec3(ubo.inverseView[3]);

  vec3 worldPos = inPos.xyz;
  vec3 V = normalize(camPos - worldPos);
  vec3 N = normalize(inNormal);
  vec3 R = reflect(-V, N);
  
  vec3 reflection = texture(skybox, R).rgb;
  vec3 waterColor = vec4(0.01, 0.05, 0.1, 1.0).rgb;

  float F0 = 0.02;
  float base = 1.0 - max(dot(N, V), 0.0);
  float base2 = base * base;
  float base5 = base2 * base2 * base;
  float fresnel = F0 + (1.0 - F0) * base5;

  vec3 finalColor = mix(waterColor, reflection, fresnel);

  outColor = vec4(finalColor, 1.0);
}
