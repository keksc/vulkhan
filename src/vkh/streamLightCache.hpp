#pragma once

#include "nightLights.hpp"
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <vector>

namespace vkh {
// The name is retained for callers, but sources are now stationary habitats.
// Terrain data is cached by world cell, never by a recycled GPU array slot.
class StreamLightCache {
public:
  static constexpr int queryBudget = 24;
  static constexpr size_t maxCachedTiles = 2048;

  template <typename Height, typename Blocked>
  NightLights update(glm::vec3 camera, Height height, Blocked blocked,
                     float time) {
    if (!std::isfinite(time))
      time = lastTime;
    if (time < lastTime)
      tiles.clear();
    lastTime = time;
    ++generation;
    NightLights output;
    output.grid = {camera.x, camera.z, 0, 0}; // diagnostic only, not shading
    int remaining = queryBudget, slot = 0;
    for (int level = 0; level < 3; ++level) {
      int half = NightLights::widths[level] / 2;
      auto eye = glm::ivec2(glm::floor(glm::vec2(camera.x, camera.z) /
                                       NightLights::spacing[level]));
      for (int z = -half; z <= half; ++z)
        for (int x = -half; x <= half; ++x, ++slot) {
          Key key{eye.x + x, eye.y + z, level};
          auto &tile = tiles[key];
          tile.lastSeen = generation;
          const glm::ivec2 cell(key.x, key.z);
          const auto center = nightPatchCenter(cell, level);
          const uint32_t seed = nightCellSeed(cell, level);
          const float radius = nightPatchRadius(seed, level);
          if (remaining > 0 && time >= tile.nextSample) {
            --remaining;
            const bool excluded = blocked(center);
            const float h = excluded ? NAN : height(center);
            // A known exclusion is a removal. A temporary absent height is
            // not: preserve the last successful sample without a timeout.
            if (excluded != tile.excluded) {
              tile.visibilityFrom = visibility(tile, time);
              tile.visibilityStart = time;
              tile.excluded = excluded;
            }
            if (std::isfinite(h)) {
              glm::vec3 plane{h, 0, 0};
              const float tap = radius;
              const float east = height(center + glm::vec2(tap, 0));
              const float west = height(center - glm::vec2(tap, 0));
              const float north = height(center + glm::vec2(0, tap));
              const float south = height(center - glm::vec2(0, tap));
              if (std::isfinite(east) && std::isfinite(west))
                plane.y = (east - west) / (2.f * tap);
              else if (tile.valid)
                plane.y = displayedPlane(tile, time).y;
              if (std::isfinite(north) && std::isfinite(south))
                plane.z = (north - south) / (2.f * tap);
              else if (tile.valid)
                plane.z = displayedPlane(tile, time).z;
              // Lift airborne insects over unresolved small terrain features.
              float clearance = 0;
              const std::array<float, 4> residuals{
                  east - h - plane.y * tap, west - h + plane.y * tap,
                  north - h - plane.z * tap, south - h + plane.z * tap};
              for (float residual : residuals)
                if (std::isfinite(residual))
                  clearance = std::max(clearance, residual);
              tile.clearanceFrom =
                  tile.valid ? glm::mix(tile.clearanceFrom, tile.clearance,
                                        blend(time - tile.changedAt))
                             : clearance;
              tile.clearance = clearance;
              tile.from = tile.valid ? displayedPlane(tile, time) : plane;
              tile.target = plane;
              tile.changedAt = time;
              if (!tile.valid) {
                tile.valid = true;
                tile.visibilityStart = time;
                tile.visibilityFrom = 0;
              }
            }
            constexpr std::array<float, 3> interval{1.f, 2.f, 4.f};
            tile.nextSample = time + (std::isfinite(h) ? interval[level] : .3f);
          }
          if (!tile.valid)
            continue;
          const auto plane = displayedPlane(tile, time);
          output.positions[slot] = {center.x, plane.x, center.y, radius};
          output.colors[slot] = {1.f, .68f, .24f, visibility(tile, time)};
          output.paths[slot] = {plane.y, plane.z,
                                glm::mix(tile.clearanceFrom, tile.clearance,
                                         blend(time - tile.changedAt)),
                                float(level) + nightRandom(seed + 79u) * .9f};
        }
    }
    // Prune only nonresident history. Residency itself is at most 99 entries.
    if (tiles.size() > maxCachedTiles) {
      // Select all evictions in one scan instead of scanning the entire
      // history again for each newly streamed cell during fast travel.
      std::vector<std::pair<Key, uint64_t>> candidates;
      candidates.reserve(tiles.size());
      for (const auto &[key, tile] : tiles)
        if (tile.lastSeen != generation)
          candidates.emplace_back(key, tile.lastSeen);
      const size_t count =
          std::min(tiles.size() - maxCachedTiles, candidates.size());
      if (count < candidates.size())
        std::nth_element(
            candidates.begin(), candidates.begin() + count, candidates.end(),
            [](const auto &a, const auto &b) { return a.second < b.second; });
      for (size_t i = 0; i < count; ++i)
        tiles.erase(candidates[i].first);
    }
    return output;
  }

  size_t cachedTiles() const { return tiles.size(); }

private:
  struct Key {
    int x, z, level;
    bool operator==(const Key &) const = default;
  };
  struct Hash {
    size_t operator()(const Key &key) const {
      return nightCellSeed({key.x, key.z}, key.level);
    }
  };
  struct Tile {
    glm::vec3 from{}, target{};
    float changedAt = 0, visibilityStart = 0, visibilityFrom = 0;
    float nextSample = -std::numeric_limits<float>::infinity();
    uint64_t lastSeen = 0;
    float clearance = 0, clearanceFrom = 0;
    bool valid = false, excluded = false;
  };
  static float blend(float value) {
    float t = glm::clamp(value, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
  }
  static glm::vec3 displayedPlane(const Tile &tile, float time) {
    return glm::mix(tile.from, tile.target, blend(time - tile.changedAt));
  }
  static float visibility(const Tile &tile, float time) {
    if (!tile.valid)
      return 0;
    return glm::mix(tile.visibilityFrom, tile.excluded ? 0.f : 1.f,
                    blend((time - tile.visibilityStart) / .6f));
  }
  std::unordered_map<Key, Tile, Hash> tiles;
  float lastTime = 0;
  uint64_t generation = 0;
};
} // namespace vkh
