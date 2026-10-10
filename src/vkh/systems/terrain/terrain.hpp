#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include <array>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../buffer.hpp"
#include "../../image.hpp"
#include "../../pipeline.hpp"
#include "../system.hpp"
#include "terrainChunks.hpp"

namespace vkh {

class GraphicsPipeline;

// Per-pass knobs for TerrainSys::render(). The main pass uses the defaults;
// planar reflections are drawn into a small, ripple-distorted target and
// don't need full mesh density or the whole view distance.
struct TerrainRenderOptions {
  // 0 = every vertex, 1 = every 2nd (4x fewer triangles), 2 = every 4th
  // (16x fewer). See terrain::kIndexLodCount.
  uint32_t indexLod{0};
  // Chunks whose box is farther than this from the main camera are skipped.
  float maxDistance{std::numeric_limits<float>::max()};
};

// Streaming, LOD'd, effectively-infinite terrain.
//
// The world is a quadtree of fixed-size meshes ("chunks", see
// terrainChunks.hpp): fine chunks near the camera, each ring twice as coarse
// as the last. Chunk meshes are generated on worker threads straight from the
// height function, uploaded into one big slot pool (one vertex buffer, one
// index buffer shared by every chunk) and drawn with one drawIndexed each,
// frustum-culled on the CPU. Chunks the camera leaves behind are recycled.
//
// Per frame, call (outside any render pass, before the reflection/main
// passes):  terrainSys.update(cmd);
// then in every pass that should show terrain:  terrainSys.render(...);
class TerrainSys : public System {
public:
  // Same shape as ProceduralGrassSys::HeightmapBinding. Only used for the
  // sun-shadow march and cavity AO in terrain.frag, so it only needs to cover
  // the area near the origin (the finite mountain's heightmap works). The
  // shader fades shadows/AO out near the rect's edge and skips them outside.
  struct HeightmapBinding {
    std::shared_ptr<Image2D> image;
    glm::vec2 worldMin{};
    glm::vec2 worldSize{};
  };

  struct CreateInfo {
    // World-space terrain height. Called from worker threads. See
    // terrain::HeightFn.
    terrain::HeightFn heightFn;

    // For the shader: world Y of the lowest/highest terrain (normalized
    // altitude = (y - baseHeight) / heightRange) and the slope that counts as
    // bare rock (MountainCreateInfo::rockSlopeAngleDeg).
    float baseHeight{0.f};
    float heightRange{1.f};
    float extraHeightRange{0.f}; // additive terrain features (volcanoes, etc.)
    float rockSlopeAngleDeg{45.f};

    // Vertex spacing of the finest chunks (world units).
    float lod0CellSize{1.5f};
    uint32_t quadsPerChunk{64};
    uint32_t lodCount{6};
    float splitFactor{1.5f};
    // Furthest terrain generated/drawn. Clamped to the camera's far plane.
    float viewDistance{5000.f};
    bool fineNormals{true};
    // See terrain::ChunkParams::normalTapCells.
    float normalTapCells{4.f};

    // Slot pool size (each slot holds one chunk). ~210 chunks are live at
    // the default settings; the rest is headroom for LOD transitions.
    uint32_t maxChunks{512};
    // Max chunks uploaded per frame (bounds the per-frame copy cost).
    uint32_t uploadsPerFrame{8};
    // 0 = hardware_concurrency - 1 (at least 1).
    uint32_t workerThreads{0};

    // Heightmap-traced sun shadow steps. 0 disables terrain self-shadowing.
    uint32_t shadowSteps{24};

    // Lake fidelity (see setLakeZones()). Chunks overlapping a lake zone are
    // refined to at most this LOD while within lakeRefineDistance of the
    // camera. 0 = full detail, which is what makes the ground match the shore
    // the water was fitted to exactly. Costs a few extra chunks per lake, so
    // keep maxChunks roomy if many lakes are close together.
    uint32_t lakeRefineMaxLod{0};
    float lakeRefineDistance{1200.f};

    HeightmapBinding heightmap;
  };

  struct Stats {
    uint32_t residentChunks{};
    uint32_t drawnChunks{};   // after frustum culling, last render() call
    uint32_t pendingChunks{}; // requested but not yet uploaded
  };

  TerrainSys(EngineContext &context, const CreateInfo &info);
  ~TerrainSys();

  // Blocks until every chunk around `camPos` is generated and uploaded, so
  // the first frame isn't empty. Call once after construction with the
  // camera's spawn position.
  void prewarm(const glm::vec3 &camPos);

  // Streams chunks for the current camera: requests new ones, uploads
  // finished ones (copy commands recorded into `cmd`), recycles stale ones
  // and rebuilds the draw list. Must be called outside a render pass.
  void update(vk::CommandBuffer cmd);

  // `viewProj` (optional) frustum-culls against that matrix instead of the
  // main camera's - pass PlanarReflectionSys::getReflViewProj() with its
  // global set and clip plane when rendering a reflection pass.
  void render(vk::DescriptorSet globalSetOverride = nullptr,
              const glm::vec4 &clipPlane = glm::vec4(0.f, 1.f, 0.f, 1e9f),
              const glm::mat4 *viewProj = nullptr,
              const TerrainRenderOptions &options = {});

  // Tells the terrain where lakes are (padded world-space XZ boxes). Chunks
  // overlapping a zone use the same erosion detail as the lake builder at
  // every LOD and are kept fine near the camera, so the ground under a lake's
  // shoreline is the surface its water mesh was fitted to. Replaces the
  // previous set. Chunks already resident that were built without the lake
  // known are regenerated in the background (the old copy stays on screen
  // until the new one is uploaded, so nothing flashes or leaves a hole).
  // Main thread only.
  void setLakeZones(std::vector<terrain::RefineZone> zones);

