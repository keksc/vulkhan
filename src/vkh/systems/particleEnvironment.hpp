#pragma once
#include "../nightLights.hpp"
#include <cmath>
#include <glm/glm.hpp>
#include <vector>
namespace vkh {
// Only query already loaded ground; never trigger terrain generation.
template <typename Height, typename Blocked>
std::vector<glm::vec3> particlePatches(glm::vec3 camera, Height height,
                                       Blocked blocked) {
  const glm::vec2 cell = glm::floor(glm::vec2{camera.x, camera.z} / 8.f) * 8.f;
  std::vector<glm::vec3> patches;
  for (int z = -2; z <= 2; ++z)
    for (int x = -2; x <= 2; ++x) {
      const glm::ivec2 key = glm::ivec2(cell / 8.f) + glm::ivec2{x, z};
      const uint32_t seed = nightCellSeed(key, 3);
      const glm::vec2 center =
          cell + glm::vec2{x * 8.f + 4.f, z * 8.f + 4.f} +
          (glm::vec2(nightRandom(seed), nightRandom(seed + 17u)) - .5f) * 3.6f;
      if (blocked(center))
        continue;
      const float ground = height(center);
      if (!std::isfinite(ground))
        continue;
      float highest = ground, lowest = ground;
      bool valid = true;
      for (glm::vec2 corner : {glm::vec2{-3, -3}, {3, -3}, {-3, 3}, {3, 3}}) {
        const float sample = height(center + corner);
        if (!std::isfinite(sample) || blocked(center + corner)) {
          valid = false;
          break;
        }
        highest = glm::max(highest, sample);
        lowest = glm::min(lowest, sample);
      }
      // Root flowers at the actual center height on gentle, valid slopes.
      if (valid && highest - lowest < 2.f)
        patches.emplace_back(center.x, ground, center.y);
    }
  return patches;
}
} // namespace vkh
