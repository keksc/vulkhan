#pragma once

#include "engineContext.hpp"

#include "AudioFile.h"

namespace vkh {
void initAudio();
void cleanupAudio();
void updateAudio(float elapsedTime);
} // namespace vkh
