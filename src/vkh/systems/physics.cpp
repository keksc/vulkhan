#include "physics.hpp"

#include "../entity.hpp"
#include <fmt/base.h>

namespace vkh {
namespace physicsSys {
void update(EngineContext &context) {
  for (auto &entity : context.entities) {
    if (entity.model == nullptr)
      fmt::print("player velocity XYZ: {}, {}, {}\n", entity.velocity.x, entity.velocity.y, entity.velocity.x);
    glm::vec3 force = entity.computeWeight();
    glm::vec3 acceleration = force / entity.mass;
    entity.velocity += acceleration * context.frameInfo.dt;
    entity.transform.position += entity.velocity * context.frameInfo.dt;
  }
}
} // namespace physicsSys
} // namespace vkh
