#version 450

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec4 outColor;

#include "globalUbo.glsl"

layout(set = 1, binding = 0) uniform sampler2D colorTexSampler;

layout(push_constant) uniform Push {
  vec4 color;
  uint useColorTexture;
} push;

void main() {
  vec4 color = (push.useColorTexture == 1) ? texture(colorTexSampler, uv) : push.color;
  
  vec3 norm = normalize(normal);
  vec3 lightDir = normalize(vec3(1.0));
  const float ambient = 0.1;
  float diffuse = max(dot(norm, lightDir), 0.0);

  vec4 result = (ambient + diffuse) * color;

  outColor = result;
}
