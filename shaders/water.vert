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

const float amplitude = 0.8f;   // Wave height
const float wavelength = 20.0f; // Distance between crests
const float steepness = 0.75f;  // Peak sharpness (0-1)
const vec2 direction = normalize(vec2(1.0f, 0.2f)); // Wave direction
const float speed = 2.0f; // Wave propagation speed
                            //
void main() {
  float height = texture(heightMap, uv).r;
  float k = 2 * PI / wavelength;
  float Q = steepness / (k * amplitude);
  float frequency = sqrt(9.8 * k);
  
  // Base position
  vec3 pos = position;
  
  // Gerstner wave calculations
  float phase = k * dot(direction, pos.xz) - frequency * push.time;
  vec2 horizontalOffset = Q * amplitude * direction * cos(phase);
  
  pos.x += horizontalOffset.x;
  pos.z += horizontalOffset.y;
  pos.y += amplitude * sin(phase);

  // Calculate normals using partial derivatives
  vec3 tangent = vec3(1.0, k * amplitude * cos(phase), 0.0);
  vec3 bitangent = vec3(0.0, k * amplitude * cos(phase), 1.0);
  fragNormal = normalize(cross(tangent, bitangent));

  // Transform final position
  gl_Position = ubo.projection * ubo.view * push.modelMatrix * vec4(pos, 1.0);
  fragPosition = pos;
  fragUv = uv;
}
