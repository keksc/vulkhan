#version 450

layout (location = 0) in vec3 inColor;

layout (location = 0) out vec4 outColor;

struct Particle {
  vec3 position;
  vec3 color;
};

const int MAX_PARTICLES = 10;

layout(set = 0, binding = 0) uniform GlobalUbo {
  mat4 projection;
  mat4 view;
  mat4 inverseView;
  Particle particles[MAX_PARTICLES];
  int numParticles;
  float aspectRatio;
} ubo;

void main() {
  outColor = vec4(inColor, 1.0);
}
