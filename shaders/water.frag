#version 450

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 outColor;

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

layout(set = 1, binding = 0) uniform sampler2D heightMap;

void main() {
  float height = texture(heightMap, uv).r;
  
  outColor = vec4(vec3(height*1000), 1.0);
}
