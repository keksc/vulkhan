#pragma once
// Pure-CPU helpers for infinite, streamed grass (no Vulkan, unit-testable):
//
//  * HeightWindow: a camera-centred (height, grass-suitability) map built from
//    the terrain's height function. grassGenerate.comp only ever samples ONE
//    heightmap given an origin/size in the UBO, so "infinite" grass = keep
//    that heightmap a moving window around the camera and re-centre it
//    occasionally. Texels sit exactly on the terrain's LOD 0 vertex lattice,
//    results are packed as R16G16_SFLOAT (y, suitability) like
//    mountain::generate()'s heightmap, and re-centring only evaluates the
//    height function for texels the previous window didn't already have.
//  * collectVisibleTiles: enumerates the 1 m grass tiles around the camera
//    directly (no field-sized tile list, no field-sized scan per frame),
//    skipping 16x16-tile blocks that are out of range or outside the view
//    frustum and returning the rest nearest-first up to a capacity.

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <future>
#include <thread>
#include <vector>

namespace vkh::grass {

// ---------------------------------------------------------------------------
// Suitability (same heuristic as mountain.cpp's computeGrassSuitability)
// ---------------------------------------------------------------------------

inline float grassSuitability(float hNorm, float slopeNorm) {
  const float altitudeScore =
      1.f - glm::clamp((std::abs(hNorm - 0.5f) - 0.15f) / 0.2f, 0.f, 1.f);
  const float slopeScore = 1.f - glm::smoothstep(0.15f, 0.5f, slopeNorm);
  return glm::clamp(altitudeScore * slopeScore, 0.f, 1.f);
}

// ---------------------------------------------------------------------------
// Height window
// ---------------------------------------------------------------------------

struct WindowParams {
  uint32_t res{512};  // texels per side
  float cell{1.5f};   // world units per texel == terrain lod0CellSize
  float baseHeight{0.f};
  float heightRange{1.f};
  float rockSlopeAngleDeg{45.f};
  uint32_t threads{0}; // 0 = hardware_concurrency - 1 (at least 1)
};

// (xz, cellSize) -> world height. Called from worker threads.
using HeightFn = std::function<float(glm::vec2, float)>;
// true => no grass at xz (lakes, ...). Called from worker threads.
using BlockedFn = std::function<bool(glm::vec2)>;
// true => everything the blocked test needs is known for this world rect.
using RectReadyFn = std::function<bool(glm::vec2 rectMin, glm::vec2 rectMax)>;
// Extra multiplier on the spawn suitability at xz (biomes: see biome.hpp's
// grassDensity). hNorm = (y - baseHeight) / heightRange, clamped to [0,1].
// Called from worker threads.
using DensityFn = std::function<float(glm::vec2 xz, float hNorm)>;

struct WindowResult {
  glm::vec2 center{0.f};   // requested centre (world XZ)
  glm::vec2 worldMin{0.f}; // world XZ of texel (0,0)
  glm::vec2 worldSize{0.f};// (res-1) * cell: texel i is at worldMin + i*cell
  uint32_t res{0};
  float minY{0.f}, maxY{0.f};
  bool complete{true};     // blocked-test inputs were all available
  std::vector<uint32_t> texels; // packHalf2x16(y, suitability), row-major
};

class HeightWindow {
public:
  HeightWindow(WindowParams p, HeightFn height, BlockedFn blocked = {},
               RectReadyFn ready = {}, DensityFn density = {})
      : p_(p), height_(std::move(height)), blocked_(std::move(blocked)),
        ready_(std::move(ready)), density_(std::move(density)) {
    if (p_.threads == 0)
      p_.threads = std::max(1u, std::thread::hardware_concurrency() - 1u);
  }

