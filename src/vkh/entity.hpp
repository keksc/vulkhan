#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>

#include "engineContext.hpp"
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

// RigidBody structure for physics properties
struct RigidBody {
  glm::vec3 velocity{0.f};
  float mass{1.f};
  bool isStatic{false}; // Static bodies do not move

  const glm::vec3 computeWeight() const { return {0, mass * 9.81f, 0}; }
};

struct Entity {
    Transform transform;
    RigidBody rigidBody;
    std::unique_ptr<Model> model;

    // Modified constructors to accept const references
    Entity(EngineContext& context, Transform transform, const std::string& name,
           RigidBody rigidBody = {});
    
    Entity(EngineContext& context, Transform transform, const std::string& name,
           const std::string& modelFilepath, bool modelEnableTexture = true, RigidBody rigidBody = {});
    
    Entity(EngineContext& context, Transform transform, const std::string& name,
           const std::string& modelFilepath, const std::string& modelTextureFilepath,
           RigidBody rigidBody = {});
};

} // namespace vkh
