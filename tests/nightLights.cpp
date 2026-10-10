#include "../src/vkh/nightLights.hpp"
#include <cassert>
#include <iostream>
int main() {
  using namespace vkh;
  static_assert(NightLights::activeCount == 99);
  static_assert(NightLights::activeCount * 64 + 25 * 8 <= 8192);
  for (int level = 0; level < 3; ++level) {
    float smallest = 1, largest = 0;
    for (int z = -20; z < 20; ++z)
      for (int x = -20; x < 20; ++x) {
        glm::ivec2 cell{x, z};
        auto p = nightPatchCenter(cell, level);
        assert(p == nightPatchCenter(cell, level));
        auto f = p / NightLights::spacing[level] - glm::vec2(cell);
        assert(f.x >= .179f && f.x <= .821f);
        assert(f.y >= .179f && f.y <= .821f);
        smallest = std::min(smallest, f.x);
        largest = std::max(largest, f.x);
      }
    assert(largest - smallest > .6f);
  }
  std::cout << "Stable, jittered world cells and particle capacity passed\n";
}
