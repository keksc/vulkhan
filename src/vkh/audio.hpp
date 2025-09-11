#pragma once

#include "engineContext.hpp"

#include <filesystem>

namespace vkh {
namespace audio {
void init();
void cleanup();
void update(EngineContext &context);

struct Sound {
  Sound(const std::filesystem::path &file);
  ALuint source;
  ALuint buffer;
  void play(ALint spacialized = AL_FALSE, ALint loop = AL_FALSE,
            float pitch = 1.f) const;
  void stop();
  ~Sound();
};
} // namespace audio
} // namespace vkh
