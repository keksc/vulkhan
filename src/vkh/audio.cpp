#include "audio.hpp"
#include <stdexcept>

#include "AudioFile.h"
#include <AL/al.h>
#include <AL/alc.h>
#include <cmath>

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
  alSource3f(source, AL_POSITION, 0.0f, 0.0f, -2.0f);
  alSource3f(source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);

  alSourcePlay(source);
}

void updateAudio(float elapsedTime) {
  //float speed = 1.0f;
  //float radius = 2.0f;
  //float angle = speed * elapsedTime;

  //float x = radius * cos(angle);
  //float z = radius * sin(angle);

  //alSource3f(source, AL_POSITION, x, 0.0f, z);
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
