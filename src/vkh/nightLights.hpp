#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace vkh {
// Keep the existing UBO capacity. Three world-anchored particle clipmaps use
// 49 + 25 + 25 slots; lighting/material emission is evaluated independently.
struct NightLights {
  static constexpr int count = 121, insectsPerSwarm = 64;
  static constexpr int nearCount = 49, middleCount = 25, farCount = 25;
  static constexpr int activeCount = nearCount + middleCount + farCount;
  static constexpr std::array<int, 3> widths{7, 5, 5};
  static constexpr std::array<float, 3> spacing{64.f, 256.f, 1024.f};
  glm::vec4 grid{};
  // xyz: anchored patch center/ground height, w: patch radius.
  // colors.w: smooth arrival/removal visibility; paths: slope X/Z, terrain
  // clearance, level + a stable fractional seed. No travelling lane IDs.
  std::array<glm::vec4, count> positions{}, colors{}, paths{};
};

inline uint32_t nightHash(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  return x ^ (x >> 16);
}
inline uint32_t nightCellSeed(glm::ivec2 cell, int level) {
  return nightHash(uint32_t(cell.x) * 73856093u ^ uint32_t(cell.y) * 19349663u ^
                   uint32_t(level) * 83492791u);
}
inline float nightRandom(uint32_t seed) {
  return float(nightHash(seed) >> 8) / 16777216.f;
}
inline glm::vec2 nightPatchCenter(glm::ivec2 cell, int level) {
  uint32_t seed = nightCellSeed(cell, level);
  return (glm::vec2(cell) + glm::vec2(.18f) +
          glm::vec2(nightRandom(seed), nightRandom(seed + 17u)) * .64f) *
         NightLights::spacing[level];
}
inline float nightPatchRadius(uint32_t seed, int level) {
  constexpr std::array<float, 3> radii{12.f, 36.f, 110.f};
  return radii[level] * (.7f + .3f * nightRandom(seed + 41u));
}
} // namespace vkh
