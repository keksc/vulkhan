#include "../src/vkh/systems/particleEnvironment.hpp"
#include <cassert>
#include <iostream>
#include <limits>
int main() {
  auto flat = [](glm::vec2) { return 10.f; };
  auto clear = [](glm::vec2) { return false; };
  auto patches = vkh::particlePatches(glm::vec3{}, flat, clear);
  assert(patches.size() == 25);
  for (auto p : patches)
    assert(p.y == 10.f);
  assert(vkh::particlePatches(glm::vec3{}, flat, [](glm::vec2) {
           return true;
         }).empty());
  assert(vkh::particlePatches(
             glm::vec3{},
             [](glm::vec2) { return std::numeric_limits<float>::quiet_NaN(); },
             clear)
             .empty());
  assert(vkh::particlePatches(
             glm::vec3{}, [](glm::vec2 p) { return p.x; }, clear)
             .empty());
  auto moved = vkh::particlePatches(glm::vec3{800, 0, -800}, flat, clear);
  assert(moved.size() == 25 && moved[0].x > 700 && moved[0].z < -700);
  assert(vkh::particlePatches(glm::vec3{1, 0, 1}, flat, clear) == patches);
  auto slope = [](glm::vec2 p) { return p.x * .1f; };
  auto rooted = vkh::particlePatches(glm::vec3{}, slope, clear);
  assert(rooted.size() == 25);
  for (auto p : rooted)
    assert(std::abs(p.y - slope({p.x, p.z})) < .0001f);
  std::cout << "Particle placement checks passed\n";
}
