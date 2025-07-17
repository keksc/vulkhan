#pragma once

#include <glm/glm.hpp>

#include <limits>

namespace vkh {
struct AABB {
  glm::vec3 min{-std::numeric_limits<float>::max()};
  glm::vec3 max{std::numeric_limits<float>::max()};
  bool intersects(const AABB &other) const {
    return (min.x <= other.max.x && max.x >= other.min.x) &&
           (min.y <= other.max.y && max.y >= other.min.y) &&
           (min.z <= other.max.z && max.z >= other.min.z);
  }
};
} // namespace vkh
