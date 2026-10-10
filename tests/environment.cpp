#include "../src/vkh/environment.hpp"
#include "../src/vkh/systems/tree/rockFootprints.hpp"
#include "../src/vkh/systems/tree/rockGeometry.hpp"
#include <cassert>
#include <iostream>
#include <limits>
int main() {
  vkh::rocks::Footprints footprints;
  assert(!footprints.blocked({0, 0}));
  footprints.replace({{{-4.1f, -0.1f}, 1.0f}, {{4.f, 4.f}, 0.3f}});
  assert(footprints.blocked({-4.1f, -0.1f}));
  assert(footprints.blocked({-3.2f, -0.1f}));
  assert(footprints.blocked({4.f, 4.f}));
  assert(!footprints.blocked({-2.9f, -0.1f}));
  assert(!footprints.blocked({0, 0}));
  footprints.replace({});
  assert(!footprints.blocked({4.f, 4.f}));
  vkh::Environment e;
  assert(std::abs(glm::length(vkh::safeSunDirection(e)) - 1.f) < 1e-6f);
  assert(std::abs(glm::length(vkh::safeWindDirection(e)) - 1.f) < 1e-6f);
  assert(std::abs(glm::mix(0.5f, 20.f, vkh::spectrumWindStrength(e)) -
                  e.windSpeed) < 1e-5f);
  e.sunDirection = glm::vec3(0);
  e.windDirection = glm::vec2(0);
  assert(vkh::safeSunDirection(e) == glm::vec3(0, 1, 0));
  assert(vkh::safeWindDirection(e) == glm::vec2(1, 0));
  e.sunDirection.x = std::numeric_limits<float>::quiet_NaN();
  assert(vkh::safeSunDirection(e) == glm::vec3(0, 1, 0));
  e.windSpeed = 0;
  assert(vkh::spectrumWindStrength(e) == 0);
  e.windSpeed = 100;
  assert(vkh::spectrumWindStrength(e) == 1);
  for (uint32_t seed = 0; seed < 64; ++seed) {
    auto a = vkh::rocks::generate(seed), b = vkh::rocks::generate(seed);
    assert(!a.vertices.empty() && a.indices.size() <= 320u * 3u);
    assert(a.vertices.size() == b.vertices.size());
    assert(a.min.y < 0 && a.max.y > 0);
    for (size_t i = 0; i < a.vertices.size(); ++i) {
      auto &v = a.vertices[i];
      assert(v.pos == b.vertices[i].pos);
      assert(std::isfinite(v.normal.x) && std::isfinite(v.normal.y) &&
             std::isfinite(v.normal.z));
      assert(std::abs(glm::length(v.normal) - 1.f) < 1e-5f);
      assert(glm::all(glm::greaterThanEqual(v.pos, a.min)));
      assert(glm::all(glm::lessThanEqual(v.pos, a.max)));
    }
    for (size_t i = 0; i < a.indices.size(); i += 3) {
      auto &v0 = a.vertices[a.indices[i]], &v1 = a.vertices[a.indices[i + 1]],
           &v2 = a.vertices[a.indices[i + 2]];
      auto cross = glm::cross(v1.pos - v0.pos, v2.pos - v0.pos);
      assert(glm::dot(cross, cross) > 1e-10f);
      assert(glm::dot(cross, v0.normal + v1.normal + v2.normal) > 0);
    }
  }
  std::cout
      << "Environment directions and spectrum mapping pass; 64 deterministic "
         "rock meshes have finite outward normals and valid bounds.\n";
}
