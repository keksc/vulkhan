#ifndef STREAM_CACHE_HEADER
#define STREAM_CACHE_HEADER "../src/vkh/streamLightCache.hpp"
#endif
#include STREAM_CACHE_HEADER

#include <cassert>
#include <chrono>
#include <iostream>

int main() {
  vkh::StreamLightCache cache;
  auto height = [](glm::vec2 p) { return 10.f + p.x * .001f; };
  auto blocked = [](glm::vec2) { return false; };
  const auto start = std::chrono::steady_clock::now();
  double checksum = 0;
  constexpr int frames = 2000;
  for (int i = 0; i < frames; ++i) {
    auto lights =
        cache.update({i * 10000.f, 0, -i * 10000.f}, height, blocked, 2.f);
    assert(cache.cachedTiles() <= vkh::StreamLightCache::maxCachedTiles);
    checksum += lights.positions[0].y;
  }
  const double ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start)
                        .count();
  // Current residents must survive eviction even when the clock is paused.
  const glm::vec3 end((frames - 1) * 10000.f, 0, -(frames - 1) * 10000.f);
  vkh::NightLights lights;
  for (int i = 0; i < 6; ++i)
    lights = cache.update(end, height, blocked, 2.f);
  for (int i = 0; i < vkh::NightLights::activeCount; ++i)
    assert(lights.positions[i].w > 0);
  const auto retained =
      cache.update(end, [](glm::vec2) { return NAN; }, blocked, 100.f);
  for (int i = 0; i < vkh::NightLights::activeCount; ++i)
    assert(retained.positions[i] == lights.positions[i]);
  std::cout << "Fast-travel cache: " << ms / frames << " ms/update; " << frames
            << " relocations; checksum=" << checksum << '\n';
}