  // Not thread-safe against itself: run one build at a time (the heights of
  // the previous window are cached and reused). Safe to run on a worker
  // thread while the main thread does other work.
  WindowResult build(glm::vec2 center) {
    const int32_t R = static_cast<int32_t>(p_.res);
    const int32_t G = R + 2; // 1-texel ring for central-difference slopes
    const int32_t ox = static_cast<int32_t>(std::lround(center.x / p_.cell)) - R / 2;
    const int32_t oz = static_cast<int32_t>(std::lround(center.y / p_.cell)) - R / 2;

    // ---- heights for lattice cells [ox-1, ox+R] x [oz-1, oz+R] -----------
    std::vector<float> H(static_cast<size_t>(G) * G);
    const bool haveOld = !oldH_.empty();
    parallelRows(G, [&](int32_t row) {
      const int32_t gz = oz - 1 + row;
      for (int32_t col = 0; col < G; ++col) {
        const int32_t gx = ox - 1 + col;
        float y;
        const int32_t orow = gz - (oldOz_ - 1), ocol = gx - (oldOx_ - 1);
        if (haveOld && orow >= 0 && orow < G && ocol >= 0 && ocol < G)
          y = oldH_[static_cast<size_t>(orow) * G + ocol];
        else
          y = height_(glm::vec2(gx * p_.cell, gz * p_.cell), p_.cell);
        H[static_cast<size_t>(row) * G + col] = y;
      }
    });

    WindowResult out;
    out.center = center;
    out.res = p_.res;
    out.worldMin = glm::vec2(ox * p_.cell, oz * p_.cell);
    out.worldSize = glm::vec2(static_cast<float>(R - 1) * p_.cell);
    out.complete = !ready_ || ready_(out.worldMin, out.worldMin + out.worldSize);
    out.texels.resize(static_cast<size_t>(R) * R);

    // ---- pack (y, suitability) -------------------------------------------
    const float cosRock = std::cos(glm::radians(p_.rockSlopeAngleDeg));
    const float inv2c = 1.f / (2.f * p_.cell);
    std::vector<float> rowMin(R, 1e30f), rowMax(R, -1e30f);
    parallelRows(R, [&](int32_t j) {
      float mn = 1e30f, mx = -1e30f;
      for (int32_t i = 0; i < R; ++i) {
        const size_t c = static_cast<size_t>(j + 1) * G + (i + 1);
        const float y = H[c];
        const float dHdx = (H[c + 1] - H[c - 1]) * inv2c;
        const float dHdz = (H[c + G] - H[c - G]) * inv2c;
        const float normalY = 1.f / std::sqrt(1.f + dHdx * dHdx + dHdz * dHdz);
        float slopeNorm = 0.f;
        if (cosRock < 1.f - 1e-5f)
          slopeNorm = glm::clamp((1.f - normalY) / (1.f - cosRock), 0.f, 1.f);
        const float hNorm = glm::clamp((y - p_.baseHeight) / p_.heightRange, 0.f, 1.f);
        float suit = grassSuitability(hNorm, slopeNorm);
        if (suit > 0.f && density_)
          suit *= glm::clamp(
              density_(glm::vec2((ox + i) * p_.cell, (oz + j) * p_.cell), hNorm),
              0.f, 1.f);
        if (suit > 0.f && blocked_ &&
            blocked_(glm::vec2((ox + i) * p_.cell, (oz + j) * p_.cell)))
          suit = 0.f;
        out.texels[static_cast<size_t>(j) * R + i] =
            glm::packHalf2x16(glm::vec2(y, suit));
        mn = std::min(mn, y);
        mx = std::max(mx, y);
      }
      rowMin[j] = mn;
      rowMax[j] = mx;
    });
    out.minY = *std::min_element(rowMin.begin(), rowMin.end());
    out.maxY = *std::max_element(rowMax.begin(), rowMax.end());

    oldH_ = std::move(H);
    oldOx_ = ox;
    oldOz_ = oz;
    return out;
  }

private:
  template <class Fn> void parallelRows(int32_t rows, Fn &&fn) {
    const uint32_t T = std::min<uint32_t>(p_.threads, static_cast<uint32_t>(rows));
    if (T <= 1) {
      for (int32_t r = 0; r < rows; ++r)
        fn(r);
      return;
    }
    // Interleaved rows: neighbouring rows cost about the same, and this
    // keeps the (cached-vs-computed) imbalance spread across threads.
    std::vector<std::future<void>> jobs;
    jobs.reserve(T - 1);
    for (uint32_t t = 1; t < T; ++t)
      jobs.push_back(std::async(std::launch::async, [&, t] {
        for (int32_t r = static_cast<int32_t>(t); r < rows; r += static_cast<int32_t>(T))
          fn(r);
      }));
    for (int32_t r = 0; r < rows; r += static_cast<int32_t>(T))
      fn(r);
    for (auto &j : jobs)
      j.get();
  }

