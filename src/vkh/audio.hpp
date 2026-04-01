#pragma once

#include "engineContext.hpp"

#include <AL/al.h>
#include <filesystem>

namespace vkh {
namespace audio {
void init();
void cleanup();
void update(EngineContext &context);
void setVolume(float volume);

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

  void setVolume(float volume);
  void setPitch(float pitch);
  void setLooping(bool loop);
  void seek(float seconds);

  void setPosition(float x, float y, float z);

  bool isPlaying() const;
  bool isPaused() const;
};

} // namespace audio
} // namespace vkh
