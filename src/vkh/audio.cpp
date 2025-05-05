#include "audio.hpp"
#include <stdexcept>

#include "AudioFile.h"
#include <AL/al.h>
#include <AL/alc.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace vkh {
namespace audio {
std::vector<short> convertTo16Bit(const AudioFile<float> &audioFile) {
  std::vector<short> result;
  const auto &samples = audioFile.samples[0];
  for (float sample : samples) {
    result.push_back(static_cast<short>(sample * 32767.0f));
  }
  return result;
}
Sound::Sound(const std::filesystem::path &file) {
  AudioFile<float> audioFile;
  if (!audioFile.load(file.string())) {
    throw std::runtime_error("Failed to load WAV file.");
  }

  auto data = convertTo16Bit(audioFile);

  alGenBuffers(1, &buffer);
  alBufferData(buffer, AL_FORMAT_MONO16, data.data(),
               static_cast<ALsizei>(data.size() * sizeof(short)),
               static_cast<ALsizei>(audioFile.getSampleRate()));
  alGenSources(1, &source);
  alSourcei(source, AL_BUFFER, buffer);
};
void Sound::play(ALint spacialized, ALint loop) {
  alSourcei(source, AL_LOOPING, loop);
  if (spacialized == AL_TRUE) {
    alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);
    alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);
  } else {
    alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);
  }
  alSourcePlay(source);
}
void Sound::stop() { alSourceStop(source); }
Sound::~Sound() {
  alDeleteSources(1, &source);
  alDeleteBuffers(1, &buffer);
}
ALCdevice *device = nullptr;
ALCcontext *context = nullptr;

void init() {
  device = alcOpenDevice(nullptr);
  if (!device) {
    throw std::runtime_error("Failed to open OpenAL device.");
  }

  context = alcCreateContext(device, nullptr);
  if (!context || !alcMakeContextCurrent(context)) {
    throw std::runtime_error("Failed to create or set OpenAL context.");
  }

  /*auto& source = sources.emplace_back();
  auto &buffer = buffers.emplace_back();
  alGenSources(1, &source);
  alGenBuffers(1, &buffer);
  if (alGetError() != AL_NO_ERROR) {
    throw std::runtime_error("Error generating OpenAL source or buffer.");
  }*/

  /*AudioFile<float> audioFile;
  if (!audioFile.load("sounds/lobby.wav")) {
    throw std::runtime_error("Failed to load WAV file.");
  }

  auto data = convertTo16Bit(audioFile);

  alBufferData(buffer, AL_FORMAT_MONO16, data.data(),
               static_cast<ALsizei>(data.size() * sizeof(short)),
               static_cast<ALsizei>(audioFile.getSampleRate()));
  alSourcei(source, AL_BUFFER, buffer);

  alSourcef(source, AL_ROLLOFF_FACTOR, 1.0f);
  alSourcef(source, AL_REFERENCE_DISTANCE, 1.0f);
  alSourcef(source, AL_MAX_DISTANCE, 10.0f);
  alSource3f(source, AL_POSITION, 0.f, 0.f, 0.f);
  alSource3f(source, AL_VELOCITY, 0.0f, 0.f, 0.f);

  alSourcePlay(source);*/

  // alListenerf(AL_GAIN, 0.f); // mute master volume
}
void update(EngineContext &context) {
  glm::vec3 forward =
      glm::rotate(context.camera.orientation, glm::vec3(0.0f, 0.0f, 1.0f));

  glm::vec3 up =
      glm::rotate(context.camera.orientation, glm::vec3(0.0f, 1.0f, 0.0f));

  alListener3f(AL_POSITION, context.camera.position.x,
               context.camera.position.y, context.camera.position.z);
  float orientationArray[6] = {forward.x, forward.y, forward.z,
                               up.x,      up.y,      up.z};
  alListenerfv(AL_ORIENTATION, orientationArray);

  // cleanup if time is elapsed
  //  for (auto &sound : sounds) {
  //    alSourceStop(source);
  //    alDeleteSources(1, &source);
  //  }
  //  for (auto &buffer : buffers) {
  //    alDeleteBuffers(1, &buffer);
  //  }
}
void play() {}
void cleanup() {
  alcMakeContextCurrent(nullptr);
  if (context)
    alcDestroyContext(context);
  if (device)
    alcCloseDevice(device);
}
} // namespace audio
} // namespace vkh
