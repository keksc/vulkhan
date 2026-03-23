#pragma once

#include "engineContext.hpp"

#include <AL/al.h>
#include <filesystem>

namespace vkh {
namespace audio {
void init();
void cleanup();
void update(EngineContext &context);

struct Sound {
  Sound(const std::filesystem::path &file);
  ~Sound();

  Sound(const Sound &) = delete;
  Sound &operator=(const Sound &) = delete;
  Sound(Sound &&other) noexcept;
  Sound &operator=(Sound &&other) noexcept;

  ALuint source = 0;
  ALuint buffer = 0;

  void play(bool spacialized = false) const;
  void pause() const;
  void resume() const;
  void stop() const;

  void setVolume(float volume) const;
  void setPitch(float pitch) const;
  void setLooping(bool loop) const;
  void seek(float seconds) const;
  
  void setPosition(float x, float y, float z) const;

  bool isPlaying() const;
  bool isPaused() const;
};

} // namespace audio
} // namespace vkh
