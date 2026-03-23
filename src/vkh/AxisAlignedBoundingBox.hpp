#pragma once

#include <glm/glm.hpp>

#include <limits>

namespace vkh {
struct AABB {
  glm::vec3 min{std::numeric_limits<float>::max()};
  glm::vec3 max{std::numeric_limits<float>::lowest()};
  bool intersects(const AABB &other) const {
    return (min.x <= other.max.x && max.x >= other.min.x) &&
           (min.y <= other.max.y && max.y >= other.min.y) &&
           (min.z <= other.max.z && max.z >= other.min.z);
  }
  AABB operator+(const glm::vec3 &x) const { return AABB{min + x, max + x}; }
  AABB operator*(const glm::vec3 &s) const { return AABB{min * s, max * s}; }
};
} // namespace vkh
