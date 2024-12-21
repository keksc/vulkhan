#include "physics.hpp"

#include "../entity.hpp"
#include <fmt/format.h>

namespace vkh {
namespace physicsSys {
void update(EngineContext &context) {
  for (auto &entity : context.entities) {
    if (!entity.model.has_value())
      continue;
    glm::vec3 force =
        entity.rigidBody.computeWeight() + glm::vec3{1.f, 0.f, 0.f};
    glm::vec3 acceleration = force / entity.rigidBody.mass;
    entity.rigidBody.velocity += acceleration * context.frameInfo.dt;
    entity.transform.position +=
        entity.rigidBody.velocity * context.frameInfo.dt;
  }
}
} // namespace physicsSys
} // namespace vkh
