#version 450

layout (location = 0) in vec2 uv;

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
} ubo;

layout(push_constant) uniform Push {
  float time;
} push;

void main() {
  vec2 pos = vec2(0.7, -0.7)-uv;

  float r = length(pos)*3.0;
  float a = atan(pos.y,pos.x);

  float f = pow(abs(cos(a*12.)*sin(a*3.))*.8+.1, 4);

  float opacity = 1.-smoothstep(f,f+0.02,r);
  outColor = vec4(0.054, 0.57, 0.8, opacity);
}
