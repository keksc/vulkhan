#pragma once

#include "engineContext.hpp"

namespace vkh {
void initAudio();
void cleanupAudio();
void updateAudio(EngineContext& context);
} // namespace vkh
