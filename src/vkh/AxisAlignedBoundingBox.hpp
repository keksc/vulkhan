#pragma once

#include <glm/glm.hpp>

namespace vkh {
template <typename T> struct AABB {
  T a, b;
  float w() { return glm::abs(a.x - b.x); }
  float h() { return glm::abs(a.y - b.y); }
  glm::vec2 size() { return glm::abs(a - b); }
};
} // namespace vkh
