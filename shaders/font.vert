#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 uv;

layout(location = 0) out vec2 fragUv;

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

void main() {
  gl_Position = vec4(position, 1.0);
  fragUv = uv;
}