#include "terrainChunks.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace vkh::terrain {

// ---------------------------------------------------------------------------
// Addressing
// ---------------------------------------------------------------------------

float chunkCellSize(const ChunkParams &p, uint32_t lod) {
  return p.lod0CellSize * static_cast<float>(1u << lod);
}

float chunkSize(const ChunkParams &p, uint32_t lod) {
  return chunkCellSize(p, lod) * static_cast<float>(p.quadsPerChunk);
}

glm::vec2 chunkMin(const ChunkParams &p, const ChunkKey &k) {
  float s = chunkSize(p, k.lod);
  return glm::vec2(static_cast<float>(k.x) * s, static_cast<float>(k.z) * s);
}

Box chunkSelectionBox(const ChunkParams &p, const ChunkKey &k) {
  glm::vec2 mn = chunkMin(p, k);
  float s = chunkSize(p, k.lod);
  return {glm::vec3(mn.x, p.minY, mn.y), glm::vec3(mn.x + s, p.maxY, mn.y + s)};
}

float distanceToBox(const Box &b, const glm::vec3 &pt) {
  glm::vec3 d = glm::max(glm::max(b.min - pt, pt - b.max), glm::vec3(0.f));
  return glm::length(d);
}

ChunkKey childOf(const ChunkKey &k, uint32_t index) {
  return ChunkKey{k.x * 2 + static_cast<int32_t>(index & 1u),
                  k.z * 2 + static_cast<int32_t>(index >> 1u), k.lod - 1u};
}

