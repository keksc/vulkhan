#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;

layout(location = 0) out vec3 fragColor;

layout(set = 0, binding = 0) uniform GlobalUbo {
  mat4 projection;
  mat4 view;
  mat4 inverseView;
  float aspectRatio;
} ubo;

void main() {
  gl_Position = ubo.projection * ubo.view * vec4(position, 1.0);
  gl_PointSize = 100.0 / gl_Position.w;
  fragColor = color;
}
