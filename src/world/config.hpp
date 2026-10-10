#pragma once
#include "../vkh/systems/grass/biome.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace vkh::world {
constexpr uint32_t maxProfiles = 16, maxRegions = 16, maxVolcanoes = 8;
struct Profile {
  std::string name;
  glm::vec3 ground{.2f, .13f, .06f}, rock{.25f, .25f, .25f};
  glm::vec3 grass{.28f, .42f, .10f}, foliage{.12f, .25f, .07f};
  float grassDensity{1}, treeDensity{1}, dryness{0}, broadleaf{0};
  float snow{1};
};
struct Region {
  glm::vec2 center{};
  float radius{400}, blend{100};
  uint32_t profile{};
};
struct Volcano {
  glm::vec3 position{}; // xyz: vent; y calculated from the terrain at startup
  float radius{180}, height{75}, crater{.25f}, depth{45};
  bool active{true};
  float plumeHeight{100};
  glm::vec3 plumeColor{.12f, .115f, .10f};
};
struct Terrain {
  float baseHeight{0}, peakHeight{120}, featureSize{100}, erosionScale{30};
  float erosionStrength{.22f};
  uint32_t erosionOctaves{5};
};
struct GpuProfile {
  glm::vec4 ground, rock, grass, foliage, controls;
};
struct GpuRegion {
  glm::vec4 centerRadius, selection;
};
struct GpuVolcano {
  glm::vec4 positionRadius, shape, plume;
};
struct GpuData {
  glm::vec4 climate, map, terrain;
  glm::uvec4 counts;
  std::array<GpuProfile, maxProfiles> profiles{};
  std::array<GpuRegion, maxRegions> regions{};
  std::array<GpuVolcano, maxVolcanoes> volcanoes{};
};
static_assert(sizeof(GpuProfile) == 80 && sizeof(GpuRegion) == 32 &&
              sizeof(GpuVolcano) == 48);
static_assert(sizeof(GpuData) == 2240);
struct Sample {
  glm::vec3 ground{}, rock{}, grass{}, foliage{};
  float grassDensity{}, treeDensity{}, dryness{}, broadleaf{}, snow{};
};
struct Config {
  Terrain terrain;
  glm::vec4 climate{1.f / 1500, 1.f / 1100, .8f, 0};
  glm::vec2 homeRadii{250, 900};
  std::vector<Profile> profiles;
  std::vector<Region> regions;
  std::vector<Volcano> volcanoes;
  Config();
  Sample sample(glm::vec2 xz, float altitude) const;
  float shapeHeight(glm::vec2 xz, float base) const;
  bool lakeAllowed(glm::vec3 center) const;
  GpuData gpuData() const;
};
std::shared_ptr<Config> loadConfig(const std::string &path);

