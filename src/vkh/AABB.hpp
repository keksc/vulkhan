#pragma once

#include <glm/glm.hpp>

#include <limits>
#include <optional>
#include <vector>

namespace vkh {
struct AABB {
  glm::vec3 min{std::numeric_limits<float>::max()};
  glm::vec3 max{std::numeric_limits<float>::lowest()};

  bool intersects(const AABB &other) const {
    return (min.x <= other.max.x && max.x >= other.min.x) &&
           (min.y <= other.max.y && max.y >= other.min.y) &&
           (min.z <= other.max.z && max.z >= other.min.z);
  }

  bool isOnOrForwardPlane(const glm::vec4 &plane) const {
    const float radius =
        glm::dot(glm::abs(glm::vec3(plane)), (max - min) * 0.5f);
    const float distance =
        glm::dot(glm::vec3(plane), (max + min) * 0.5f) + plane.w;
    return distance >= -radius;
  }

  bool isInFrustum(const std::vector<glm::vec4> &planes) const {
    for (const auto &plane : planes) {
      if (!isOnOrForwardPlane(plane))
        return false;
    }
    return true;
  }

  AABB operator+(const glm::vec3 &x) const { return AABB{min + x, max + x}; }
  AABB operator*(const glm::vec3 &s) const { return AABB{min * s, max * s}; }
};

struct Ray {
  glm::vec3 o;
  glm::vec3 dir;

  // slab algorithm
  std::optional<float> intersects(const AABB &aabb) const {
    float tMin = 0.f;
    float tMax = std::numeric_limits<float>::infinity();

    for (size_t axis = 0; axis < 3; axis++) {
      if (glm::abs(dir[axis]) < 1e-8f) {
        if (o[axis] < aabb.min[axis] || o[axis] > aabb.max[axis])
          return std::nullopt;

        continue;
      }

      float invD = 1.f / dir[axis];
      float t0 = (aabb.min[axis] - o[axis]) * invD;
      float t1 = (aabb.max[axis] - o[axis]) * invD;

      if (t0 > t1)
        std::swap(t0, t1);

      tMin = glm::max(tMin, t0);
      tMax = glm::min(tMax, t1);

      if (tMax < tMin)
        return std::nullopt;
    }
    return tMin;
  }
};
} // namespace vkh
