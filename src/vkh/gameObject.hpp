#pragma once

// libs
#include <glm/gtc/matrix_transform.hpp>

// std
#include <memory>
#include <unordered_map>

namespace vkh {
class LveModel;
struct Transform {
  glm::vec3 translation{};
  glm::vec3 scale{1.f, 1.f, 1.f};
  glm::vec3 rotation{};

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

struct Entity {
  Transform transform;
  std::shared_ptr<LveModel> model;
};
} // namespace vkh
