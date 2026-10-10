#pragma once
// CPU mirror of shaders/biome.glsl. Keep every constant and formula identical:
// the terrain shader draws the biomes, this makes grass and trees agree with
// what it draws. Same integer hash on both sides, so results match to float
// precision.
//
//   grassDensity(xz, hNorm) -> multiply into the grass spawn suitability
//                              (see HeightWindow's DensityFn in grassField.hpp)
//   treeDensity(xz, hNorm)  -> multiply into the tree acceptance probability
//
// hNorm = (y - baseHeight) / heightRange, clamped to [0,1].

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>

namespace vkh::biome {

constexpr float kTempScale = 1.f / 1500.f;
constexpr float kMoistScale = 1.f / 1100.f;
constexpr float kLapse = 0.80f;
constexpr float kHomeR0 = 250.f;
constexpr float kHomeR1 = 900.f;

inline uint32_t hashU(uint32_t n) {
  n = (n ^ 61u) ^ (n >> 16u);
  n *= 9u;
  n = n ^ (n >> 4u);
  n *= 0x27d4eb2du;
  n = n ^ (n >> 15u);
  return n;
}
inline float cell(int32_t x, int32_t y, uint32_t seed) {
  return static_cast<float>(hashU(static_cast<uint32_t>(x) * 73856093u ^
                                  static_cast<uint32_t>(y) * 19349663u ^ seed)) *
         (1.f / 4294967296.f);
}
inline float noise(glm::vec2 p, uint32_t seed) {
  const glm::vec2 i = glm::floor(p), f = p - i;
  const glm::vec2 u = f * f * (3.f - 2.f * f);
  const int32_t cx = static_cast<int32_t>(i.x), cy = static_cast<int32_t>(i.y);
  return glm::mix(glm::mix(cell(cx, cy, seed), cell(cx + 1, cy, seed), u.x),
                  glm::mix(cell(cx, cy + 1, seed), cell(cx + 1, cy + 1, seed), u.x),
                  u.y);
}
inline float fbm(glm::vec2 p, uint32_t seed) {
  const float v = noise(p, seed) * 0.5f + noise(p * 2.f + 17.f, seed) * 0.25f +
                  noise(p * 4.f + 41.f, seed) * 0.125f;
  return v / 0.875f;
}
inline float spread(float x) { return glm::smoothstep(0.18f, 0.82f, x); }

struct Climate {
  float tempBase; // temperature at sea level
  float temp;     // tempBase - lapse * altitude
  float moist;
};

inline Climate climateAt(glm::vec2 xz, float alt) {
  float tn = spread(fbm(xz * kTempScale, 0x1234u));
  float mn = spread(fbm(xz * kMoistScale + 7.7f, 0x5678u));
  const float rg = noise(xz * (1.f / 140.f), 0x9abcu) - 0.5f;
  const float away = glm::smoothstep(kHomeR0, kHomeR1, glm::length(xz));
  tn = glm::mix(0.55f, tn, away);
  mn = glm::mix(0.52f, mn, away);
  Climate c;
  c.tempBase = glm::mix(0.15f, 1.f, tn) + rg * 0.10f * away;
  c.temp = c.tempBase - kLapse * alt;
  c.moist = glm::clamp(mn - rg * 0.10f * away, 0.f, 1.f);
  return c;
}

// (meadow, steppe, desert, forest), altitude-independent, sums to 1.
inline glm::vec4 warmDistribution(float tempBase, float moist) {
  const float hot = glm::smoothstep(0.55f, 0.85f, tempBase);
  const float dry = 1.f - glm::smoothstep(0.25f, 0.50f, moist);
  const float wet = glm::smoothstep(0.55f, 0.80f, moist);
  const float desert = hot * dry;
  const float steppe = (1.f - desert) * dry * (0.80f + 0.20f * hot);
  const float forest =
      (1.f - hot * 0.7f) * wet * (1.f - dry) * (1.f - desert - steppe);
  const float meadow = glm::max(1.f - desert - steppe - forest, 0.f) + 1e-3f;
  return glm::vec4(meadow, steppe, desert, forest) /
         (meadow + steppe + desert + forest);
}

struct Biome {
  float meadow, steppe, desert, forest, tundra;
};

inline Biome biomeAt(const Climate &c) {
  const float tundra = 1.f - glm::smoothstep(0.02f, 0.30f, c.temp);
  const glm::vec4 w = warmDistribution(c.tempBase, c.moist) * (1.f - tundra);
  return {w.x, w.y, w.z, w.w, tundra};
}

inline float grassDensityOf(const Biome &b) {
  return b.meadow + b.steppe * 0.55f + b.forest * 0.45f + b.tundra * 0.06f;
}
inline float treeDensityOf(const Biome &b) {
  return b.meadow + b.forest * 1.5f + b.steppe * 0.2f + b.tundra * 0.25f;
}

inline float grassDensity(glm::vec2 xz, float hNorm) {
  return grassDensityOf(biomeAt(climateAt(xz, hNorm)));
}
inline float treeDensity(glm::vec2 xz, float hNorm) {
  return treeDensityOf(biomeAt(climateAt(xz, hNorm)));
}

} // namespace vkh::biome
