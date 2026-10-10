#include "../src/vkh/streamLightCache.hpp"
#include <cassert>
#include <iostream>
int main() {
  vkh::StreamLightCache cache;
  int queries = 0;
  auto height = [&](glm::vec2 p) {
    ++queries;
    return 10.f + p.x * .1f + p.y * .2f;
  };
  auto blocked = [](glm::vec2) { return false; };
  vkh::NightLights lights;
  for (int frame = 0; frame < 100; ++frame) {
    queries = 0;
    lights = cache.update({0, 0, 0}, height, blocked, frame * .02f);
    assert(queries <= vkh::StreamLightCache::queryBudget * 5);
  }
  int valid = 0;
  for (auto p : lights.positions)
    valid += p.w > 0;
  assert(valid == 99);
  auto moved = cache.update({65, 0, 0}, height, blocked, 2.f);
  int overlap = 0;
  for (int a = 0; a < 99; ++a)
    for (int b = 0; b < 99; ++b)
      if (lights.positions[a].w > 0 && moved.positions[b].w > 0 &&
          lights.positions[a].x == moved.positions[b].x &&
          lights.positions[a].z == moved.positions[b].z) {
        assert(glm::length(lights.positions[a] - moved.positions[b]) < .001f);
        ++overlap;
      }
  assert(overlap >= 80);
  // Paused-clock travel must still evict history and stay bounded.
  for (int i = 0; i < 100; ++i)
    cache.update({i * 10000.f, 0, 0}, height, blocked, 2.f);
  assert(cache.cachedTiles() <= vkh::StreamLightCache::maxCachedTiles);
  std::cout << "Sampling budget, world anchoring and bounded cache passed\n";
}
