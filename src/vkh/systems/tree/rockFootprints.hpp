#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

namespace vkh::rocks {

struct Footprint {
  glm::vec2 center;
  float radius;
};

// Spatial bins keep grass exclusion independent of the total rock count.
// Workers see an immutable snapshot while streaming replaces the next one.
class Footprints {
  using Bins = std::unordered_map<uint64_t, std::vector<Footprint>>;
  std::atomic<std::shared_ptr<const Bins>> bins_{std::make_shared<Bins>()};
  static constexpr float cell = 4.f;
  static uint64_t key(int32_t x, int32_t z) {
    return (uint64_t(uint32_t(x)) << 32) | uint32_t(z);
  }

public:
  void replace(const std::vector<Footprint> &discs) {
    auto bins = std::make_shared<Bins>();
    for (const auto &d : discs) {
      int32_t x0 = std::floor((d.center.x - d.radius) / cell);
      int32_t z0 = std::floor((d.center.y - d.radius) / cell);
      int32_t x1 = std::floor((d.center.x + d.radius) / cell);
      int32_t z1 = std::floor((d.center.y + d.radius) / cell);
      for (int32_t z = z0; z <= z1; ++z)
        for (int32_t x = x0; x <= x1; ++x)
          (*bins)[key(x, z)].push_back(d);
    }
    bins_.store(std::move(bins));
  }
  bool blocked(glm::vec2 point) const {
    const auto bins = bins_.load();
    auto it =
        bins->find(key(std::floor(point.x / cell), std::floor(point.y / cell)));
    if (it == bins->end())
      return false;
    for (const auto &d : it->second) {
      const glm::vec2 delta = point - d.center;
      if (glm::dot(delta, delta) <= d.radius * d.radius)
        return true;
    }
    return false;
  }
};

} // namespace vkh::rocks
