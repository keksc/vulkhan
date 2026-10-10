#pragma once
// Streaming forest placement for the infinite terrain. Pure CPU, no Vulkan.
//
//  * Trees are generated per square tile on worker threads, deterministically
//    (same tile => same trees, whatever order tiles stream in; jitter is
//    seeded by the GLOBAL cell index so tile borders are seamless).
//  * Every ready tile has a distance tier (Near / Mid / Far) with hysteresis,
//    which main.cpp maps to mesh LODs. Tiles beyond farDist are not loaded,
//    tiles beyond farDist + unloadMargin are dropped.
//  * Tile contents are shared_ptr<const vector>, so a snapshot of the whole
//    forest is a cheap vector of pointers that a worker thread can turn into
//    GPU instance data while the main thread keeps running (see LatestJob).

#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <thread>
#include <utility>
#include <vector>

namespace vkh::trees {

struct Instance {
  glm::vec3 position{};
  float yaw{};
  float scale{1.f};
  uint8_t species{0};
};

enum class Tier : uint8_t { Near, Mid, Far };

struct Params {
  float tileSize{160.f};  // world units per tile side
  float cellSize{7.f};    // one jittered candidate per cell => min spacing
  uint32_t speciesCount{1};
  float nearDist{140.f};  // closer than this: full-detail trees
  float midDist{450.f};   // up to here: low-poly, also drawn in reflections
  float farDist{1000.f};  // up to here: low-poly, main view only; load radius
  float hysteresis{30.f}; // tier boundaries move by this much on the way back
  float unloadMargin{250.f};
  float scaleMin{0.8f}, scaleMax{1.3f};
  uint32_t maxInFlight{3};        // concurrent tile generations
  uint32_t maxLaunchPerUpdate{2}; // new generations started per update()
  float moveThreshold{4.f};       // re-scan tiles only after this much motion
  uint32_t seed{1337};
};

// (xz) -> probability in [0,1] that a tree stands there (0: never; includes
// density, altitude/slope fade and lake exclusion), and the ground height in
// `groundY`. Called concurrently from worker threads: must be thread-safe.
using SampleFn = std::function<float(glm::vec2 xz, float &groundY)>;
// Optional gate: false => the tile's rectangle can't be generated yet (e.g.
// the lakes that overlap it haven't been analysed). Called on the main thread.
using ReadyFn = std::function<bool(glm::vec2 rectMin, glm::vec2 rectMax)>;

using TileTrees = std::shared_ptr<const std::vector<Instance>>;
struct TileView {
  TileTrees trees;
  Tier tier;
};
using Snapshot = std::vector<TileView>;

class TreeStreamer {
public:
  TreeStreamer(Params p, SampleFn sample, ReadyFn ready = {})
      : p_(p), sample_(std::move(sample)), ready_(std::move(ready)) {}

  // Main thread, once per frame. Returns true when the snapshot changed
  // (a tile became ready or was dropped, or a tile changed tier).
  bool update(const glm::vec3 &cam) {
    bool changed = harvest();

    const glm::vec2 c2(cam.x, cam.z);
    const bool moved =
        !lastScan_ || glm::distance(*lastScan_, c2) >= p_.moveThreshold;
    const bool waiting = inFlight_ > 0 || backlog_;
    if (!moved && !waiting)
      return changed;
    lastScan_ = c2;

    changed |= retier(cam);
    changed |= unload(cam);
    launchMissing(cam, p_.maxLaunchPerUpdate);
    return changed;
  }

  // Blocks (up to timeoutMs) until every launchable tile within `radius` of
  // `cam` is ready, so the first frames aren't bare. Gated tiles are skipped.
  void prewarm(const glm::vec3 &cam, float radius, int timeoutMs = 3000) {
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 <
           std::chrono::milliseconds(timeoutMs)) {
      harvest();
      retier(cam);
      size_t launched = launchMissing(cam, 1u << 20, radius);
      if (launched == 0 && inFlight_ == 0)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    harvest();
    retier(cam);
  }

