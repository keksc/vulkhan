#pragma once
#include <algorithm>
#include <cmath>
namespace UI {
inline float sliderValue(float position, float low, float high, float step) {
  if (high <= low)
    return low;
  float value = low + std::clamp(position, 0.f, 1.f) * (high - low);
  if (step > 0)
    value = low + std::round((value - low) / step) * step;
  return std::clamp(value, low, high);
}
} // namespace UI
