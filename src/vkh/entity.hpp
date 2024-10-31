#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <unordered_map>

#include "engineContext.hpp"

namespace vkh {
class Model;
struct Transform {
  glm::vec3 position{};
  glm::vec3 scale{1.f, 1.f, 1.f};
  glm::quat orientation{};

  // Matrix corrsponds to Translate * Ry * Rx * Rz * Scale
  // Rotations correspond to Tait-bryan angles of Y(1), X(2), Z(3)
  // https://en.wikipedia.org/wiki/Euler_angles#Rotation_matrix
  glm::mat4 mat4();

  glm::mat3 normalMatrix();
};

struct PointLight {
  glm::vec3 color{};
  float lightIntensity = 1.0f;
  Transform transform;
};

struct BoundingBox {
  glm::vec3 min;
  glm::vec3 max;
  bool contains(const glm::vec3 &point) const {
    return (point.x >= min.x && point.x <= max.x) &&
           (point.y >= min.y && point.y <= max.y) &&
           (point.z >= min.z && point.z <= max.z);
  }
  glm::vec3 center() const { return (min + max) * 0.5f; }
  glm::vec3 size() const { return max - min; }
};

struct Entity {
  Transform transform;
  std::shared_ptr<Model> model;
  BoundingBox boundingBox;
  glm::vec3 velocity{0.f};
  float mass{1.f};
  glm::vec3 computeWeight() { return {0, mass * 9.81, 0}; }
};
} // namespace vkh
