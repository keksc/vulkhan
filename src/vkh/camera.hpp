#pragma once

#include <glm/glm.hpp>

#include "AxisAlignedBoundingBox.hpp"
#include "engineContext.hpp"

namespace vkh {

namespace camera {
  void calcViewDirection(EngineContext& context, glm::vec3 direction,
                         glm::vec3 up = glm::vec3{0.f, -1.f, 0.f});
  void calcViewTarget(EngineContext& context, glm::vec3 target,
                      glm::vec3 up = glm::vec3{0.f, -1.f, 0.f});
  void calcViewYXZ(EngineContext& context);

  Ray getPickingRay(const EngineContext &context, glm::vec2 mousePos);
  std::vector<glm::vec4> getFrustumPlanes(const glm::mat4& matrix);
};
} // namespace vkh
