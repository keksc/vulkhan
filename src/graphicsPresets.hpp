#pragma once
#include <array>
#include <cstdint>
namespace vkh {
inline constexpr std::array<uint32_t, 4> msaaPresets{1, 2, 4, 8};
inline uint32_t supportedMsaa(uint32_t requested, uint32_t supportedMask) {
  for (auto it = msaaPresets.rbegin(); it != msaaPresets.rend(); ++it)
    if (*it <= requested && (supportedMask & *it))
      return *it;
  return 1;
}
inline bool validMsaa(uint32_t samples) {
  for (auto preset : msaaPresets)
    if (samples == preset)
      return true;
  return false;
}
} // namespace vkh
