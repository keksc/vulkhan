#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec2 fragUv;

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
  vec2 pos = position;
  pos.x /= ubo.aspectRatio;
  gl_Position = vec4(pos, 0.0, 1.0);
  fragUv = uv;
}