  Snapshot snapshot() const {
    Snapshot s;
    s.reserve(tiles_.size());
    for (const auto &[k, t] : tiles_)
      if (t.trees)
        s.push_back({t.trees, t.tier});
    return s;
  }

  size_t readyTiles() const {
    size_t n = 0;
    for (const auto &[k, t] : tiles_)
      n += t.trees != nullptr;
    return n;
  }

private:
  using Key = std::pair<int32_t, int32_t>;
  struct Tile {
    std::future<TileTrees> fut;
    TileTrees trees; // null until generated
    Tier tier{Tier::Far};
  };

  float distTo(const glm::vec3 &cam, const Key &k) const {
    const float x0 = k.first * p_.tileSize, z0 = k.second * p_.tileSize;
    const float dx = std::max({x0 - cam.x, cam.x - (x0 + p_.tileSize), 0.f});
    const float dz = std::max({z0 - cam.z, cam.z - (z0 + p_.tileSize), 0.f});
    return std::sqrt(dx * dx + dz * dz);
  }

  Tier tierFor(float d, std::optional<Tier> cur) const {
    const float h = cur ? p_.hysteresis : 0.f;
    // Boundaries are pushed away from the current tier by `h` so a camera
    // hovering on a boundary doesn't flip the tile every frame.
    auto lim = [&](float edge, bool currentlyInside) {
      return currentlyInside ? edge + h : edge - h;
    };
    const bool inNear = cur && *cur == Tier::Near;
    const bool inNearOrMid = cur && *cur != Tier::Far;
    if (d < lim(p_.nearDist, inNear))
      return Tier::Near;
    if (d < lim(p_.midDist, inNearOrMid))
      return Tier::Mid;
    return Tier::Far;
  }