bool chunkOverlapsZones(const ChunkParams &p, const ChunkKey &k,
                        const RefineZones &zones) {
  const glm::vec2 mn = chunkMin(p, k);
  const float s = chunkSize(p, k.lod);
  for (const RefineZone &z : zones)
    if (z.min.x < mn.x + s && z.max.x > mn.x && z.min.y < mn.y + s &&
        z.max.y > mn.y)
      return true;
  return false;
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

namespace {

bool inRange(const ChunkParams &p, const ChunkKey &k, const glm::vec3 &cam) {
  return distanceToBox(chunkSelectionBox(p, k), cam) <= p.viewDistance;
}

bool desiredSplit(const ChunkParams &p, const ChunkKey &k, const glm::vec3 &cam,
                  const RefineZones *zones) {
  if (k.lod == 0)
    return false;
  float d = distanceToBox(chunkSelectionBox(p, k), cam);
  if (d < p.splitFactor * chunkSize(p, k.lod))
    return true;
  // Lakes: keep the ground under/around them fine, whatever the distance
  // rule says (only within refineDistance; beyond it the error is sub-pixel).
  return zones && !zones->empty() && k.lod > p.refineMaxLod &&
         d <= p.refineDistance && chunkOverlapsZones(p, k, *zones);
}

template <typename Fn>
void forEachRoot(const ChunkParams &p, const glm::vec3 &cam, Fn &&fn) {
  const uint32_t rootLod = p.lodCount - 1u;
  const float s = chunkSize(p, rootLod);
  const int32_t x0 =
      static_cast<int32_t>(std::floor((cam.x - p.viewDistance) / s));
  const int32_t x1 =
      static_cast<int32_t>(std::floor((cam.x + p.viewDistance) / s));
  const int32_t z0 =
      static_cast<int32_t>(std::floor((cam.z - p.viewDistance) / s));
  const int32_t z1 =
      static_cast<int32_t>(std::floor((cam.z + p.viewDistance) / s));
  for (int32_t z = z0; z <= z1; ++z)
    for (int32_t x = x0; x <= x1; ++x) {
      ChunkKey k{x, z, rootLod};
      if (inRange(p, k, cam))
        fn(k);
    }
}

void selectRec(const ChunkParams &p, const glm::vec3 &cam, const ChunkKey &k,
               std::vector<ChunkKey> &out, const RefineZones *zones) {
  if (!inRange(p, k, cam))
    return;
  if (desiredSplit(p, k, cam, zones)) {
    for (uint32_t i = 0; i < 4; ++i)
      selectRec(p, cam, childOf(k, i), out, zones);
  } else {
    out.push_back(k);
  }
}

using ResidentFn = std::function<bool(const ChunkKey &)>;
using KeySet = std::unordered_set<ChunkKey, ChunkKeyHash>;

// True if this node's whole region can be drawn from resident chunks of this
// node or finer ones. Children outside view range never get requested, so
// they count as covered.
bool covered(const ChunkParams &p, const glm::vec3 &cam, const ChunkKey &k,
             const ResidentFn &resident) {
  if (resident(k))
    return true;
  if (k.lod == 0)
    return !inRange(p, k, cam);
  for (uint32_t i = 0; i < 4; ++i)
    if (!covered(p, cam, childOf(k, i), resident))
      return false;
  return true;
}

// Can the desired subtree below `k` be drawn without holes?
bool ready(const ChunkParams &p, const glm::vec3 &cam, const ChunkKey &k,
           const ResidentFn &resident, const KeySet &wanted) {
  if (!inRange(p, k, cam))
    return true;
  if (wanted.contains(k) || k.lod == 0)
    return covered(p, cam, k, resident);
  for (uint32_t i = 0; i < 4; ++i)
    if (!ready(p, cam, childOf(k, i), resident, wanted))
      return false;
  return true;
}

// Emits the coarsest resident chunks that tile `k`'s region.
void emitResidentCut(const ChunkKey &k, const ResidentFn &resident,
                     std::vector<ChunkKey> &out) {
  if (resident(k)) {
    out.push_back(k);
    return;
  }
  if (k.lod == 0)
    return;
  for (uint32_t i = 0; i < 4; ++i)
    emitResidentCut(childOf(k, i), resident, out);
}

void emitRec(const ChunkParams &p, const glm::vec3 &cam, const ChunkKey &k,
             const ResidentFn &resident, std::vector<ChunkKey> &out,
             const KeySet &wanted) {
  if (!inRange(p, k, cam))
    return;
  if (wanted.contains(k) || k.lod == 0) {
    emitResidentCut(k, resident, out);
    return;
  }
  if (ready(p, cam, k, resident, wanted)) {
    for (uint32_t i = 0; i < 4; ++i)
      emitRec(p, cam, childOf(k, i), resident, out, wanted);
  } else if (resident(k)) {
    out.push_back(k); // keep showing the coarse chunk until children arrive
  } else {
    for (uint32_t i = 0; i < 4; ++i) // best effort (startup): may have holes
      emitRec(p, cam, childOf(k, i), resident, out, wanted);
  }
}

} // namespace

void selectLeaves(const ChunkParams &p, const glm::vec3 &cam,
                  std::vector<ChunkKey> &out, const RefineZones *zones) {
  out.clear();
  if (p.leafBudget == 0) {
    forEachRoot(p, cam,
                [&](const ChunkKey &k) { selectRec(p, cam, k, out, zones); });
    return;
  }
  struct Candidate {
    float priority;
    ChunkKey key;
  };
  auto later = [](const Candidate &a, const Candidate &b) {
    return std::tuple(a.priority, a.key.lod, a.key.x, a.key.z) >
           std::tuple(b.priority, b.key.lod, b.key.x, b.key.z);
  };
  std::priority_queue<Candidate, std::vector<Candidate>, decltype(later)> queue(
      later);
  KeySet leaves;
  auto add = [&](const ChunkKey &k) {
    if (!inRange(p, k, cam))
      return;
    leaves.insert(k);
    if (desiredSplit(p, k, cam, zones))
      queue.push(
          {distanceToBox(chunkSelectionBox(p, k), cam) / chunkSize(p, k.lod),
           k});
  };
  forEachRoot(p, cam, add);
  while (!queue.empty()) {
    const ChunkKey k = queue.top().key;
    queue.pop();
    uint32_t children = 0;
    for (uint32_t i = 0; i < 4; ++i)
      children += inRange(p, childOf(k, i), cam);
    if (leaves.size() + children - 1 > p.leafBudget)
      continue;
    leaves.erase(k);
    for (uint32_t i = 0; i < 4; ++i)
      add(childOf(k, i));
  }
  out.assign(leaves.begin(), leaves.end());
  std::sort(out.begin(), out.end(), [](const ChunkKey &a, const ChunkKey &b) {
    return std::tuple(a.lod, a.x, a.z) < std::tuple(b.lod, b.x, b.z);
  });
}

void buildDrawList(const ChunkParams &p, const glm::vec3 &cam,
                   const std::function<bool(const ChunkKey &)> &isResident,
                   std::vector<ChunkKey> &out, const RefineZones *zones) {
  out.clear();
  std::vector<ChunkKey> desired;
  selectLeaves(p, cam, desired, zones);
  const KeySet wanted(desired.begin(), desired.end());
  forEachRoot(p, cam, [&](const ChunkKey &k) {
    emitRec(p, cam, k, isResident, out, wanted);
  });
}

// ---------------------------------------------------------------------------
// Mesh
// ---------------------------------------------------------------------------

size_t verticesPerChunk(const ChunkParams &p) {
  size_t g = p.quadsPerChunk + 1u;
  return g * g;
}

namespace {
void appendIndices(const ChunkParams &p, uint32_t step,
                   std::vector<uint32_t> &idx) {
  const uint32_t Q = p.quadsPerChunk;
  const uint32_t G = Q + 1u;
  if (step == 0 || Q % step != 0)
    step = 1;

  // Top surface: identical pattern/winding to the finite mountain mesh.
  for (uint32_t iz = 0; iz < Q; iz += step) {
    for (uint32_t ix = 0; ix < Q; ix += step) {
      uint32_t i0 = iz * G + ix;
      uint32_t i1 = i0 + step;
      uint32_t i2 = i0 + step * G;
      uint32_t i3 = i2 + step;
      idx.insert(idx.end(), {i0, i2, i1, i1, i2, i3});
    }
  }
}
} // namespace

std::vector<uint32_t>
buildChunkIndices(const ChunkParams &p,
                  std::array<IndexLod, kIndexLodCount> &ranges) {
  std::vector<uint32_t> idx;
  const size_t Q = p.quadsPerChunk;
  idx.reserve(Q * Q * 8u);
  for (uint32_t l = 0; l < kIndexLodCount; ++l) {
    ranges[l].first = static_cast<uint32_t>(idx.size());
    appendIndices(p, 1u << l, idx);
    ranges[l].count = static_cast<uint32_t>(idx.size()) - ranges[l].first;
  }
  return idx;
}

void generateChunk(const ChunkParams &p, const HeightFn &height,
                   const ChunkKey &key, std::vector<TerrainVertex> &out,
                   Box &bounds) {
  const uint32_t Q = p.quadsPerChunk;
  const uint32_t G = Q + 1u;
  const float cell = chunkCellSize(p, key.lod);
  const float size = chunkSize(p, key.lod);
  const glm::vec2 org = chunkMin(p, key);

  // LOD 0 (or fineNormals off): sample a (Q+3)^2 ring so the edge normals use
  // the neighbouring chunk's heights and match across seams. Otherwise only
  // the (Q+1)^2 grid is sampled and normals use extra taps at LOD 0 spacing.
  const bool ring = key.lod == 0 || !p.fineNormals ||
                    cell >= p.lod0CellSize * p.normalTapCells;
  const uint32_t R = ring ? Q + 3u : G;
  const int32_t off = ring ? 1 : 0;

  // Per-thread scratch: avoids a malloc/free of the height ring per chunk.
  thread_local std::vector<float> H;
  H.resize(static_cast<size_t>(R) * R);
  for (uint32_t rz = 0; rz < R; ++rz)
    for (uint32_t rx = 0; rx < R; ++rx) {
      glm::vec2 pos =
          org + glm::vec2(static_cast<float>(static_cast<int32_t>(rx) - off),
                          static_cast<float>(static_cast<int32_t>(rz) - off)) *
                    cell;
      float hv = height(pos, cell);
      // A NaN/Inf height would poison the chunk's bounds (and with them the
      // frustum test), so treat it as flat ground instead.
      if (!std::isfinite(hv))
        hv = 0.5f * (p.minY + p.maxY);
      H[static_cast<size_t>(rz) * R + rx] = hv;
    }
  auto h = [&](int32_t ix, int32_t iz) {
    return H[static_cast<size_t>(iz + off) * R + static_cast<size_t>(ix + off)];
  };

  out.resize(verticesPerChunk(p));
  float minY = std::numeric_limits<float>::max();
  float maxY = std::numeric_limits<float>::lowest();
  const float e = p.lod0CellSize;

  for (int32_t iz = 0; iz < static_cast<int32_t>(G); ++iz) {
    for (int32_t ix = 0; ix < static_cast<int32_t>(G); ++ix) {
      glm::vec2 xz =
          org +
          glm::vec2(static_cast<float>(ix), static_cast<float>(iz)) * cell;
      float y = h(ix, iz);
      float dHdx;
      float dHdz;
      if (ring) {
        dHdx = (h(ix + 1, iz) - h(ix - 1, iz)) / (2.f * cell);
        dHdz = (h(ix, iz + 1) - h(ix, iz - 1)) / (2.f * cell);
      } else {
        dHdx = (height(xz + glm::vec2(e, 0.f), e) -
                height(xz - glm::vec2(e, 0.f), e)) /
               (2.f * e);
        dHdz = (height(xz + glm::vec2(0.f, e), e) -
                height(xz - glm::vec2(0.f, e), e)) /
               (2.f * e);
        if (!std::isfinite(dHdx) || !std::isfinite(dHdz))
          dHdx = dHdz = 0.f;
      }
      TerrainVertex &v = out[static_cast<size_t>(iz) * G + ix];
      v.pos = glm::vec3(xz.x, y, xz.y);
      v.normal = glm::normalize(glm::vec3(-dHdx, 1.f, -dHdz));
      minY = std::min(minY, y);
      maxY = std::max(maxY, y);
    }
  }

  bounds.min = glm::vec3(org.x, minY, org.y);
  bounds.max = glm::vec3(org.x + size, maxY, org.y + size);
}

// ---------------------------------------------------------------------------
// Frustum
// ---------------------------------------------------------------------------

float sampleChunkHeight(const ChunkParams &p, const ChunkKey &key,
                        std::span<const float> heights, glm::vec2 xz,
                        std::span<const glm::vec4> edges) {
  const uint32_t Q = p.quadsPerChunk, G = Q + 1;
  const glm::vec2 grid =
      glm::clamp((xz - chunkMin(p, key)) / chunkCellSize(p, key.lod),
                 glm::vec2(0), glm::vec2(float(Q)));
  const uint32_t x = std::min(uint32_t(grid.x), Q - 1);
  const uint32_t z = std::min(uint32_t(grid.y), Q - 1);
  const glm::vec2 f = grid - glm::vec2(x, z);
  auto tap = [&](uint32_t ix, uint32_t iz) {
    // Same priority at corners as terrain.vert's fitted edge lookup.
    if (!edges.empty()) {
      if (iz == 0)
        return edges[ix].x;
      if (iz == Q)
        return edges[G + ix].x;
      if (ix == 0)
        return edges[2 * G + iz].x;
      if (ix == Q)
        return edges[3 * G + iz].x;
    }
    return heights[iz * G + ix];
  };
  const float h00 = tap(x, z), h10 = tap(x + 1, z);
  const float h01 = tap(x, z + 1), h11 = tap(x + 1, z + 1);
  // The mesh diagonal joins (x+1,z) to (x,z+1), not bilinear quads.
  if (f.x + f.y <= 1)
    return h00 + f.x * (h10 - h00) + f.y * (h01 - h00);
  return h11 + (1 - f.x) * (h01 - h11) + (1 - f.y) * (h10 - h11);
}

EdgeSamples extractEdges(const ChunkParams &p,
                         const std::vector<TerrainVertex> &vertices) {
  const uint32_t Q = p.quadsPerChunk, G = Q + 1;
  EdgeSamples result(4 * G);
  for (uint32_t i = 0; i < G; ++i) {
    const uint32_t ids[] = {i, Q * G + i, i * G, i * G + Q};
    for (uint32_t e = 0; e < 4; ++e) {
      const auto &v = vertices[ids[e]];
      result[e * G + i] = glm::vec4(v.pos.y, v.normal);
    }
  }
  return result;
}

void stitchEdges(const ChunkParams &p, const std::vector<ChunkKey> &keys,
                 const std::vector<const EdgeSamples *> &edges,
                 uint32_t indexStep, std::vector<glm::vec4> &out) {
  using Point = std::pair<int64_t, int64_t>;
  struct Rect {
    int64_t x, z, size;
  };
  const uint32_t Q = p.quadsPerChunk, G = Q + 1;
  std::vector<Rect> rects;
  for (const auto &k : keys) {
    const int64_t size = int64_t(Q) << k.lod;
    rects.push_back({k.x * size, k.z * size, size});
  }
  auto contains = [&](size_t i, Point pt) {
    const auto &r = rects[i];
    return pt.first >= r.x && pt.first <= r.x + r.size && pt.second >= r.z &&
           pt.second <= r.z + r.size;
  };
  auto rank = [&](size_t i) {
    return std::tuple(keys[i].lod, keys[i].x, keys[i].z);
  };
  // Neighbours only: don't scan the whole draw list for every edge sample.
  std::vector<std::vector<size_t>> touching(keys.size());
  for (size_t i = 0; i < keys.size(); ++i)
    for (size_t j = 0; j < keys.size(); ++j) {
      const auto &a = rects[i], &b = rects[j];
      if (a.x <= b.x + b.size && b.x <= a.x + a.size && a.z <= b.z + b.size &&
          b.z <= a.z + a.size)
        touching[i].push_back(j);
    }

  struct PointHash {
    size_t operator()(Point pt) const {
      uint64_t h = uint64_t(pt.first) * 0x9E3779B97F4A7C15ull;
      h ^= uint64_t(pt.second) + (h << 6) + (h >> 2);
      return static_cast<size_t>(h);
    }
  };
  std::unordered_map<Point, glm::vec4, PointHash> cache;
  cache.reserve(keys.size() * 2 * G);
  std::function<glm::vec4(Point, size_t)> resolve;
  resolve = [&](Point pt, size_t from) -> glm::vec4 {
    if (auto it = cache.find(pt); it != cache.end())
      return it->second;
    size_t owner = from;
    for (size_t i : touching[from])
      if (contains(i, pt) && rank(i) > rank(owner))
        owner = i;
    const auto &r = rects[owner];
    const bool alongX = pt.second == r.z || pt.second == r.z + r.size;
    const uint32_t edge =
        alongX ? (pt.second == r.z ? 0 : 1) : (pt.first == r.x ? 2 : 3);
    const int64_t cell = int64_t(1) << keys[owner].lod;
    const int64_t offset = alongX ? pt.first - r.x : pt.second - r.z;
    const uint32_t lo =
        std::min<uint32_t>(offset / (cell * indexStep) * indexStep, Q);
    const uint32_t hi = std::min(lo + indexStep, Q);
    auto tap = [&](uint32_t n) -> glm::vec4 {
      Point pos = alongX ? Point{r.x + n * cell, pt.second}
                         : Point{pt.first, r.z + n * cell};
      // Resolve endpoint corners on the next coarser surface too. Ownership
      // increases strictly, so even multi-level streaming transitions end.
      for (size_t i : touching[owner])
        if (contains(i, pos) && rank(i) > rank(owner))
          return resolve(pos, owner);
      return (*edges[owner])[edge * G + n];
    };
    const float t =
        hi == lo ? 0.f : float(offset - lo * cell) / float((hi - lo) * cell);
    glm::vec4 value = glm::mix(tap(lo), tap(hi), t);
    cache.emplace(pt, value);
    return value;
  };

  out.resize(keys.size() * 4 * G);
  for (size_t i = 0; i < keys.size(); ++i) {
    const auto &r = rects[i];
    const int64_t cell = int64_t(1) << keys[i].lod;
    for (uint32_t n = 0; n < G; ++n) {
      const Point points[] = {{r.x + n * cell, r.z},
                              {r.x + n * cell, r.z + r.size},
                              {r.x, r.z + n * cell},
                              {r.x + r.size, r.z + n * cell}};
      for (uint32_t e = 0; e < 4; ++e)
        out[(i * 4 + e) * G + n] = resolve(points[e], i);
    }
  }
}

std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4 &m) {
  glm::vec4 row0{m[0][0], m[1][0], m[2][0], m[3][0]};
  glm::vec4 row1{m[0][1], m[1][1], m[2][1], m[3][1]};
  glm::vec4 row2{m[0][2], m[1][2], m[2][2], m[3][2]};
  glm::vec4 row3{m[0][3], m[1][3], m[2][3], m[3][3]};

  std::array<glm::vec4, 6> planes = {
      row3 + row0, row3 - row0, row3 + row1, row3 - row1, row2, row3 - row2,
  };
  for (auto &pl : planes) {
    float len = glm::length(glm::vec3(pl));
    if (len > 1e-8f)
      pl /= len;
  }
  return planes;
}

bool boxInFrustum(const std::array<glm::vec4, 6> &planes, const Box &b) {
  for (const auto &pl : planes) {
    glm::vec3 positive(pl.x >= 0.f ? b.max.x : b.min.x,
                       pl.y >= 0.f ? b.max.y : b.min.y,
                       pl.z >= 0.f ? b.max.z : b.min.z);
    if (glm::dot(glm::vec3(pl), positive) + pl.w < 0.f)
      return false;
  }
  return true;
}

} // namespace vkh::terrain
