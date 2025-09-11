#version 450

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec4 outColor;

#include "globalUbo.glsl"

layout(set = 1, binding = 0) uniform sampler2D colorTexSampler;
// layout(set = 1, binding = 1) uniform sampler2D metallicRoughnessTexSampler;
layout(set = 2, binding = 0) uniform samplerCube skybox;

layout(push_constant) uniform Push {
  mat4 modelMatrix;
  mat3 normalMatrix;
  vec4 color;
  bool useColorTexture;
  bool useMetallicRoughnessTexture;
  float metallic;
  float roughness;
} push;

void main() {
  vec4 color = push.useColorTexture ? texture(colorTexSampler, uv) : push.color;
  // vec2 metallicRoughness = push.useMetallicRoughnessTexture ? texture(metallicRoughnessTexSampler, uv).rg : vec2(push.metallic, push.roughness);
  vec3 norm = normalize(normal);
  vec3 lightDir = normalize(vec3(1.0));
  const float ambient = 0.1;
  float diffuse = max(dot(norm, lightDir), 0.0);

  vec4 result = (ambient + diffuse) * color;

  vec3 camPos = vec3(ubo.inverseView[3]);
  vec3 V = normalize(camPos - pos);
  vec3 N = norm;
  vec3 R = reflect(-V, N);
  vec4 envColor = texture(skybox, R);

  outColor = mix(result, envColor, 0.2);
}
