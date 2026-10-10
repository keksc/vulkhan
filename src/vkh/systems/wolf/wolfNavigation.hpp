#pragma once

#include <glm/glm.hpp>

#include <functional>
#include <vector>

namespace vkh::wildlife {

// CPU navigation over the same terrain surface and exclusions as gameplay.
// Searches are bounded; an unreachable goal returns failure, not a path
// through a lake or cliff. No Vulkan or renderer dependencies.
class WolfNavigation {
public:
  using HeightFn = std::function<float(glm::vec2)>;
  using BlockedFn = std::function<bool(glm::vec2)>;
  WolfNavigation(HeightFn height, BlockedFn blocked);
  float height(glm::vec2 p) const;
  bool canStand(glm::vec2 p) const;
  bool canTraverse(glm::vec2 from, glm::vec2 to) const;
  bool findPath(glm::vec2 from, glm::vec2 to, std::vector<glm::vec2> &path,
                bool allowDetour = true) const;
  static constexpr int kExpansionBudget = 768;
  static constexpr int kSearchSampleBudget = 32768;
  static constexpr int kFrameSampleBudget = 65536;
  void beginFrame() { frameSamplesRemaining = kFrameSampleBudget; }

private:
  mutable bool planning = false;
  mutable int searchSamplesRemaining = 0;
  mutable int frameSamplesRemaining = kFrameSampleBudget;
  HeightFn heightFn;
  BlockedFn blockedFn;
};

} // namespace vkh::wildlife
