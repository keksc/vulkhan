#pragma once

#include <glm/glm.hpp>

#include <limits>
#include <optional>
#include <vector>

struct AABB {
  glm::vec3 min{std::numeric_limits<float>::max()};
  glm::vec3 max{std::numeric_limits<float>::lowest()};

  bool intersects(const AABB &other) const {
    return (min.x <= other.max.x && max.x >= other.min.x) &&
           (min.y <= other.max.y && max.y >= other.min.y) &&
           (min.z <= other.max.z && max.z >= other.min.z);
  }

  bool isOnOrForwardPlane(const glm::vec4& plane) const {
    const float radius = glm::dot(glm::abs(glm::vec3(plane)), (max - min) * 0.5f);
    const float distance = glm::dot(glm::vec3(plane), (max + min) * 0.5f) + plane.w;
    return distance >= -radius;
  }

  bool isInFrustum(const std::vector<glm::vec4>& planes) const {
    for (const auto& plane : planes) {
      if (!isOnOrForwardPlane(plane)) return false;
    }
    return true;
  }

  AABB operator+(const glm::vec3 &x) const { return AABB{min + x, max + x}; }
  AABB operator*(const glm::vec3 &s) const { return AABB{min * s, max * s}; }
};

struct Ray {
  glm::vec3 origin;
  glm::vec3 direction;

  std::optional<float> intersects(const AABB &aabb) const {
    float tmin = (aabb.min.x - origin.x) / direction.x;
    float tmax = (aabb.max.x - origin.x) / direction.x;

    if (tmin > tmax)
      std::swap(tmin, tmax);

    float tymin = (aabb.min.y - origin.y) / direction.y;
    float tymax = (aabb.max.y - origin.y) / direction.y;

    if (tymin > tymax)
      std::swap(tymin, tymax);

    if ((tmin > tymax) || (tymin > tmax))
      return std::nullopt;

    if (tymin > tmin)
      tmin = tymin;

    if (tymax < tmax)
      tmax = tymax;

    float tzmin = (aabb.min.z - origin.z) / direction.z;
    float tzmax = (aabb.max.z - origin.z) / direction.z;

    if (tzmin > tzmax)
      std::swap(tzmin, tzmax);

    if ((tmin > tzmax) || (tzmin > tmax))
      return std::nullopt;

    if (tzmin > tmin)
      tmin = tzmin;

    if (tzmax < tmax)
      tmax = tzmax;

    if (tmin < 0) {
      if (tmax < 0)
        return std::nullopt;
      return tmax;
    }

    return tmin;
  }
};
