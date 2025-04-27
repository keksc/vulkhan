#pragma once

#include "engineContext.hpp"

#include <filesystem>

namespace vkh {
namespace audio {
void init();
void cleanup();
void update(EngineContext &context);
void play(const std::filesystem::path &file);
} // namespace audio
} // namespace vkh
