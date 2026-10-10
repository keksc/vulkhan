#pragma once

#include "nightLights.hpp"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace vkh {

// World-space direction toward the sun, and wind travel direction on XZ.
// Shared by the main view, planar reflections, foliage, and wave spectrum.
struct Environment {
  NightLights lights;
  glm::vec3 sunDirection{0.65f, 0.50f, 0.35f};
  glm::vec3 sunColor{0.55f, 0.68f, 1.f};
  float sunIntensity{0.035f};
  glm::vec2 windDirection{1.f, 0.35f};
  float windSpeed{6.f}; // metres/second; gusts do not rebuild the FFT spectrum
  float windStrength{0.65f};
  float fogDensity{0.001f};
  float fogHeightFalloff{0.018f};
  float fogBaseHeight{24.f};
  float fogAnisotropy{0.65f};
  glm::vec3 fogColor{0.004f, 0.007f, 0.014f};
  glm::vec3 skyAmbient{0.008f, 0.012f, 0.022f};
  glm::vec3 groundAmbient{0.003f, 0.004f, 0.007f};
};

inline glm::vec3 safeSunDirection(const Environment &e) {
  const float length = glm::length(e.sunDirection);
  return std::isfinite(length) && length > 1e-5f ? e.sunDirection / length
                                                 : glm::vec3(0.f, 1.f, 0.f);
}
inline glm::vec2 safeWindDirection(const Environment &e) {
  const float length = glm::length(e.windDirection);
  return std::isfinite(length) && length > 1e-5f ? e.windDirection / length
                                                 : glm::vec2(1.f, 0.f);
}
inline float spectrumWindStrength(const Environment &e) {
  return glm::clamp((e.windSpeed - 0.5f) / 19.5f, 0.f, 1.f);
}

} // namespace vkh
