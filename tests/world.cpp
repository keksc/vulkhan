#include "../src/world/config.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
int main(int argc, char **argv) {
  auto config =
      vkh::world::loadConfig(argc > 1 ? argv[1] : "config/world.toml");
  assert(config->profiles.size() == 7 && config->regions.size() == 2 &&
         config->volcanoes.size() == 1);
  auto autumn = config->sample({220, -100}, .35f);
  assert(autumn.foliage.r > autumn.foliage.g * 3 && autumn.broadleaf == 1.f);
  auto volcanic = config->sample({700, 300}, .7f);
  assert(volcanic.grassDensity < .03f && volcanic.snow == 0.f);
  auto &v = config->volcanoes.front();
  glm::vec2 center(v.position.x, v.position.z);
  float floor = config->shapeHeight(center, 50),
        rim =
            config->shapeHeight(center + glm::vec2(v.radius * v.crater, 0), 50);
  assert(rim > floor + 5);
  assert(std::abs(config->shapeHeight(center + glm::vec2(v.radius + 1, 0), 50) -
                  50) < 1e-6f);
  assert(!config->lakeAllowed({center.x, 50, center.y}));
  v.active = false;
  assert(config->lakeAllowed({center.x, 50, center.y}));
  auto data = config->gpuData();
  assert(data.counts.x == 7 && data.counts.y == 2 && data.counts.z == 1);
  assert(data.volcanoes[0].shape.w == 0);
  for (int x = -1000; x <= 1000; x += 5) {
    auto a = config->sample({float(x), 37}, .5f),
         b = config->sample({float(x) + .001f, 37}, .5f);
    assert(glm::length(a.grass - b.grass) < .002f);
    assert(a.grassDensity >= 0 && a.snow >= 0 && a.snow <= 1.0001f);
    assert(std::isfinite(config->shapeHeight({float(x), float(x)}, 50)));
  }
  const auto temp =
      std::filesystem::temp_directory_path() / "vulkhan-world-test.toml";
  {
    std::ofstream f(temp);
    f << R"([profiles.purple]
grass = [0.4, 0.05, 0.5]
treeDensity = 0.2
[[regions]]
profile = "purple"
center = [0, 0]
radius = 100
blend = 20
)";
  }
  auto custom = vkh::world::loadConfig(temp.string());
  assert(custom->profiles.size() == 8);
  assert(glm::length(custom->sample({0, 0}, .4f).grass -
                     glm::vec3(.4f, .05f, .5f)) < 1e-6f);
  {
    std::ofstream f(temp);
    f << "[[regions]]\nprofile = 'missing'\ncenter = [0, 0]\n";
  }
  bool rejected = false;
  try {
    vkh::world::loadConfig(temp.string());
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  assert(rejected);
  {
    std::ofstream f(temp);
    f << "[[volcanoes]]\ncenter = [0, 0]\nradius = 0\n";
  }
  rejected = false;
  try {
    vkh::world::loadConfig(temp.string());
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  assert(rejected);
  // TOML syntax errors, duplicate keys, and wrong field types must fail rather
  // than silently reverting to defaults.
  for (const auto &invalid :
       {"[terrain\npeakHeight = 100\n",
        "[terrain]\npeakHeight = 100\npeakHeight = 120\n",
        "[terrain]\nfeatureSize = 'large'\n", "profiles = 42\n",
        "[profiles.autumn]\nfoliage = [0.5, nan, 0.1]\n",
        "[[volcanoes]]\ncenter = [0, 0]\nactive = 'true'\n"}) {
    {
      std::ofstream f(temp);
      f << invalid;
    }
    rejected = false;
    try {
      vkh::world::loadConfig(temp.string());
    } catch (const std::runtime_error &e) {
      rejected = true;
      assert(std::string(e.what()).find(temp.string()) != std::string::npos);
    }
    assert(rejected);
  }
  std::filesystem::remove(temp);
  std::cout << "Config loading, custom profiles, blend continuity, volcano "
               "rim/floor, lake exclusion and GPU packing pass.\n";
}
