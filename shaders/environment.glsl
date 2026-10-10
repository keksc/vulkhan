#ifndef ENVIRONMENT_GLSL
#define ENVIRONMENT_GLSL
#include "bioluminescence.glsl"

// Include after globalUbo.glsl. All values are world-space, linear light.
vec3 sunDirection() { return ubo.sunDirection.xyz; }
vec3 sunRadiance() { return ubo.sunColor.rgb * ubo.sunColor.w; }
vec3 hemisphereAmbient(vec3 N) {
  return mix(ubo.groundAmbient.rgb, ubo.skyAmbient.rgb,
             clamp(N.y * 0.5 + 0.5, 0.0, 1.0));
}

vec3 localLighting(vec3 P, vec3 N) {
  return bioIrradiance(bioHabitat(P.xz), N);
}

float weatherHash(vec2 p) {
  vec3 q = fract(vec3(p.xyx) * 0.1031);
  q += dot(q, q.yzx + 33.33);
  return fract((q.x + q.y) * q.z);
}
float weatherNoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(weatherHash(i), weatherHash(i + vec2(1, 0)), f.x),
             mix(weatherHash(i + vec2(0, 1)), weatherHash(i + vec2(1)), f.x), f.y);
}

// The same travelling gust fronts drive grass, branches, and drifting haze.
// No per-frame randomness: wind is continuous in space and time.
float windGust(vec2 xz) {
  if (ubo.wind.w <= 0.0) return 0.0;
  vec2 moving = xz - ubo.wind.xy * ubo.time * ubo.wind.z;
  float broad = weatherNoise(moving * 0.025);
  float turbulence = weatherNoise(moving * 0.11 + 17.0);
  return (0.30 + 1.15 * broad * broad + 0.25 * turbulence) * ubo.wind.w;
}

vec3 fogInscatter(vec3 ray) {
  float g = ubo.fogParams.w;
  float mu = clamp(dot(ray, sunDirection()), -1.0, 1.0);
  float phase = (1.0 - g * g) /
      pow(max(1.0 + g * g - 2.0 * g * mu, 0.04), 1.5);
  return ubo.fogColor.rgb + sunRadiance() * min(phase, 8.0) * 0.025;
}

vec3 sceneFog(vec3 color, vec3 P, vec3 V, float dist) {
  if (ubo.fogParams.x <= 0.0) return color;
  vec3 camera = ubo.inverseView[3].xyz;
  float falloff = ubo.fogParams.y;
  float h0 = max(camera.y - ubo.fogParams.z, -40.0);
  float h1 = max(P.y - ubo.fogParams.z, -40.0);
  float r0 = exp(clamp(-h0 * falloff, -12.0, 3.0));
  float r1 = exp(clamp(-h1 * falloff, -12.0, 3.0));
  float delta = (h1 - h0) * falloff;
  // Analytic integral of exponential height fog; stable for horizontal rays.
  float meanDensity = abs(delta) < 0.001 ? 0.5 * (r0 + r1)
                                        : (r0 - r1) / delta;
  vec2 mid = mix(camera.xz, P.xz, 0.45);
  float ribbons = weatherNoise((mid - ubo.wind.xy * ubo.time *
                               ubo.wind.z * 0.35) * 0.009);
  float valley = 1.0 - smoothstep(ubo.fogParams.z + 15.0,
                                 ubo.fogParams.z + 100.0, P.y);
  float density = ubo.fogParams.x * meanDensity *
                  mix(1.0, 0.65 + 0.75 * ribbons, valley);
  float opacity = 1.0 - exp(-max(dist - 8.0, 0.0) * max(density, 0.0));
  return mix(color, fogInscatter(-V), clamp(opacity, 0.0, 1.0));
}

#endif
