#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 position;
layout(location = 2) in vec3 normal;

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

const vec3 lightPos = vec3(2.0, -3.0, -1.0);

void main() {
  vec3 dirToLight = normalize(lightPos - position);
  float h = texture(heightMap, uv).r;
  outColor = vec4(vec3(dot(normalize(normal), dirToLight)), 1.0);
}
