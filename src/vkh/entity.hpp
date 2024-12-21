#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <optional>

#include "model.hpp"

namespace vkh {

// Transform structure for position, scale, and orientation
struct Transform {
  glm::vec3 position{};
  glm::vec3 scale{1.f, 1.f, 1.f};
  glm::quat orientation{};

  glm::mat4 mat4();
  glm::mat3 normalMatrix();
};

// Point light structure
struct PointLight {
  glm::vec3 color{};
  float lightIntensity = 1.0f;
  Transform transform;
};

// RigidBody structure for physics properties
struct RigidBody {
  glm::vec3 velocity{0.f};
  float mass{1.f};
  bool isStatic{false}; // Static bodies do not move

  const glm::vec3 computeWeight() const { return {0, mass * 9.81f, 0}; }
};

struct Entity {
  Transform transform;
  std::optional<Model> model;
  RigidBody rigidBody;
};

} // namespace vkh
