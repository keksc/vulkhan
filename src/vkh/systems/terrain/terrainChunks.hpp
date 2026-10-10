#pragma once

// Pure-CPU half of the chunked terrain: chunk addressing, quadtree LOD
// selection, the "which resident chunks do we actually draw" cut, mesh
// generation, and frustum tests. Deliberately free of Vulkan so it can be
// unit-tested and run on worker threads (TerrainSys owns all the GPU side).

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace vkh::terrain {

// One square chunk of terrain. A chunk of LOD `lod` covers the world-space
// XZ rectangle [x*S, (x+1)*S) x [z*S, (z+1)*S) where S = chunkSize(lod).
// Each LOD doubles S (and the vertex spacing) while the vertex count per
// chunk stays fixed, so a LOD L+1 chunk is exactly the union of four LOD L
// chunks - a quadtree.
struct ChunkKey {
  int32_t x{};
  int32_t z{};
  uint32_t lod{};
  bool operator==(const ChunkKey &) const = default;
};

struct ChunkKeyHash {
  size_t operator()(const ChunkKey &k) const noexcept {
    uint64_t h = (uint64_t(uint32_t(k.x)) << 32) ^ uint64_t(uint32_t(k.z));
    h ^= uint64_t(k.lod + 1u) * 0x9E3779B97F4A7C15ull;
    h ^= h >> 29;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 32;
    return static_cast<size_t>(h);
  }
};

struct Box {
  glm::vec3 min{};
  glm::vec3 max{};
};

// World-space XZ rectangle that must stay faithful to the height function the
// lake builder used (a lake's padded bounding box). Chunks overlapping one are
// (a) sampled with a pinned erosion-octave count, so their ground is the same
// surface at every LOD, and (b) refined to at most ChunkParams::refineMaxLod
// while the camera is within ChunkParams::refineDistance, so the coarse mesh
// does not cut across the shoreline the water was fitted to.
struct RefineZone {
  glm::vec2 min{};
  glm::vec2 max{};
};
using RefineZones = std::vector<RefineZone>;

struct ChunkParams {
  // World units between neighbouring vertices at LOD 0. Pass the same value
  // the finite mountain used (worldSize / (resolution - 1)) and LOD 0 chunks
  // carry exactly the same erosion octaves it did.
  float lod0CellSize{1.5f};
  // Quads per chunk side (vertices = quads + 1), identical for every LOD.
  uint32_t quadsPerChunk{64};
  // Number of LOD levels. Roots of the quadtree are the coarsest level.
  uint32_t lodCount{6};
  // A node is split into its four children while the camera is closer than
  // splitFactor * nodeSize (distance to the node's bounding box). Higher =
  // detail extends further out (more chunks).
  float splitFactor{1.5f};
  // Nothing farther than this (3D distance to the chunk box) is generated or
  // drawn. Should be <= the camera's far plane.
  float viewDistance{5000.f};
  // Conservative world-space Y range of the terrain, used ONLY for LOD
  // distance calculations (actual chunk bounds come from generated heights).
  float minY{0.f};
  float maxY{100.f};
  // If true, LOD >= 1 chunks compute normals from extra height samples taken
  // at LOD 0's spacing, so shading/slope is identical whatever the LOD
  // (otherwise coarse chunks get smoothed normals and layer colours pop at
  // LOD borders). Costs ~5x the height samples for coarse chunks.
  bool fineNormals{true};
  // Chunks whose vertex spacing is >= lod0CellSize * normalTapCells compute
  // normals from the (Q+3)^2 height ring (1 sample per vertex) instead of
  // 4 extra fine taps per vertex: at that spacing fine taps only alias, and
  // generation gets ~5x cheaper for every coarse chunk.
  float normalTapCells{4.f};
  // Chunks overlapping a RefineZone are split down to this LOD (inclusive)
  // while their box is within refineDistance of the camera.
  uint32_t refineMaxLod{0};
  float refineDistance{1200.f};
  // 0 = unrestricted. Reserve slot-pool headroom for streaming transitions.
  uint32_t leafBudget{0};
};

// World-space height (Y) at an XZ position. `cellSize` is the vertex spacing
// the sample will be used at (the erosion filter drops octaves finer than the
// grid can show). MUST be thread-safe: it is called from worker threads.
using HeightFn = std::function<float(glm::vec2 worldXZ, float cellSize)>;

struct TerrainVertex {
  glm::vec3 pos{};
  glm::vec3 normal{};
};

// ---- Addressing ------------------------------------------------------------
float chunkCellSize(const ChunkParams &p, uint32_t lod);
float chunkSize(const ChunkParams &p, uint32_t lod);
glm::vec2 chunkMin(const ChunkParams &p, const ChunkKey &k);
// Box used for LOD/range decisions (uses ChunkParams::minY/maxY).
Box chunkSelectionBox(const ChunkParams &p, const ChunkKey &k);
float distanceToBox(const Box &b, const glm::vec3 &pt);
ChunkKey childOf(const ChunkKey &k, uint32_t index /*0..3*/);
// True if the chunk's XZ rectangle intersects any zone.
bool chunkOverlapsZones(const ChunkParams &p, const ChunkKey &k,
                        const RefineZones &zones);

// ---- Selection
// ---------------------------------------------------------------- The desired
// set of leaf chunks around `cam`: a gap-free, overlap-free quadtree cut,
// finest near the camera.
void selectLeaves(const ChunkParams &p, const glm::vec3 &cam,
                  std::vector<ChunkKey> &out,
                  const RefineZones *zones = nullptr);

// Which RESIDENT chunks to draw this frame. Starts from the desired cut but
// never shows a hole while new chunks are still streaming in: a node keeps
// drawing its coarse (resident) self until every desired leaf below it is
// covered, and when merging it draws the finer chunks it still has until the
// coarse one arrives. The result never contains overlapping chunks.
void buildDrawList(const ChunkParams &p, const glm::vec3 &cam,
                   const std::function<bool(const ChunkKey &)> &isResident,
                   std::vector<ChunkKey> &out,
                   const RefineZones *zones = nullptr);

// ---- Mesh
// --------------------------------------------------------------------- Surface
// vertices only: (quads+1)^2, row-major. No downward skirts.
size_t verticesPerChunk(const ChunkParams &p);
// The shared index buffer holds the same chunk topology at several
// resolutions (every vertex, every 2nd, every 4th). Skirts use the matching
// subset, so all of them share the chunk's one vertex slot. Coarser sets are
// for passes that don't need the detail (planar reflections).
constexpr uint32_t kIndexLodCount = 3;
struct IndexLod {
  uint32_t first{}; // offset into the shared index buffer
  uint32_t count{};
};
// Index buffer shared by EVERY chunk: surface triangles only.
// Winding matches the finite mountain mesh.
std::vector<uint32_t>
buildChunkIndices(const ChunkParams &p,
                  std::array<IndexLod, kIndexLodCount> &ranges);
// Generates one chunk's vertices (world space). Thread-safe if `height` is.
void generateChunk(const ChunkParams &p, const HeightFn &height,
                   const ChunkKey &key, std::vector<TerrainVertex> &outVerts,
                   Box &outBounds);

// Height on the actual triangle surface, including the same fitted edges
// the vertex shader uses. XZ may lie anywhere in this chunk, including its
// boundary. Heights are row-major (quads+1)^2; edges, if supplied, are 4*(Q+1).
float sampleChunkHeight(const ChunkParams &p, const ChunkKey &key,
                        std::span<const float> heights, glm::vec2 xz,
                        std::span<const glm::vec4> edges = {});

// Edge order: z-min, z-max, x-min, x-max; each sample packs (height, normal).
using EdgeSamples = std::vector<glm::vec4>;
EdgeSamples extractEdges(const ChunkParams &p,
                         const std::vector<TerrainVertex> &vertices);
// Fits fine edges to the actual coarser resident surface. Corners use one
// canonical owner, including diagonal neighbours, even during streaming.
// indexStep also accounts for simplified reflection topology (1, 2, or 4).
void stitchEdges(const ChunkParams &p, const std::vector<ChunkKey> &keys,
                 const std::vector<const EdgeSamples *> &edges,
                 uint32_t indexStep, std::vector<glm::vec4> &out);

// ---- Frustum
// ------------------------------------------------------------------
// Gribb/Hartmann planes for Vulkan's [0,1] depth range. Same convention as
// main.cpp's extractFrustumPlanes (works for reverse-Z / Y-flipped matrices).
std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4 &viewProj);
bool boxInFrustum(const std::array<glm::vec4, 6> &planes, const Box &b);

} // namespace vkh::terrain
