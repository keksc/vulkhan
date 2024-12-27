#version 450

layout (location = 0) in vec2 uv;

layout (location = 0) out vec4 outColor;

struct Particle {
  vec3 position;
  vec3 color;
};

layout(set = 0, binding = 0) uniform GlobalUbo {
  mat4 projection;
  mat4 view;
  mat4 invView;
  Particle particles[10];
  int numParticles;
  float aspectRatio;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D texSampler;

void main() {
  vec4 color = vec4(vec3(1.0), texture(texSampler, uv).r);
  outColor = color;
}
