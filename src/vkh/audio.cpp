#include "audio.hpp"
#include <algorithm>
#include <stdexcept>
#include <vector>

#include <AL/al.h>
#include <AL/alc.h>
#include <opus/opusfile.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>

namespace vkh {
namespace audio {

std::vector<short> loadOpus(const std::string &filepath, int &sampleRate,
                            int &channels) {
  int error;
  OggOpusFile *opusFile = op_open_file(filepath.c_str(), &error);
  if (!opusFile) {
    throw std::runtime_error(
        std::format("Failed to open Opus file: {}, error {}", filepath, error));
  }

  const OpusHead *head = op_head(opusFile, -1);

  // FIX: op_read automatically downmixes surround sound to stereo.
  // We MUST cap channels at 2, otherwise we will read past the end of our
  // buffer.
  channels = (head->channel_count > 1) ? 2 : 1;
  sampleRate = 48000;

  std::vector<short> pcmData;
  const int BUFFER_SIZE = 4096;
  short buffer[BUFFER_SIZE * 2]; // account for stereo max

  while (true) {
    int samplesRead =
        op_read(opusFile, buffer, sizeof(buffer) / sizeof(buffer[0]), nullptr);
    if (samplesRead < 0) {
      op_free(opusFile);
      throw std::runtime_error("Error decoding Opus file data.");
    }
    if (samplesRead == 0)
      break;

    pcmData.insert(pcmData.end(), buffer, buffer + (samplesRead * channels));
  }

  op_free(opusFile);
  return pcmData;
}

Sound::Sound(const std::filesystem::path &file) {
  std::vector<short> pcmData;
  int sampleRate = 0;
  int channels = 1;

  std::string ext = file.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == ".opus") {
    pcmData = loadOpus(file.string(), sampleRate, channels);
  } else {
    throw std::runtime_error("Unsupported audio format: " + ext);
  }

  ALenum format = (channels == 2) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;

  alGenBuffers(1, &buffer);
  alBufferData(buffer, format, pcmData.data(),
               static_cast<ALsizei>(pcmData.size() * sizeof(short)),
               static_cast<ALsizei>(sampleRate));

  alGenSources(1, &source);
  alSourcei(source, AL_BUFFER, buffer);
}

// FIX: Move constructor
Sound::Sound(Sound &&other) noexcept
    : source(other.source), buffer(other.buffer) {
  other.source = 0;
  other.buffer = 0;
}

// FIX: Move assignment operator
Sound &Sound::operator=(Sound &&other) noexcept {
  if (this != &other) {
    if (source) {
      alSourceStop(source);
      alSourcei(source, AL_BUFFER, 0);
      alDeleteSources(1, &source);
    }
    if (buffer) {
      alDeleteBuffers(1, &buffer);
    }
    source = other.source;
    buffer = other.buffer;
    other.source = 0;
    other.buffer = 0;
  }
  return *this;
}

// FIX: Safely stop the source before unbinding and deleting the buffer
Sound::~Sound() {
  if (source) {
    alSourceStop(source);
    alSourcei(source, AL_BUFFER, 0);
    alDeleteSources(1, &source);
  }
  if (buffer) {
    alDeleteBuffers(1, &buffer);
  }
}

void Sound::play(bool spacialized) const {
  if (spacialized) {
    alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);
  } else {
    alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);
  }
  alSourcePlay(source);
}

void Sound::pause() const { alSourcePause(source); }
void Sound::resume() const { alSourcePlay(source); }
void Sound::stop() const { alSourceStop(source); }

void Sound::setVolume(float volume) { alSourcef(source, AL_GAIN, volume); }
void Sound::setPitch(float pitch) { alSourcef(source, AL_PITCH, pitch); }

void Sound::setLooping(bool loop) {
  alSourcei(source, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
}

void Sound::seek(float seconds) { alSourcef(source, AL_SEC_OFFSET, seconds); }

void Sound::setPosition(float x, float y, float z) {
  alSource3f(source, AL_POSITION, x, y, z);
}

bool Sound::isPlaying() const {
  ALint state;
  alGetSourcei(source, AL_SOURCE_STATE, &state);
  return state == AL_PLAYING;
}

bool Sound::isPaused() const {
  ALint state;
  alGetSourcei(source, AL_SOURCE_STATE, &state);
  return state == AL_PAUSED;
}

ALCdevice *device = nullptr;
ALCcontext *context = nullptr;

void init() {
  device = alcOpenDevice(nullptr);
  if (!device)
    throw std::runtime_error("Failed to open OpenAL device.");

  context = alcCreateContext(device, nullptr);
  if (!context || !alcMakeContextCurrent(context)) {
    throw std::runtime_error("Failed to create or set OpenAL context.");
  }
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
}

void setVolume(float volume) { alListenerf(AL_GAIN, volume); }

void cleanup() {
  alcMakeContextCurrent(nullptr);
  if (context)
    alcDestroyContext(context);
  if (device)
    alcCloseDevice(device);
}

} // namespace audio
} // namespace vkh
