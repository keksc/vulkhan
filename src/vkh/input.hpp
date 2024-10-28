#pragma once

#include "camera.hpp"
#include "engineContext.hpp"

namespace vkh {
namespace input {

void init(EngineContext &context);
void moveInPlaneXZ(EngineContext &context);

}; // namespace controller
} // namespace vkh
