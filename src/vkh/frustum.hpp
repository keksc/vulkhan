#pragma once

#include <array>
#include <glm/glm.hpp>

namespace vkh {

// Vulkan clip volume: -w <= x,y <= w and 0 <= z <= w. Also works
// with reversed Z; the last two planes exchange their geometric roles.
inline std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4 &m) {
  const glm::vec4 row0{m[0][0], m[1][0], m[2][0], m[3][0]};
  const glm::vec4 row1{m[0][1], m[1][1], m[2][1], m[3][1]};
  const glm::vec4 row2{m[0][2], m[1][2], m[2][2], m[3][2]};
  const glm::vec4 row3{m[0][3], m[1][3], m[2][3], m[3][3]};
  std::array<glm::vec4, 6> planes{row3 + row0, row3 - row0, row3 + row1,
                                  row3 - row1, row2,        row3 - row2};
  for (auto &plane : planes) {
    const float length = glm::length(glm::vec3(plane));
    // An infinite far plane has a zero normal. Keep its constant inequality.
    if (length > 1e-8f)
      plane /= length;
  }
  return planes;
}

} // namespace vkh
