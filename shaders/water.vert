#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out vec3 fragPosition;
layout(location = 2) out vec3 fragNormal;

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

layout(push_constant) uniform Push {
  mat4 modelMatrix;
  float time;
} push;

const float PI = 3.141592653589793;

const float amplitude = 24.5;   // Wave height
const float wavelength = 20.0; // Distance between crests
const float steepness = 0.66;  // Peak sharpness (0-1)
const vec2 direction = normalize(vec2(1.0, 0.2)); // Wave direction
const float speed = 2.12f; // Wave propagation speed
                            //
void main() {
  float height = texture(heightMap, uv).r;
  vec3 pos = position;
  pos.y += height;

  // Transform final position
  gl_Position = ubo.projection * ubo.view * push.modelMatrix * vec4(position, 1.0);
  fragPosition = pos;
  fragUv = uv;
}
