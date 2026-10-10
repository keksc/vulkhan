#include "../src/vkh/systems/wolf/wolfPacks.hpp"

#include <glm/gtx/quaternion.hpp>

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using vkh::wildlife::WolfPacks;

int main() {
  // The asset contract says muzzle +Z. Check the renderer's transform
  // points that muzzle along travel for cardinal and sloped headings,
  // without reflecting the mesh or turning its back into its belly.
  for (glm::vec3 direction :
       {glm::vec3(0, 0, -1), glm::vec3(1, 0, 0), glm::vec3(0, 0, 1),
        glm::vec3(-1, 0, 0), glm::vec3(1, .3f, -1)}) {
    direction = glm::normalize(direction);
    WolfPacks::Wolf wolf;
    wolf.position = {12, 4, -7};
    wolf.scale = 1.35f;
    wolf.orientation = glm::quatLookAt(direction, glm::vec3(0, 1, 0));
    const auto model = wolf.modelMatrix();
    const auto nose = glm::vec3(model * glm::vec4(0, 0, 1, 1));
    assert(glm::dot(glm::normalize(nose - wolf.position), direction) > .9999f);
    assert(glm::determinant(glm::mat3(model)) > 0);
    const auto up = glm::vec3(model * glm::vec4(0, 1, 0, 0));
    assert(glm::dot(up, wolf.orientation * glm::vec3(0, 1, 0)) > 1.f);
  }
  auto height = [](glm::vec2 p) {
    return .08f * p.x + .04f * p.y + std::sin(p.x * .03f);
  };
  auto blocked = [](glm::vec2 p) {
    return glm::distance(p, glm::vec2(-35, -40)) < 20.f;
  };
  WolfPacks a(height, blocked, {0, 5, 0});
  WolfPacks b(height, blocked, {0, 5, 0});
  const auto initial = a.interpolated();
  for (const auto &wolf : initial) {
    assert(wolf.active);
    assert(!blocked({wolf.position.x, wolf.position.z}));
  }
  size_t checked = 0;
  for (int frame = 0; frame < 3600; ++frame) {
    a.update(1.f / 60.f, {0, 5, 0});
    b.update(1.f / 60.f, {0, 5, 0});
    const auto wa = a.interpolated(), wb = b.interpolated();
    for (size_t i = 0; i < wa.size(); ++i) {
      const auto &w = wa[i];
      assert(glm::distance(w.position, wb[i].position) < 1e-6f);
      assert(std::isfinite(w.phase));
      assert(w.speed >= 0 && w.speed <= 6.9f);
      assert(std::abs(glm::length(w.orientation) - 1.f) < 1e-4f);
      assert(std::abs(w.position.y - height({w.position.x, w.position.z})) <
             .01f);
      assert(!blocked({w.position.x, w.position.z}));
      ++checked;
    }
  }
  // A normal launch must have an encounter pack near and in front of the
  // actual viewing direction, not three packs hundreds of metres away.
  for (glm::vec3 direction : {glm::vec3(0, 0, -1), glm::vec3(1, 0, 0),
                              glm::vec3(0, 0, 1), glm::vec3(-1, 0, 0)}) {
    WolfPacks encounters([](glm::vec2) { return 0.f; }, {}, {0, 1.8f, 0},
                         direction);
    for (size_t i = 0; i < WolfPacks::kPackSize; ++i) {
      const auto wolf = encounters.interpolated()[i];
      const glm::vec3 horizontal(wolf.position.x, 0, wolf.position.z);
      assert(wolf.active && glm::length(horizontal) < 90.f);
      assert(glm::dot(horizontal, direction) > 0);
    }
    // Walking through the world must keep replenishing nearby encounters.
    for (int step = 1; step <= 20; ++step) {
      const auto camera =
          direction * (float(step) * 350.f) + glm::vec3(0, 1.8f, 0);
      encounters.update(.1f, camera, direction);
      const auto wolf = encounters.interpolated()[0];
      assert(wolf.active && glm::distance(wolf.position, camera) < 95.f);
      assert(glm::dot(wolf.position - camera, direction) > 0);
    }
  }
  const auto before = a.interpolated();
  a.update(-1, {0, 0, 0});
  a.update(std::numeric_limits<float>::quiet_NaN(), {0, 0, 0});
  assert(a.interpolated()[0].position == before[0].position);
  // A long stall is bounded to four simulation ticks, without a teleport.
  a.update(100, {0, 5, 0});
  assert(glm::distance(a.interpolated()[0].position, before[0].position) < 1.f);
  // Remote groups recycle near the camera, without resetting nearby groups.
  a.update(.1f, {3000, 5, 3000});
  for (const auto &w : a.interpolated())
    assert(glm::distance(glm::vec2(w.position.x, w.position.z),
                         glm::vec2(3000, 3000)) < 435.f);
  // No valid dry ground: do not spawn animals in water.
  WolfPacks submerged(height, [](glm::vec2) { return true; }, {0, 5, 0});
  submerged.update(10, {0, 5, 0});
  for (const auto &w : submerged.interpolated())
    assert(!w.active);
  // Regression terrain: a deliberately flat core, with hills outside.
  // Packs must spawn on the hills, and keep traversing different elevations.
  auto hillHeight = [](glm::vec2 p) {
    return std::max(0.f, glm::length(p) - 250.f) * .30f;
  };
  const glm::vec3 hillCamera{300, hillHeight({300, 0}) + 2, 0};
  WolfPacks hills(hillHeight, {}, hillCamera);
  const auto start = hills.interpolated();
  for (const auto &w : start) {
    assert(w.active && w.position.y > 0.f);
    assert(w.scale >= 1.20f && w.scale <= 1.45f);
  }
  float ascent = 0, descent = 0;
  auto last = start;
  for (int frame = 0; frame < 10800; ++frame) {
    hills.update(1.f / 60.f, hillCamera, {0, 0, -1}, false);
    const auto wolves = hills.interpolated();
    for (size_t p = 0; p < WolfPacks::kPackCount; ++p) {
      const size_t i = p * WolfPacks::kPackSize;
      const float dy = wolves[i].position.y - last[i].position.y;
      // Count traversal, not distant pack recycling.
      if (glm::distance(wolves[i].position, last[i].position) < 1.f) {
        ascent += std::max(dy, 0.f);
        descent += std::max(-dy, 0.f);
      }
      assert(std::abs(wolves[i].position.y -
                      hillHeight({wolves[i].position.x,
                                  wolves[i].position.z})) < 1e-4f);
    }
    last = wolves;
  }
  assert(ascent > 15.f && descent > 15.f);
  std::cout << "Hill traversal: ascent=" << ascent << " descent=" << descent
            << " metres\n";
  std::cout << "Wolf pack simulation: " << checked << " states checked\n";
}
