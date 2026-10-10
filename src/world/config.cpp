#include "config.hpp"
#include <filesystem>
#include <stdexcept>
#include <toml++/toml.hpp>

namespace vkh::world {
namespace {
const toml::table &table(const toml::node &node, const char *name) {
  auto t = node.as_table();
  if (!t)
    throw std::runtime_error(std::string(name) + " must be a table");
  return *t;
}
const toml::array &array(const toml::node &node, const char *name) {
  auto a = node.as_array();
  if (!a)
    throw std::runtime_error(std::string(name) + " must be an array");
  return *a;
}
float numeric(const toml::node &node, const char *name) {
  auto value = node.value<double>();
  if (!value || !std::isfinite(*value) || !std::isfinite(float(*value)))
    throw std::runtime_error(std::string(name) + " must be a finite number");
  return float(*value);
}
void number(const toml::table &t, const char *key, float &v) {
  if (auto item = t.get(key))
    v = numeric(*item, key);
}
void color(const toml::table &t, const char *key, glm::vec3 &v) {
  auto item = t.get(key);
  if (!item)
    return;
  auto &a = array(*item, key);
  if (a.size() != 3)
    throw std::runtime_error(std::string(key) + " must have three channels");
  for (size_t i = 0; i < 3; ++i)
    v[i] = numeric(a[i], key);
  if (glm::any(glm::lessThan(v, glm::vec3(0))) ||
      glm::any(glm::greaterThan(v, glm::vec3(1))))
    throw std::runtime_error(std::string(key) + " must be RGB in [0,1]");
}
uint32_t profileIndex(Config &c, const std::string &name) {
  for (uint32_t i = 0; i < c.profiles.size(); ++i)
    if (c.profiles[i].name == name)
      return i;
  throw std::runtime_error("Unknown biome profile: " + name);
}
void center(const toml::table &t, glm::vec2 &v) {
  auto item = t.get("center");
  if (!item)
    throw std::runtime_error("center is required");
  auto &a = array(*item, "center");
  if (a.size() != 2)
    throw std::runtime_error("center must be [x,z]");
  v = {numeric(a[0], "center"), numeric(a[1], "center")};
}
} // namespace
std::shared_ptr<Config> loadConfig(const std::string &path) {
  auto c = std::make_shared<Config>();
  if (!std::filesystem::exists(path))
    return c;
  try {
    auto root = toml::parse_file(path);
    if (auto node = root.get("terrain")) {
      auto &t = table(*node, "terrain");
      number(t, "baseHeight", c->terrain.baseHeight);
      number(t, "peakHeight", c->terrain.peakHeight);
      number(t, "featureSize", c->terrain.featureSize);
      number(t, "erosionScale", c->terrain.erosionScale);
      number(t, "erosionStrength", c->terrain.erosionStrength);
      if (c->terrain.peakHeight <= c->terrain.baseHeight ||
          c->terrain.featureSize <= 0 || c->terrain.erosionScale <= 0)
        throw std::runtime_error(
            "Terrain height range and feature scales must be positive");
    }
    if (auto node = root.get("climate")) {
      auto &t = table(*node, "climate");
      float temperatureSize = 1.f / c->climate.x,
            moistureSize = 1.f / c->climate.y;
      number(t, "temperatureFeatureSize", temperatureSize);
      number(t, "moistureFeatureSize", moistureSize);
      number(t, "altitudeLapse", c->climate.z);
      number(t, "homeRadiusInner", c->homeRadii.x);
      number(t, "homeRadiusOuter", c->homeRadii.y);
      if (temperatureSize <= 0 || moistureSize <= 0 || c->climate.z < 0 ||
          c->homeRadii.x < 0 || c->homeRadii.y <= c->homeRadii.x)
        throw std::runtime_error("Invalid climate scales or home radii");
      c->climate.x = 1.f / temperatureSize;
      c->climate.y = 1.f / moistureSize;
    }
    if (auto node = root.get("profiles"))
      for (auto &[key, entry] : table(*node, "profiles")) {
        std::string name(key.str());
        uint32_t id = 0;
        while (id < c->profiles.size() && c->profiles[id].name != name)
          ++id;
        if (id == c->profiles.size()) {
          if (id == maxProfiles)
            throw std::runtime_error("Too many biome profiles");
          Profile p;
          p.name = name;
          c->profiles.push_back(p);
        }
        auto &p = c->profiles[id];
        auto &t = table(entry, "profile");
        color(t, "ground", p.ground);
        color(t, "rock", p.rock);
        color(t, "grass", p.grass);
        color(t, "foliage", p.foliage);
        number(t, "grassDensity", p.grassDensity);
        number(t, "treeDensity", p.treeDensity);
        number(t, "dryness", p.dryness);
        number(t, "broadleaf", p.broadleaf);
        number(t, "snow", p.snow);
        if (p.grassDensity < 0 || p.treeDensity < 0 || p.dryness < 0 ||
            p.dryness > 1 || p.broadleaf < 0 || p.broadleaf > 1 || p.snow < 0 ||
            p.snow > 1)
          throw std::runtime_error("Density must be nonnegative; dryness, "
                                   "broadleaf and snow must be in [0,1]");
      }
    if (auto node = root.get("regions"))
      for (auto &entry : array(*node, "regions")) {
        if (c->regions.size() == maxRegions)
          throw std::runtime_error("Too many biome regions");
        auto &t = table(entry, "region");
        Region r;
        center(t, r.center);
        number(t, "radius", r.radius);
        number(t, "blend", r.blend);
        auto name = t["profile"].value<std::string>();
        if (!name)
          throw std::runtime_error("Region profile must be a string");
        r.profile = profileIndex(*c, *name);
        if (r.radius <= 0 || r.blend <= 0 || r.blend > r.radius)
          throw std::runtime_error(
              "Region radius/blend must be positive; blend <= radius");
        c->regions.push_back(r);
      }
    if (auto node = root.get("volcanoes"))
      for (auto &entry : array(*node, "volcanoes")) {
        if (c->volcanoes.size() == maxVolcanoes)
          throw std::runtime_error("Too many volcanoes");
        auto &t = table(entry, "volcano");
        Volcano v;
        glm::vec2 p;
        center(t, p);
        v.position = {p.x, 0, p.y};
        number(t, "radius", v.radius);
        number(t, "height", v.height);
        number(t, "crater", v.crater);
        number(t, "depth", v.depth);
        number(t, "plumeHeight", v.plumeHeight);
        color(t, "plumeColor", v.plumeColor);
        if (auto active = t.get("active")) {
          auto value = active->value<bool>();
          if (!value)
            throw std::runtime_error("active must be a boolean");
          v.active = *value;
        }
        if (v.radius <= 0 || v.height <= 0 || v.crater <= 0 ||
            v.crater >= .8f || v.depth < 0 || v.plumeHeight <= 0)
          throw std::runtime_error("Invalid volcano dimensions");
        c->volcanoes.push_back(v);
      }
    return c;
  } catch (const std::exception &e) {
    throw std::runtime_error("Invalid world config '" + path +
                             "': " + e.what());
  }
}
} // namespace vkh::world
