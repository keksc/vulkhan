#version 450

layout (location = 0) in vec3 inColor;

layout (location = 0) out vec4 outColor;

struct PointLight {
  vec4 position; // ignore w
  vec4 color; // w is intensity
};

layout(set = 0, binding = 0) uniform GlobalUbo {
  mat4 projection;
  mat4 view;
  mat4 invView;
  vec4 ambientLightColor; // w is intensity
  PointLight pointLights[10];
  int numLights;
  float aspectRatio;
} ubo;

void main() {
  outColor = vec4(inColor, 1.0);
}
