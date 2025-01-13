#version 450

layout(location = 0) in vec3 position;
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

layout(set = 1, binding = 0) uniform sampler2D heightMap;

layout(push_constant) uniform Push {
  mat4 modelMatrix;
  float time;
} push;

vec2 complexExp(float x) {
    return vec2(cos(x), sin(x));
}

void main() {
  //float height = texture(heightMap, uv).r*1000;// * heightScale + heightOffset;
  float waveNumber = 1.0;
  float angularFrequency = 1.0;
  float phase = 1.0;
  float amplitude = 1.0;
  vec2 height = amplitude*complexExp(phase)*complexExp(waveNumber*position.x+angularFrequency*push.time);
  vec3 displacedPosition = vec3(position.x, position.y+height.y, position.z);

  gl_Position = ubo.projection * ubo.view * push.modelMatrix * vec4(displacedPosition, 1.0);
  fragUv = uv;
}
