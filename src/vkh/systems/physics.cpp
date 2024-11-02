#include "physics.hpp"

#include "../entity.hpp"
#include <fmt/base.h>

namespace vkh {
namespace physicsSys {
void update(EngineContext &context) {
  for (auto &entity : context.entities) {
    if (entity.model == nullptr)
      fmt::print("player velocity XYZ: {}, {}, {}\n", entity.rigidBody.velocity.x, entity.rigidBody.velocity.y, entity.rigidBody.velocity.x);
    glm::vec3 force = entity.rigidBody.computeWeight();
    glm::vec3 acceleration = force / entity.rigidBody.mass;
    entity.rigidBody.velocity += acceleration * context.frameInfo.dt;
    entity.transform.position += entity.rigidBody.velocity * context.frameInfo.dt;
  }
}
} // namespace physicsSys
} // namespace vkh