  bool harvest() {
    bool changed = false;
    for (auto &[k, t] : tiles_) {
      if (t.trees || !t.fut.valid())
        continue;
      if (t.fut.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        continue;
      t.trees = t.fut.get();
      --inFlight_;
      changed = true;
    }
    return changed;
  }

  bool retier(const glm::vec3 &cam) {
    bool changed = false;
    for (auto &[k, t] : tiles_) {
      if (!t.trees)
        continue;
      Tier nt = tierFor(distTo(cam, k), t.tier);
      if (nt != t.tier) {
        t.tier = nt;
        changed = true;
      }
    }
    return changed;
  }

  bool unload(const glm::vec3 &cam) {
    bool changed = false;
    const float limit = p_.farDist + p_.unloadMargin;
    for (auto it = tiles_.begin(); it != tiles_.end();) {
      // A tile still generating is left alone (its future would block).
      if (it->second.trees && distTo(cam, it->first) > limit) {
        it = tiles_.erase(it);
        changed = true;
      } else {
        ++it;
      }
    }
    return changed;
  }

  size_t launchMissing(const glm::vec3 &cam, size_t budget, float radius = -1.f) {
    if (radius < 0.f)
      radius = p_.farDist;
    const int r = static_cast<int>(std::ceil(radius / p_.tileSize)) + 1;
    const int cx = static_cast<int>(std::floor(cam.x / p_.tileSize));
    const int cz = static_cast<int>(std::floor(cam.z / p_.tileSize));

    std::vector<std::pair<float, Key>> want;
    for (int z = cz - r; z <= cz + r; ++z)
      for (int x = cx - r; x <= cx + r; ++x) {
        Key k{x, z};
        const float d = distTo(cam, k);
        if (d <= radius && !tiles_.contains(k))
          want.push_back({d, k});
      }
    std::sort(want.begin(), want.end(),
              [](auto &a, auto &b) { return a.first < b.first; });

    size_t launched = 0;
    backlog_ = false;
    for (auto &[d, k] : want) {
      if (ready_ &&
          !ready_(glm::vec2(k.first * p_.tileSize, k.second * p_.tileSize),
                  glm::vec2((k.first + 1) * p_.tileSize,
                            (k.second + 1) * p_.tileSize)))
        continue; // gated: retried on a later update
      if (launched >= budget || inFlight_ >= p_.maxInFlight) {
        backlog_ = true;
        break;
      }
      Params p = p_;
      SampleFn sample = sample_;
      Tile &t = tiles_[k];
      t.tier = tierFor(d, std::nullopt);
      t.fut = std::async(std::launch::async, [p, sample, k] {
        return generateTile(p, sample, k);
      });
      ++inFlight_;
      ++launched;
    }
    return launched;
  }

  static uint32_t hash(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu ^
                 (c + 0x165667B1u) * 0xC2B2AE35u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
  }
  static float u01(uint32_t h) { return (h >> 8) * (1.f / 16777216.f); }

  static TileTrees generateTile(const Params &p, const SampleFn &sample,
                                Key k) {
    auto out = std::make_shared<std::vector<Instance>>();
    const int cells = std::max(1, static_cast<int>(p.tileSize / p.cellSize));
    const float cs = p.tileSize / static_cast<float>(cells);
    const float x0 = k.first * p.tileSize, z0 = k.second * p.tileSize;
    out->reserve(static_cast<size_t>(cells) * cells / 4);

    for (int cz = 0; cz < cells; ++cz)
      for (int cx = 0; cx < cells; ++cx) {
        const int32_t gx = k.first * cells + cx, gz = k.second * cells + cz;
        const uint32_t h0 =
            hash(static_cast<uint32_t>(gx), static_cast<uint32_t>(gz), p.seed);
        const glm::vec2 xz(x0 + (cx + 0.1f + 0.8f * u01(h0)) * cs,
                           z0 + (cz + 0.1f + 0.8f * u01(hash(h0, 1, 2))) * cs);
        float y = 0.f;
        const float accept = sample(xz, y);
        if (accept <= 0.f || u01(hash(h0, 3, 4)) > accept)
          continue;
        Instance in;
        in.position = {xz.x, y, xz.y};
        in.yaw = u01(hash(h0, 5, 6)) * 6.2831853f;
        in.scale =
            glm::mix(p.scaleMin, p.scaleMax, u01(hash(h0, 7, 8)));
        in.species = static_cast<uint8_t>(hash(h0, 9, 10) %
                                          std::max(1u, p.speciesCount));
        out->push_back(in);
      }
    out->shrink_to_fit();
    return out;
  }

  Params p_;
  SampleFn sample_;
  ReadyFn ready_;
  std::map<Key, Tile> tiles_;
  uint32_t inFlight_{0};
  bool backlog_{false};
  std::optional<glm::vec2> lastScan_;
};

// Thread-safe set of "no trees here" regions (lakes), plus bookkeeping of
// which world tiles have been analysed so tree tiles that overlap an unknown
// tile can wait instead of planting trees in a lake that isn't known yet.
class ExclusionZones {
public:
  explicit ExclusionZones(float analysisTileSize) : tile_(analysisTileSize) {}

  // `mask` is maskRes.x * maskRes.y cells over the box centre +- size/2,
  // indexed [z * maskRes.x + x]; empty mask => the whole box is excluded.
  // `tile` is the analysis tile the zone came from (see clearTile()).
  void add(std::pair<int32_t, int32_t> tile, glm::vec2 center, glm::vec2 size,
           std::vector<bool> mask, glm::uvec2 maskRes) {
    std::unique_lock lock(m_);
    zones_.push_back({tile, center, size, std::move(mask), maskRes});
  }

  // Forgets a tile's zones and its "analysed" mark (call when the tile's
  // analysis is dropped; re-register it when it is analysed again so zones
  // never pile up as the camera moves back and forth).
  void clearTile(int32_t tx, int32_t tz) {
    std::unique_lock lock(m_);
    std::erase_if(zones_, [&](const Zone &z) {
      return z.tile == std::pair<int32_t, int32_t>{tx, tz};
    });
    analysed_.erase({tx, tz});
  }

