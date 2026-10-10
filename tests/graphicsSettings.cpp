#include "../src/settings.hpp"
#include <cassert>
#include <cstddef>
#include <iostream>

int main() {
  paths::initCacheDir("vulkhan-ui-settings-test");
  auto &s = vkh::settings::current();
  s = vkh::Settings{};
  s.showFps = false;
  s.fullscreen = true;
  // A real older file ends before the appended graphics settings.
  {
    std::ofstream file(vkh::settings::filePath(), std::ios::binary);
    file.write(reinterpret_cast<const char *>(&s),
               offsetof(vkh::Settings, msaaSamples));
  }
  s = vkh::Settings{};
  vkh::settings::load();
  assert(!s.showFps && s.fullscreen);
  assert(s.msaaSamples == 4 && s.grassDistance == 120 &&
         s.terrainShadowSteps == 24);
  assert(s.fieldOfView == 110 && s.lightShaftStrength == 100 && s.postEffects &&
         s.drawGrass);
  s.fieldOfView = 93;
  s.lightShaftStrength = 67;
  s.postEffects = false;
  s.drawGrass = false;
  s.msaaSamples = 1;
  s.grassDistance = 40;
  s.terrainShadowSteps = 0;
  vkh::settings::save();
  s = vkh::Settings{};
  vkh::settings::load();
  assert(s.msaaSamples == 1 && s.grassDistance == 40 &&
         s.terrainShadowSteps == 0);
  assert(s.fieldOfView == 93 && s.lightShaftStrength == 67 && !s.postEffects &&
         !s.drawGrass);
  vkh::settings::validateGraphics();
  assert(s.fieldOfView == 93 && s.lightShaftStrength == 67);
  s.fieldOfView = 999;
  s.lightShaftStrength = 999;
  s.msaaSamples = 3;
  s.grassDistance = 10000;
  s.terrainShadowSteps = 9999;
  vkh::settings::validateGraphics();
  assert(s.msaaSamples == 4 && s.grassDistance == 120 &&
         s.terrainShadowSteps == 24);
  assert(s.fieldOfView == 110 && s.lightShaftStrength == 100);
  std::cout << "Legacy settings migration, graphics persistence, Off and "
               "invalid preset fallback passed\n";
}
