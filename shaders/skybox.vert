#version 450

#include "globalUbo.glsl"

layout(location = 0) in vec3 position;

layout(location = 0) out vec3 outUVW;

void main() {
  outUVW = position;

  mat4 viewNoTranslation = mat4(mat3(ubo.view));
  vec4 pos = ubo.proj * viewNoTranslation * vec4(position, 1.0);
  gl_Position = pos.xyww;
}
