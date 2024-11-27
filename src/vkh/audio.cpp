#include "audio.hpp"

#include <AL/al.h>
#include <AL/alc.h>
#include <mpg123.h>
#include <fmt/format.h>

namespace vkh {
void checkOpenALError(const std::string &message) {
  ALenum error = alGetError();
  if (error != AL_NO_ERROR) {
    fmt::print("OpenAL Error in {}: {}\n", message, alGetString(error));
  }
}
void initAudio(EngineContext &context) {
  // Initialize OpenAL
  context.audio.device = alcOpenDevice(nullptr);
  if (!context.audio.device) {
    checkOpenALError("device creation");
  }

  context.audio.context = alcCreateContext(context.audio.device, nullptr);
  if (!context.audio.context || !alcMakeContextCurrent(context.audio.context)) {
  }

  // Initialize MPG123
  mpg123_init();
  mpg123_handle *mh = mpg123_new(nullptr, nullptr);
  if (!mh) {
  }

  // Open the MP3 file
  const char *filename = "sounds/The Yandere's Puppet Show.mp3";
  if (mpg123_open(mh, filename) != MPG123_OK) {
    mpg123_delete(mh);
    mpg123_exit();
  }

  // Get MP3 format information
  long rate;
  int channels, encoding;
  if (mpg123_getformat(mh, &rate, &channels, &encoding) != MPG123_OK) {
    mpg123_close(mh);
    mpg123_delete(mh);
    mpg123_exit();
  }

  // Determine OpenAL format
  ALenum format = (channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;

  // Read and decode the MP3 data
  std::vector<unsigned char> audioData;
  unsigned char buffer[4096];
  size_t bytesRead;
  while (mpg123_read(mh, buffer, sizeof(buffer), &bytesRead) == MPG123_OK) {
    audioData.insert(audioData.end(), buffer, buffer + bytesRead);
  }

  // Cleanup MPG123
  mpg123_close(mh);
  mpg123_delete(mh);
  mpg123_exit();

  // Create OpenAL buffer
  alGenBuffers(1, &context.audio.bufferID);
  // checkOpenALError("alGenBuffers");

  alBufferData(context.audio.bufferID, format, audioData.data(),
               audioData.size(), rate);
  // checkOpenALError("alBufferData");

  // Create OpenAL source
  alGenSources(1, &context.audio.sourceID);
  // checkOpenALError("alGenSources");

  alSourcei(context.audio.sourceID, AL_BUFFER, context.audio.bufferID);
  // checkOpenALError("alSourcei");

  // Set source position (invert Y-axis)
  alSource3f(context.audio.sourceID, AL_POSITION, 0.0f, -1.0f, 0.0f);
  alSource3f(context.audio.sourceID, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
  alSourcef(context.audio.sourceID, AL_GAIN, 1.0f);

  // Set listener position with inverted Y-axis
  alListener3f(AL_POSITION, 0.0f, -5.0f, 0.0f);
  alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);
  ALfloat orientation[] = {0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f};
  alListenerfv(AL_ORIENTATION, orientation);

  // Play the source
  alSourcePlay(context.audio.sourceID);
}
void cleanupAudio(EngineContext &context) {
  alDeleteSources(1, &context.audio.sourceID);
  alDeleteBuffers(1, &context.audio.bufferID);
  alcMakeContextCurrent(nullptr);
  alcDestroyContext(context.audio.context);
  alcCloseDevice(context.audio.device);
}
} // namespace vkh