  void setShadowSteps(uint32_t steps) {
    pushData.heightParams.w = static_cast<float>(steps);
  }

  Stats getStats() const { return stats; }
  // Main thread: height on the currently drawn mesh, including LOD seams.
  // Optional generation is expensive: real-time AI passes false and gets
  // NaN outside the streamed mesh, then waits for chunks to arrive.
  float sampleHeight(glm::vec2 xz, bool allowGeneration = true) const;

private:
  // Must match the PushConstants block in terrain.vert / terrain.frag
  // exactly (4 x vec4 = 64 bytes).
  struct PushConstants {
    glm::vec4 clipPlane{0.f, 1.f, 0.f, 1e9f};
    // x: baseHeight, y: heightRange, z: cos(rockSlopeAngle), w: shadow steps
    glm::vec4 heightParams{};
    // xy: heightmap worldMin, zw: heightmap worldSize
    glm::vec4 heightmapRect{};
    // x: quads per chunk, y: edge sample offset in this frame's buffer.
    glm::uvec4 meshParams{};
  };

  struct Generated {
    terrain::ChunkKey key;
    std::vector<terrain::TerrainVertex> verts;
    terrain::Box bounds;
    bool pinned{false}; // generated with the lake zone octave pin
    bool ok{true};      // false: generation threw, nothing to upload
  };

  struct Resident {
    uint32_t slot{};
    terrain::Box bounds;
    uint64_t lastNeeded{};
    bool pinned{false}; // see Generated::pinned
    bool stale{false};  // must be regenerated (a lake zone appeared over it)
    terrain::EdgeSamples edges;
    std::vector<float> heights;
  };

  struct DrawItem {
    uint32_t slot{};
    terrain::Box bounds;
  };

  struct Retired {
    uint32_t slot{};
    uint64_t freeAt{};
  };

  void createSetLayout();
  void createDescriptorSet();
  void createPipeline();
  void createBuffers();

  // Streaming steps (main thread).
  void releaseRetiredSlots();
  void updateDesired(const glm::vec3 &cam);
  uint32_t recordUploads(vk::CommandBuffer cmd, uint32_t stagingIndex,
                         uint32_t maxCount);
  void rebuildDrawList(const glm::vec3 &cam);
  void evictStale();
  void evict(const terrain::ChunkKey &key);

  void workerLoop(std::stop_token stop);

  // Would a chunk with this key be generated with the octave pin right now?
  // Main thread (reads `zones` without the lock: it is the only writer).
  bool needsPin(const terrain::ChunkKey &key) const;

  terrain::ChunkParams params;
  terrain::HeightFn heightFn;
  HeightmapBinding heightmap;
  PushConstants pushData{};
  uint32_t maxChunks{};
  uint32_t uploadsPerFrame{};
  std::array<terrain::IndexLod, terrain::kIndexLodCount> indexLods{};
  size_t vertsPerChunk{};
  size_t chunkBytes{};

  vk::DescriptorSetLayout heightmapSetLayout = nullptr;
  std::vector<vk::DescriptorSet> frameSets;
  std::vector<std::unique_ptr<Buffer<glm::vec4>>> seamBuffers;
  std::vector<glm::vec4> seamSamples;
  uint64_t seamRevision{0};
  std::vector<uint64_t> frameSeamRevisions;
  std::unique_ptr<GraphicsPipeline> pipeline;

  std::unique_ptr<Buffer<terrain::TerrainVertex>> vertexPool;
  std::unique_ptr<Buffer<uint32_t>> indexBuffer;
  // One persistently-mapped staging buffer per frame in flight.
  std::vector<std::unique_ptr<Buffer<std::byte>>> staging;

  std::unordered_map<terrain::ChunkKey, Resident, terrain::ChunkKeyHash>
      resident;
  std::vector<uint32_t> freeSlots;
  std::deque<Retired> retired;
  uint64_t frameCounter{0};
  glm::vec3 camPos{0.f};
  // Selection/draw-list rebuilds are skipped while the camera is (nearly)
  // still and nothing new arrived; see update().
  glm::vec3 lastSelectCam{0.f};
  uint64_t lastSelectFrame{0};
  // Recycled vertex arrays (guarded by `mtx`): chunk generation would
  // otherwise allocate and free ~100 KB per chunk.
  std::vector<std::vector<terrain::TerrainVertex>> spareVerts;

  std::unordered_set<terrain::ChunkKey, terrain::ChunkKeyHash> pending;
  std::unordered_set<terrain::ChunkKey, terrain::ChunkKeyHash> desiredSet;
  std::vector<terrain::ChunkKey> desiredLeaves;
  std::vector<terrain::ChunkKey> drawKeys;
  std::unordered_map<terrain::ChunkKey, size_t, terrain::ChunkKeyHash>
      drawIndices;
  std::vector<DrawItem> drawItems;
  Stats stats{};
  bool warnedPoolFull{false};
  bool warnedGenFailure{false};

  // Lake zones. Written by the main thread under zonesMtx; workers take a
  // snapshot under the same lock once per chunk.
  std::mutex zonesMtx;
  std::shared_ptr<const terrain::RefineZones> zones;
  bool forceReselect{false};

  // ---- Worker thread shared state (guarded by `mtx`) ----
  std::mutex mtx;
  std::condition_variable_any workCv;
  std::condition_variable doneCv;
  std::vector<terrain::ChunkKey> requests;
  std::vector<Generated> completed;
  uint32_t inFlight{0};
  glm::vec3 workerCam{0.f};
  // Declared last so it is destroyed (joined) first.
  std::vector<std::jthread> workers;
};

} // namespace vkh