inline Config::Config() {
  profiles = {{"meadow",
               {.2f, .13f, .06f},
               {.25f, .25f, .25f},
               {.28f, .42f, .10f},
               {.12f, .25f, .07f},
               1,
               1,
               0,
               .35f,
               1},
              {"steppe",
               {.3f, .21f, .11f},
               {.4f, .28f, .15f},
               {.50f, .37f, .12f},
               {.18f, .24f, .08f},
               .55f,
               .2f,
               .85f,
               .2f,
               1},
              {"desert",
               {.5f, .37f, .20f},
               {.42f, .26f, .14f},
               {.50f, .37f, .12f},
               {.18f, .24f, .08f},
               0,
               0,
               1,
               0,
               0},
              {"forest",
               {.07f, .045f, .025f},
               {.19f, .23f, .17f},
               {.22f, .35f, .09f},
               {.10f, .24f, .07f},
               .45f,
               1.5f,
               .05f,
               .65f,
               1},
              {"tundra",
               {.17f, .16f, .11f},
               {.25f, .26f, .28f},
               {.27f, .29f, .14f},
               {.10f, .22f, .09f},
               .06f,
               .25f,
               0,
               0,
               1},
              {"autumn",
               {.25f, .095f, .035f},
               {.32f, .25f, .19f},
               {.43f, .27f, .09f},
               {.52f, .065f, .016f},
               .70f,
               1.1f,
               .35f,
               1,
               .2f},
              {"volcanic",
               {.045f, .035f, .03f},
               {.08f, .065f, .055f},
               {.15f, .12f, .05f},
               {.10f, .10f, .07f},
               .02f,
               .02f,
               .8f,
               0,
               0}};
}
inline Sample Config::sample(glm::vec2 xz, float altitude) const {
  const float away = glm::smoothstep(homeRadii.x, homeRadii.y, glm::length(xz));
  float tn =
      glm::mix(.55f, biome::spread(biome::fbm(xz * climate.x, 0x1234u)), away);
  float mn = glm::mix(
      .52f, biome::spread(biome::fbm(xz * climate.y + 7.7f, 0x5678u)), away);
  float rag = (biome::noise(xz / 140.f, 0x9abcu) - .5f) * away;
  biome::Climate c{glm::mix(.15f, 1.f, tn) + rag * .1f, 0,
                   glm::clamp(mn - rag * .1f, 0.f, 1.f)};
  c.temp = c.tempBase - climate.z * altitude;
  auto b = biome::biomeAt(c);
  float weights[] = {b.meadow, b.steppe, b.desert, b.forest, b.tundra};
  Sample result;
  auto value = [](const Profile &p) {
    return Sample{p.ground,  p.rock,         p.grass,
                  p.foliage, p.grassDensity, p.treeDensity,
                  p.dryness, p.broadleaf,    p.snow};
  };
  auto mix = [](Sample a, Sample b, float t) {
    return Sample{glm::mix(a.ground, b.ground, t),
                  glm::mix(a.rock, b.rock, t),
                  glm::mix(a.grass, b.grass, t),
                  glm::mix(a.foliage, b.foliage, t),
                  glm::mix(a.grassDensity, b.grassDensity, t),
                  glm::mix(a.treeDensity, b.treeDensity, t),
                  glm::mix(a.dryness, b.dryness, t),
                  glm::mix(a.broadleaf, b.broadleaf, t),
                  glm::mix(a.snow, b.snow, t)};
  };
  // Weighted base distribution; palettes and density use exactly the same
  // weights.
  for (uint32_t i = 0; i < 5; ++i) {
    auto v = value(profiles[i]);
    float w = weights[i];
    result.ground += v.ground * w;
    result.rock += v.rock * w;
    result.grass += v.grass * w;
    result.foliage += v.foliage * w;
    result.grassDensity += v.grassDensity * w;
    result.treeDensity += v.treeDensity * w;
    result.dryness += v.dryness * w;
    result.broadleaf += v.broadleaf * w;
    result.snow += v.snow * w;
  }
  for (const auto &r : regions) {
    float w = 1 - glm::smoothstep(std::max(0.f, r.radius - r.blend), r.radius,
                                  glm::distance(xz, r.center));
    result = mix(result, value(profiles[r.profile]), w);
  }
  return result;
}
inline float Config::shapeHeight(glm::vec2 xz, float base) const {
  for (const auto &v : volcanoes) {
    float distance = glm::distance(xz, glm::vec2(v.position.x, v.position.z));
    float r = distance / v.radius;
    if (r >= 1)
      continue;
    float cone = v.height * std::pow(1 - r, 1.4f);
    float crater =
        v.depth * (1 - glm::smoothstep(v.radius * v.crater * .35f,
                                       v.radius * v.crater, distance));
    float fluting =
        std::sin(std::atan2(xz.y - v.position.z, xz.x - v.position.x) * 9.f) *
        std::sin(r * 3.14159265f) *
        glm::smoothstep(v.crater, v.crater * 1.5f, r) * cone * .035f;
    base += cone - crater + fluting;
  }
  return base;
}
inline bool Config::lakeAllowed(glm::vec3 center) const {
  for (const auto &v : volcanoes)
    if (v.active && glm::distance(glm::vec2(center.x, center.z),
                                  glm::vec2(v.position.x, v.position.z)) <
                        v.radius * v.crater * 1.5f)
      return false;
  return true;
}
inline GpuData Config::gpuData() const {
  GpuData g{};
  g.climate = climate;
  g.map = glm::vec4(homeRadii, 0, 0);
  g.terrain = {terrain.baseHeight, terrain.peakHeight - terrain.baseHeight, 0,
               0};
  g.counts = {profiles.size(), regions.size(), volcanoes.size(), 0};
  for (size_t i = 0; i < profiles.size(); ++i) {
    auto &p = profiles[i];
    g.profiles[i] = {
        glm::vec4(p.ground, p.snow), glm::vec4(p.rock, 0),
        glm::vec4(p.grass, 0), glm::vec4(p.foliage, 0),
        glm::vec4(p.grassDensity, p.treeDensity, p.dryness, p.broadleaf)};
  }
  for (size_t i = 0; i < regions.size(); ++i) {
    auto &r = regions[i];
    g.regions[i] = {glm::vec4(r.center, r.radius, r.blend),
                    glm::vec4(r.profile, 0, 0, 0)};
  }
  for (size_t i = 0; i < volcanoes.size(); ++i) {
    auto &v = volcanoes[i];
    g.volcanoes[i] = {
        glm::vec4(v.position, v.radius),
        glm::vec4(v.height, v.crater, v.depth, v.active ? 1.f : 0.f),
        glm::vec4(v.plumeColor, v.plumeHeight)};
  }
  return g;
}
} // namespace vkh::world