  WindowParams p_;
  HeightFn height_;
  BlockedFn blocked_;
  RectReadyFn ready_;
  DensityFn density_;
  std::vector<float> oldH_;
  int32_t oldOx_{0}, oldOz_{0};
};

// ---------------------------------------------------------------------------
// Visible tile enumeration
// ---------------------------------------------------------------------------

struct TileBlockRef {
  float dist;
  int32_t bx, bz;
};

// Conservative AABB-vs-frustum (planes: inside iff dot(n, p) + w >= 0).
inline bool aabbInFrustum(const glm::vec4 *planes, const glm::vec3 &mn,
                          const glm::vec3 &mx) {
  for (int i = 0; i < 6; ++i) {
    const glm::vec4 &pl = planes[i];
    const glm::vec3 pos(pl.x >= 0.f ? mx.x : mn.x, pl.y >= 0.f ? mx.y : mn.y,
                        pl.z >= 0.f ? mx.z : mn.z);
    if (glm::dot(glm::vec3(pl), pos) + pl.w < 0.f)
      return false;
  }
  return true;
}

constexpr int32_t kTileBlock = 16; // tiles per block side

// Appends the origin (min corner) of every tile whose centre is within
// `reach` of `cam`, in blocks that intersect the frustum, nearest block
// first, until `capacity` tiles are in `out`. `yMin/yMax` bound the terrain
// (and blades) vertically for the block test. `scratch` is reused storage.
inline void collectVisibleTiles(const glm::vec2 &cam, float reach, float tileSize,
                                const glm::vec4 *frustumPlanes, float yMin,
                                float yMax, size_t capacity,
                                std::vector<TileBlockRef> &scratch,
                                std::vector<glm::vec2> &out) {
  out.clear();
  scratch.clear();
  const float B = static_cast<float>(kTileBlock) * tileSize;
  const float slack = tileSize * 2.f; // per-blade jitter/warp, see shader
  const int32_t bx0 = static_cast<int32_t>(std::floor((cam.x - reach) / B));
  const int32_t bx1 = static_cast<int32_t>(std::floor((cam.x + reach) / B));
  const int32_t bz0 = static_cast<int32_t>(std::floor((cam.y - reach) / B));
  const int32_t bz1 = static_cast<int32_t>(std::floor((cam.y + reach) / B));
  for (int32_t bz = bz0; bz <= bz1; ++bz)
    for (int32_t bx = bx0; bx <= bx1; ++bx) {
      const glm::vec2 mn(static_cast<float>(bx) * B, static_cast<float>(bz) * B);
      const glm::vec2 mx = mn + glm::vec2(B);
      const float d = glm::distance(cam, glm::clamp(cam, mn, mx));
      if (d > reach)
        continue;
      if (frustumPlanes &&
          !aabbInFrustum(frustumPlanes, glm::vec3(mn.x - slack, yMin - 1.f, mn.y - slack),
                         glm::vec3(mx.x + slack, yMax + 4.f, mx.y + slack)))
        continue;
      scratch.push_back({d, bx, bz});
    }
  std::sort(scratch.begin(), scratch.end(),
            [](const TileBlockRef &a, const TileBlockRef &b) { return a.dist < b.dist; });

  const float reachSq = reach * reach;
  for (const TileBlockRef &blk : scratch)
    for (int32_t tz = blk.bz * kTileBlock; tz < (blk.bz + 1) * kTileBlock; ++tz)
      for (int32_t tx = blk.bx * kTileBlock; tx < (blk.bx + 1) * kTileBlock; ++tx) {
        const glm::vec2 origin(static_cast<float>(tx) * tileSize,
                               static_cast<float>(tz) * tileSize);
        const glm::vec2 diff = origin + glm::vec2(tileSize * 0.5f) - cam;
        if (glm::dot(diff, diff) > reachSq)
          continue;
        out.push_back(origin);
        if (out.size() >= capacity)
          return;
      }
}

} // namespace vkh::grass
