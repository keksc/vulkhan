#version 450

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
  vec2 coord = gl_PointCoord - vec2(0.5);
  outColor = vec4(1.0, 0.5, 0.0, 0.5 - length(coord));
}
