#version 450

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

layout(push_constant) uniform Push {
  int particleIndex;
} push;

void main() {
  vec2 coord = gl_PointCoord - vec2(0.5);
  outColor = vec4(ubo.particles[push.particleIndex].color, 0.5 - length(coord));
}
