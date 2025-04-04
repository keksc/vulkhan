#pragma once

#include "../engineContext.hpp"

namespace vkh {
namespace particleSys {
void init(EngineContext &context);
void cleanup(EngineContext &context);
void render(EngineContext &context);
}; // namespace particlesSys
} // namespace vkh
