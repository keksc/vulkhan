#version 450

#include "consts.glsl"

layout(location = 0) in vec2 uv;
layout(location = 1) in flat int texIndex;

layout (location = 0) out vec4 outColor;

#include "globalUbo.glsl"
layout(set = 1, binding = 0) uniform sampler2D texSamplers[64];

#define TILING_FACTOR 1.0
#define MAX_ITER 8

float waterHighlight(vec2 p, float time, float foaminess) {
  vec2 i = vec2(p);
  float c = 0.05;
  float foaminessFactor = mix(1.1, 6.0, foaminess);
  float inten = 0.005 * foaminessFactor;

  for(int n = 0; n < MAX_ITER; n++) {
    float t = time * (0.9 - (5.5 / float(n+1)));
    i = p + vec2(cos(t - i.x) + sin(t + i.y), sin(t - i.y) + cos(t + i.x));
    c += 0.9 / length(vec2(p.x / (sin(i.x + t)), p.y / cos(i.y + t)));
  }
  c = 0.2 + c / (inten * float(MAX_ITER));
  c = 1.17 - pow(c, 1.7);
  c = pow(abs(c), 15.0);
  return c / sqrt(foaminessFactor);
}

void main() {
  float t = ubo.time * 0.02 + 23.0;
  vec2 uvScreen = uv;//gl_FragCoord.xy / ubo.resolution;
  vec2 uvSquare = vec2(uvScreen.x * ubo.aspectRatio, uvScreen.y);
  float dCenter = sqrt(length(uvScreen - 0.5));

  float foaminess = smoothstep(0.9, 8.0, dCenter);
  float clearness = 0.9 + 0.5 * smoothstep(0.5, 0.1, dCenter);

  vec2 p = mod(uvSquare * TAU * TILING_FACTOR, TAU) - 250.0;

  float c = waterHighlight(p, t, foaminess);

  vec3 waterColor = vec3(0.0, 0.5, 0.5);
  vec3 color = vec3(c);
  color = clamp(color + waterColor, 0.0, 1.0);

  color = mix(waterColor, color, clearness);

  outColor = vec4(1.0 - color, 1.0) * texture(texSamplers[texIndex], uv);
}
