#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "engineContext.hpp"

namespace vkh {

namespace camera {
  void calcViewDirection(EngineContext& context, glm::vec3 direction,
                         glm::vec3 up = glm::vec3{0.f, -1.f, 0.f});
  void calcViewTarget(EngineContext& context, glm::vec3 target,
                      glm::vec3 up = glm::vec3{0.f, -1.f, 0.f});
  void calcViewYXZ(EngineContext& context);
};
} // namespace vkh
