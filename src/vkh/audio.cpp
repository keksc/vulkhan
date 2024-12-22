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

#include "entity.hpp"

namespace vkh {
ALCdevice *device = nullptr;
ALCcontext *context = nullptr;
ALuint source = 0;
ALuint buffer = 0;
std::vector<short> data;
ALsizei dataSize = 0;
ALenum format = 0;
ALsizei sampleRate = 0;

std::vector<short> convertTo16Bit(const AudioFile<float> &audioFile) {
  std::vector<short> result;
  const auto &samples = audioFile.samples[0];
  for (float sample : samples) {
    result.push_back(static_cast<short>(sample * 32767.0f));
  }
  return result;
}
void initAudio() {
  device = alcOpenDevice(nullptr);
  if (!device) {
    throw std::runtime_error("Failed to open OpenAL device.");
  }

  context = alcCreateContext(device, nullptr);
  if (!context || !alcMakeContextCurrent(context)) {
    throw std::runtime_error("Failed to create or set OpenAL context.");
  }

  alGenSources(1, &source);
  alGenBuffers(1, &buffer);
  if (alGetError() != AL_NO_ERROR) {
    throw std::runtime_error("Error generating OpenAL source or buffer.");
  }

  AudioFile<float> audioFile;
  if (!audioFile.load("sounds/the-yanderes-puppet-show.wav")) {
    throw std::runtime_error("Failed to load WAV file.");
  }

  sampleRate = static_cast<ALsizei>(audioFile.getSampleRate());
  format = AL_FORMAT_MONO16;
  data = convertTo16Bit(audioFile);
  dataSize = static_cast<ALsizei>(data.size() * sizeof(short));

  alBufferData(buffer, format, data.data(), dataSize, sampleRate);
  alSourcei(source, AL_BUFFER, buffer);

  alSourcef(source, AL_ROLLOFF_FACTOR, 1.0f);
  alSourcef(source, AL_REFERENCE_DISTANCE, 1.0f);
  alSourcef(source, AL_MAX_DISTANCE, 10.0f);
  alSource3f(source, AL_POSITION, 5.f, .5f, 0.f);
  alSource3f(source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);

  alSourcePlay(source);

  alListenerf(AL_GAIN, 0.f); // mute master volume
}

void updateAudio(EngineContext &context) {
  const Transform &transform = context.entities[0].transform;
  glm::vec3 forward = glm::rotate(
      transform.orientation, glm::vec3(0.0f, 0.0f, 1.0f)); // Forward vector

  glm::vec3 up = glm::rotate(transform.orientation,
                             glm::vec3(0.0f, -1.0f, 0.0f)); // Up vector

  alListener3f(AL_POSITION, transform.position.x, transform.position.y,
               transform.position.z);
  float orientationArray[6] = {forward.x, forward.y, forward.z,
                               up.x,      up.y,      up.z};
  alListenerfv(AL_ORIENTATION, orientationArray);
}

void cleanupAudio() {
  alSourceStop(source);
  alDeleteSources(1, &source);
  alDeleteBuffers(1, &buffer);
  alcMakeContextCurrent(nullptr);
  if (context)
    alcDestroyContext(context);
  if (device)
    alcCloseDevice(device);
}
} // namespace vkh
