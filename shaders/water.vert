#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 uv;

layout(location = 0) out vec3 fragPosWorld;
layout(location = 1) out vec3 fragNormalWorld;

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
  mat4 modelMatrix;
  mat4 normalMatrix;
  float time;
} push;

/*float initialAmplitude = 1.5;
float initialFrequency = 1.0;

float sumOfWavesX(float x) {
  float sum = 0.0;
  for(int i=0;i<4;i++) {
    float amplitude = (initialAmplitude * 0.82 * i);
    float frequency = (initialFrequency * 1.18 * i);
    sum += amplitude * exp(sin(x * frequency + push.time)-1.0);
  }
  return sum;
}

float sumOfWavesDerivativeX(float x) {
  float sum = 0.0;
  for(int i=0;i<4;i++) {
    float amplitude = (initialAmplitude * 0.82 * i);
    float frequency = (initialFrequency * 1.18 * i);
    sum += amplitude * exp(sin(x * frequency + push.time)-1.0) * frequency * cos(x * frequency + push.time);
  }
  return sum;
}

float sumOfWavesZ(float z) {
  float sum = 0.0;
  for(int i=0;i<4;i++) {
    float amplitude = (initialAmplitude * 0.75 * i);
    float frequency = (initialFrequency * 1.19 * i);
    sum += amplitude * exp(sin(z * frequency + push.time)-1.0);
  }
  return sum;
}

float sumOfWavesDerivativeZ(float z) {
  float sum = 0.0;
  for(int i=0;i<4;i++) {
    float amplitude = (initialAmplitude * 0.75 * i);
    float frequency = (initialFrequency * 1.19 * i);
    sum += amplitude * exp(sin(z * frequency + push.time)-1.0) * frequency * cos(z * frequency + push.time);
  }
  return sum;
}*/
float sumOfWavesX(float x, float z) {
    return sin(x * 10.0 + push.time) * 0.5;
}
float sumOfWavesDerivativeX(float x, float z) {
  return 5.0*cos(x*10.0+push.time);
}
float sumOfWavesZ(float x, float z) {
  return cos(z * 5.0 + push.time * 0.7) * 0.3;
}
float sumOfWavesDerivativeZ(float z) {
  return -0.5*sin(z*0.5+push.time*0.7)*0.3;
}

void main() {
  vec3 pos = position;
  float x = sumOfWavesX(pos.x, pos.z);
  float z = sumOfWavesZ(pos.z, pos.z);

  pos.y += x + z;

  vec3 tangent = normalize(vec3(1.0, sumOfWavesDerivativeX(pos.x, pos.z), 0.0));
  vec3 binormal = normalize(vec3(0.0, sumOfWavesDerivativeZ(pos.z), 1.0));
  vec3 normal = normalize(cross(tangent, binormal));

  fragPosWorld = pos;
  fragNormalWorld = normal;

  vec4 positionWorld = push.modelMatrix * vec4(pos, 1.0);
  gl_Position = ubo.projection * ubo.view * positionWorld;
}
