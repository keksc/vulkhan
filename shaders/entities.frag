#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) flat in vec4 fragColor;
layout(location = 4) flat in int fragTexIndex;

layout(location = 0) out vec4 outColor;

#include "globalUbo.glsl"

layout(set = 1, binding = 0) uniform sampler2D sceneTextures[256];

void main() {
  vec4 color = fragColor;
  
  if (fragTexIndex >= 0) {
      color *= texture(sceneTextures[nonuniformEXT(fragTexIndex)], uv);
  }
  
  vec3 norm = normalize(normal);
  vec3 lightDir = normalize(vec3(1.0));
  const float ambient = 0.1;
  float diffuse = max(dot(norm, lightDir), 0.0);
  
  vec4 result = (ambient + diffuse) * color;

  outColor = result;
}
