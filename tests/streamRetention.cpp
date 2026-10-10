#include "../src/vkh/streamLightCache.hpp"
#include <cassert>
#include <iostream>
int main() {
  vkh::StreamLightCache cache;
  bool missing = false, excluded = false;
  float altitude = 10;
  auto height = [&](glm::vec2) { return missing ? NAN : altitude; };
  auto blocked = [&](glm::vec2) { return excluded; };
  vkh::NightLights l;
  for (int i = 0; i < 100; ++i)
    l = cache.update({}, height, blocked, i * .02f);
  missing = true;
  for (int i = 100; i < 1000; ++i)
    l = cache.update({}, height, blocked, i * .02f);
  for (int i = 0; i < 99; ++i) {
    assert(l.positions[i].w > 0);
    assert(l.colors[i].w > .99f);
  }
  excluded = true;
  for (int i = 1000; i < 1100; ++i)
    l = cache.update({}, height, blocked, i * .02f);
  for (int i = 0; i < 99; ++i)
    assert(l.colors[i].w < .001f);
  excluded = false;
  missing = false;
  altitude = 20;
  bool interpolated = false;
  for (int i = 1100; i < 1200; ++i) {
    l = cache.update({}, height, blocked, i * .02f);
    for (int j = 0; j < 99; ++j)
      interpolated |= l.positions[j].y > 10.01f && l.positions[j].y < 19.99f;
  }
  assert(interpolated);
  for (int i = 0; i < 99; ++i) {
    assert(l.colors[i].w > .99f);
    assert(l.positions[i].y > 19.99f);
  }
  std::cout
      << "Long terrain gaps retained; exclusions fade; recovery interpolates\n";
}
