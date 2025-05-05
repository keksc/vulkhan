#pragma once

#include "engineContext.hpp"

namespace vkh {
namespace input {
void init(EngineContext &context);
void moveInPlaneXZ(EngineContext &context);
extern glm::dvec2 lastPos;
}; // namespace input
} // namespace vkh
