#pragma once

#include "engineContext.hpp"

namespace vkh {
namespace audio {
void init();
void cleanup();
void update(EngineContext &context);
} // namespace audio
} // namespace vkh