  void markTileAnalysed(int32_t tx, int32_t tz) {
    std::unique_lock lock(m_);
    analysed_.insert({tx, tz});
  }

  // Are all analysis tiles (centred on multiples of the tile size) that
  // overlap the rectangle analysed?
  bool tilesReady(glm::vec2 rmin, glm::vec2 rmax) const {
    const int32_t x0 = static_cast<int32_t>(std::floor((rmin.x + tile_ * .5f) / tile_));
    const int32_t x1 = static_cast<int32_t>(std::floor((rmax.x + tile_ * .5f) / tile_));
    const int32_t z0 = static_cast<int32_t>(std::floor((rmin.y + tile_ * .5f) / tile_));
    const int32_t z1 = static_cast<int32_t>(std::floor((rmax.y + tile_ * .5f) / tile_));
    std::shared_lock lock(m_);
    for (int32_t z = z0; z <= z1; ++z)
      for (int32_t x = x0; x <= x1; ++x)
        if (!analysed_.contains({x, z}))
          return false;
    return true;
  }

  // True if `xz`, or a point `margin` away on either diagonal, is inside an
  // excluded footprint (the margin keeps trees off the shoreline).
  bool blocked(glm::vec2 xz, float margin) const {
    std::shared_lock lock(m_);
    if (zones_.empty())
      return false;
    const glm::vec2 pts[5] = {xz,
                              xz + glm::vec2(margin, margin),
                              xz + glm::vec2(-margin, margin),
                              xz + glm::vec2(margin, -margin),
                              xz + glm::vec2(-margin, -margin)};
    for (const Zone &z : zones_)
      for (const glm::vec2 &p : pts)
        if (inside(z, p))
          return true;
    return false;
  }

private:
  struct Zone {
    std::pair<int32_t, int32_t> tile;
    glm::vec2 center, size;
    std::vector<bool> mask;
    glm::uvec2 res;
  };
  static bool inside(const Zone &z, glm::vec2 p) {
    const glm::vec2 rel = p - (z.center - z.size * 0.5f);
    if (rel.x < 0.f || rel.y < 0.f || rel.x > z.size.x || rel.y > z.size.y)
      return false;
    if (z.res.x == 0 || z.res.y == 0 || z.mask.empty())
      return true;
    const uint32_t mx = std::min(
        static_cast<uint32_t>(rel.x / z.size.x * z.res.x), z.res.x - 1);
    const uint32_t mz = std::min(
        static_cast<uint32_t>(rel.y / z.size.y * z.res.y), z.res.y - 1);
    return z.mask[static_cast<size_t>(mz) * z.res.x + mx];
  }

  float tile_;
  mutable std::shared_mutex m_;
  std::vector<Zone> zones_;
  std::set<std::pair<int32_t, int32_t>> analysed_;
};

// Runs `fn` on a worker thread, one job at a time. If request() is called
// while a job runs, only the NEWEST pending input is kept and started as soon
// as the current one finishes - a burst of forest changes coalesces into one
// extra rebuild instead of a queue.
template <class Input, class Output> class LatestJob {
public:
  explicit LatestJob(std::function<Output(Input)> fn) : fn_(std::move(fn)) {}

  void request(Input in) {
    if (fut_.valid())
      pending_ = std::move(in);
    else
      launch(std::move(in));
  }

  bool busy() const { return fut_.valid(); }

  // Main thread: true (and fills `out`) when a job just finished.
  bool poll(Output &out) {
    if (!fut_.valid() ||
        fut_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
      return false;
    out = fut_.get();
    if (pending_) {
      Input next = std::move(*pending_);
      pending_.reset();
      launch(std::move(next));
    }
    return true;
  }

private:
  void launch(Input in) {
    fut_ = std::async(std::launch::async,
                      [this, in = std::move(in)]() mutable {
                        return fn_(std::move(in));
                      });
  }
  std::function<Output(Input)> fn_;
  std::future<Output> fut_;
  std::optional<Input> pending_;
};

} // namespace vkh::trees
