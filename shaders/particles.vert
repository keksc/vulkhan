#version 450

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

layout(push_constant) uniform Push {
  int particleIndex;
} push;

const vec3 verts[6] = vec3[](
  vec3(0.0, 0.0, 1.0),
  vec3(0.5, 0.0, -1.0),
  vec3(0.0, 0.5, -0.5),
  vec3(0.3, 0.4, 0.5),
  vec3(0.2, 0.1, 0.2),
  vec3(1.0, 1.0, -0.2)
);

void main() {
  gl_Position = ubo.projection * ubo.view * vec4(ubo.particles[push.particleIndex].position, 1.0);
  gl_PointSize = 14.0;
}
