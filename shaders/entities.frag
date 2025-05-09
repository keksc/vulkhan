#version 450

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout (location = 0) out vec4 outColor;

#include "globalUbo.glsl"

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform Push {
  mat4 modelMatrix;
  mat4 normalMatrix;
} push;

void main() {
  const vec3 lightDir = vec3(500.0, 30.0, -1.0);
  const float ambient = 0.1;
  float lightIntensity = ambient + max(dot(normal, lightDir), 0.0);

  // vec3 lightPos = vec3(0.0, 0.0, 1.0);
  // vec3 normal = normalize(pos);
  // vec3 dirToLight = lightPos - pos;
  // vec3 cameraPosWorld = ubo.inverseView[3].xyz;
  // vec3 viewDirection = normalize(cameraPosWorld - pos);
  //
  // float attenuation = 1.0/dot(dirToLight, dirToLight);
  // // Lambertian
  // dirToLight = normalize(dirToLight);
  // float diffuseLight = max(dot(dirToLight, normal), 0.0);
  //
  // // Blinn-Phong
  // vec3 mid = normalize(viewDirection + dirToLight);
  // float specularLight = dot(dirToLight, mid);
  // specularLight = clamp(0, 1, specularLight);
  // specularLight = pow(specularLight, 512.0);
  //
  // float ambientLight = 1.0;
  //
  // outColor = texture(texSampler, uv)*((diffuseLight+specularLight)*attenuation+ambientLight);
  outColor = texture(texSampler, uv)*lightIntensity;
}
