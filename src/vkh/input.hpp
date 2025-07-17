#pragma once

#include "engineContext.hpp"
#include "systems/entity/entities.hpp"

namespace vkh {
namespace input {
void init(EngineContext &context);
void update(EngineContext &context, EntitySys &entitySys);
extern glm::dvec2 lastPos;
}; // namespace input
} // namespace vkh
