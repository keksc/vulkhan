#pragma once

#include "hudElements.hpp"
#include "../engineContext.hpp"

namespace vkh {
namespace hudSys {
void init(EngineContext &context);
void cleanup(EngineContext &context);

void update(EngineContext &context,
            View &content);
void render(EngineContext &context);
}; // namespace hudSys
} // namespace vkh
