#include "terrain.hpp"

#include "../../debug.hpp"
#include "../../pipeline.hpp"
#include "../../scene.hpp" // pulls in beginSingleTimeCommands & friends

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace vkh {

namespace {
// A chunk stays resident this many frames after the camera stops needing it,
// so hovering around an LOD boundary doesn't thrash generation.
constexpr uint64_t kEvictGraceFrames = 120;
// Quadtree selection + draw-list cut are recursive and hash-heavy; redo them
// only when the camera moved this far (squared, metres) or every N frames as
// a heartbeat (which also refreshes `lastNeeded`, so nothing live is evicted).
constexpr float kReselectDist2 = 2.f * 2.f;
constexpr uint64_t kReselectInterval = 15;
} // namespace

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void TerrainSys::createSetLayout() {
  // Set 1: heightmap for the shadow march / cavity AO (fragment only).
  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      vk::DescriptorSetLayoutBinding{0,
                                     vk::DescriptorType::eCombinedImageSampler,
                                     1, vk::ShaderStageFlagBits::eFragment},
      vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eVertex}};

  heightmapSetLayout = buildDescriptorSetLayout(context, bindings);
}

void TerrainSys::createDescriptorSet() {
  vk::DescriptorImageInfo imageInfo =
      heightmap.image->getDescriptorInfo(context.vulkan.defaultSampler);
  for (auto &buffer : seamBuffers) {
    auto set =
        context.vulkan.globalDescriptorAllocator->allocate(heightmapSetLayout);
    auto bufferInfo = buffer->descriptorInfo();
    std::array<vk::WriteDescriptorSet, 2> writes{
        vk::WriteDescriptorSet{set, 0, 0, 1,
                               vk::DescriptorType::eCombinedImageSampler,
                               &imageInfo},
        vk::WriteDescriptorSet{set, 1, 0, 1, vk::DescriptorType::eStorageBuffer,
                               nullptr, &bufferInfo}};
    context.vulkan.device.updateDescriptorSets(writes, nullptr);
    frameSets.push_back(set);
  }
}

void TerrainSys::createPipeline() {
  std::vector<vk::DescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout, heightmapSetLayout};

  vk::PushConstantRange pushRange{};
  pushRange.stageFlags =
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
  pushRange.offset = 0;
  pushRange.size = sizeof(PushConstants);

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushRange;

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo = pipelineLayoutInfo;

  // Slim vertex: position + normal only (24 bytes, vs 64 for the glTF vertex
  // the old path used). Exactly what terrain.vert consumes, so there are no
  // unconsumed-attribute validation warnings.
  std::vector<vk::VertexInputBindingDescription> bindings(1);
  bindings[0].binding = 0;
  bindings[0].stride = sizeof(terrain::TerrainVertex);
  bindings[0].inputRate = vk::VertexInputRate::eVertex;
  std::vector<vk::VertexInputAttributeDescription> attributes;
  attributes.emplace_back(0, 0, vk::Format::eR32G32B32Sfloat,
                          offsetof(terrain::TerrainVertex, pos));
  attributes.emplace_back(1, 0, vk::Format::eR32G32B32Sfloat,
                          offsetof(terrain::TerrainVertex, normal));
  pipelineInfo.bindingDescriptions = bindings;
  pipelineInfo.attributeDescriptions = attributes;

  pipelineInfo.vertpath = "shaders/terrain/terrain.vert.spv";
  pipelineInfo.fragpath = "shaders/terrain/terrain.frag.spv";

  pipelineInfo.multisampleInfo.rasterizationSamples =
      static_cast<vk::SampleCountFlagBits>(context.vulkan.msaaSamples);

  // Same winding convention as before (the chunk index buffer reuses the
  // finite mountain's triangle order).
  pipelineInfo.rasterizationInfo.cullMode = vk::CullModeFlagBits::eFront;

  pipeline =
      std::make_unique<GraphicsPipeline>(context, pipelineInfo, "terrain");
}

void TerrainSys::createBuffers() {
  vertsPerChunk = terrain::verticesPerChunk(params);
  chunkBytes = vertsPerChunk * sizeof(terrain::TerrainVertex);

  // One index buffer shared by every chunk (identical topology).
  std::vector<uint32_t> indices = terrain::buildChunkIndices(params, indexLods);
  const vk::DeviceSize indicesSize = sizeof(uint32_t) * indices.size();

  indexBuffer = std::make_unique<Buffer<uint32_t>>(
      context,
      vk::BufferUsageFlagBits::eIndexBuffer |
          vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal, indices.size());

  vertexPool = std::make_unique<Buffer<terrain::TerrainVertex>>(
      context,
      vk::BufferUsageFlagBits::eVertexBuffer |
          vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      static_cast<size_t>(maxChunks) * vertsPerChunk);

  {
    Buffer<std::byte> indexStaging(
        context, vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        indicesSize);
    indexStaging.map();
    indexStaging.write(indices.data(), indicesSize);

    vk::BufferCopy copyRegion{0, 0, indicesSize};
    auto cmd = beginSingleTimeCommands(context);
    cmd.copyBuffer(indexStaging, *indexBuffer, 1, &copyRegion);
    endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
  }

  // Per-frame-in-flight staging (a frame's copies read from it until that
  // frame retires, so frames can't share one).
  const uint32_t framesInFlight = context.vulkan.maxFramesInFlight;
  for (uint32_t i = 0; i < framesInFlight; ++i) {
    seamBuffers.push_back(std::make_unique<Buffer<glm::vec4>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        maxChunks * 4u * (params.quadsPerChunk + 1u) *
            terrain::kIndexLodCount));
    seamBuffers.back()->map();
    frameSeamRevisions.push_back(0);
    staging.push_back(std::make_unique<Buffer<std::byte>>(
        context, vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        static_cast<size_t>(uploadsPerFrame) * chunkBytes));
    staging.back()->map();
  }

  freeSlots.reserve(maxChunks);
  for (uint32_t s = maxChunks; s > 0; --s)
    freeSlots.push_back(s - 1);
}

TerrainSys::TerrainSys(EngineContext &context, const CreateInfo &info)
    : System(context), heightFn(info.heightFn), heightmap(info.heightmap) {
  if (!heightFn)
    throw std::runtime_error("TerrainSys: CreateInfo::heightFn is empty");
  if (!heightmap.image)
    throw std::runtime_error("TerrainSys: CreateInfo::heightmap.image is null");

  params.lod0CellSize = info.lod0CellSize;
  params.quadsPerChunk = info.quadsPerChunk;
  params.lodCount = std::max(1u, info.lodCount);
  params.splitFactor = info.splitFactor;
  params.viewDistance = std::min(info.viewDistance, context.camera.far);
  params.fineNormals = info.fineNormals;
  params.normalTapCells = info.normalTapCells;
  params.refineMaxLod = info.lakeRefineMaxLod;
  params.refineDistance = info.lakeRefineDistance;
  // Generous Y range for LOD distance (erosion can push slightly past
  // [base, base+range]).
  params.minY =
      info.baseHeight - 0.3f * info.heightRange - info.extraHeightRange;
  params.maxY =
      info.baseHeight + 1.3f * info.heightRange + info.extraHeightRange;

  maxChunks = std::max(1u, info.maxChunks);
  params.leafBudget = std::max(1u, maxChunks * 3u / 4u);
  uploadsPerFrame = std::max(1u, info.uploadsPerFrame);

  pushData.heightParams =
      glm::vec4(info.baseHeight, info.heightRange,
                std::cos(glm::radians(info.rockSlopeAngleDeg)),
                static_cast<float>(info.shadowSteps));
  pushData.heightmapRect = glm::vec4(heightmap.worldMin, heightmap.worldSize);

  createSetLayout();
  createPipeline();
  createBuffers();
  createDescriptorSet();

  uint32_t threads = info.workerThreads;
  if (threads == 0) {
    uint32_t hw = std::thread::hardware_concurrency();
    threads = hw > 2 ? std::min(hw - 1, 8u) : 1u;
  }
  for (uint32_t i = 0; i < threads; ++i)
    workers.emplace_back([this](std::stop_token st) { workerLoop(st); });
}

TerrainSys::~TerrainSys() {
  for (auto &w : workers)
    w.request_stop();
  workCv.notify_all();
  workers.clear(); // joins
  if (context.vulkan.device) {
    context.vulkan.device.destroyDescriptorSetLayout(heightmapSetLayout,
                                                     nullptr);
  }
}

// ---------------------------------------------------------------------------
// Worker threads
// ---------------------------------------------------------------------------

void TerrainSys::workerLoop(std::stop_token stop) {
  while (true) {
    terrain::ChunkKey key;
    {
      std::unique_lock lock(mtx);
      workCv.wait(lock, stop, [&] { return !requests.empty(); });
      if (stop.stop_requested())
        return;

      // Nearest chunk to the camera first.
      size_t best = 0;
      float bestDist = std::numeric_limits<float>::max();
      for (size_t i = 0; i < requests.size(); ++i) {
        glm::vec2 mn = terrain::chunkMin(params, requests[i]);
        float half = 0.5f * terrain::chunkSize(params, requests[i].lod);
        float dx = mn.x + half - workerCam.x;
        float dz = mn.y + half - workerCam.z;
        float d = dx * dx + dz * dz;
        if (d < bestDist) {
          bestDist = d;
          best = i;
        }
      }
      key = requests[best];
      requests[best] = requests.back();
      requests.pop_back();
      ++inFlight;
    }

    Generated g;
    g.key = key;
    {
      std::lock_guard lock(mtx);
      if (!spareVerts.empty()) {
        g.verts = std::move(spareVerts.back());
        spareVerts.pop_back();
      }
    }

    // Chunks over a lake sample the ground with LOD 0's erosion detail
    // whatever their own spacing, so it is the surface the lake mesh was fitted
    // to (a coarser chunk would drop gully octaves and shift the shoreline).
    // Pinned for the whole chunk, so it has no height step inside it.
    std::shared_ptr<const terrain::RefineZones> zs;
    {
      std::lock_guard lock(zonesMtx);
      zs = zones;
    }
    g.pinned = key.lod > 0 && zs && !zs->empty() &&
               terrain::chunkOverlapsZones(params, key, *zs);
    terrain::HeightFn pinnedFn;
    const terrain::HeightFn *fn = &heightFn;
    if (g.pinned) {
      const float pin = params.lod0CellSize;
      pinnedFn = [this, pin](glm::vec2 p, float cell) {
        return heightFn(p, std::min(cell, pin));
      };
      fn = &pinnedFn;
    }
    try {
      terrain::generateChunk(params, *fn, key, g.verts, g.bounds);
    } catch (...) {
      // Never let a worker die (std::terminate); the main thread drops the
      // request and asks for the chunk again on its next selection.
      g.ok = false;
    }

    {
      std::lock_guard lock(mtx);
      completed.push_back(std::move(g));
      --inFlight;
    }
    doneCv.notify_all();
  }
}

// ---------------------------------------------------------------------------
// Streaming (main thread)
// ---------------------------------------------------------------------------

void TerrainSys::releaseRetiredSlots() {
  while (!retired.empty() && retired.front().freeAt <= frameCounter) {
    freeSlots.push_back(retired.front().slot);
    retired.pop_front();
  }
}

bool TerrainSys::needsPin(const terrain::ChunkKey &key) const {
  return key.lod > 0 && zones && !zones->empty() &&
         terrain::chunkOverlapsZones(params, key, *zones);
}

void TerrainSys::setLakeZones(std::vector<terrain::RefineZone> z) {
  // Drop degenerate/non-finite rectangles instead of trusting the caller.
  z.erase(std::remove_if(z.begin(), z.end(),
                         [](const terrain::RefineZone &r) {
                           return !(std::isfinite(r.min.x) &&
                                    std::isfinite(r.min.y) &&
                                    std::isfinite(r.max.x) &&
                                    std::isfinite(r.max.y)) ||
                                  r.max.x <= r.min.x || r.max.y <= r.min.y;
                         }),
          z.end());
  auto next = std::make_shared<const terrain::RefineZones>(std::move(z));
  {
    std::lock_guard lock(zonesMtx);
    zones = std::move(next);
  }
  // Anything resident that was built before this zone existed is regenerated;
  // the old copy keeps drawing until the new one arrives.
  for (auto &[key, r] : resident)
    if (!r.pinned && !r.stale && needsPin(key))
      r.stale = true;
  forceReselect = true;
}

void TerrainSys::updateDesired(const glm::vec3 &cam) {
  terrain::selectLeaves(params, cam, desiredLeaves, zones.get());
  desiredSet.clear();
  desiredSet.insert(desiredLeaves.begin(), desiredLeaves.end());

  std::vector<terrain::ChunkKey> fresh;
  for (const auto &k : desiredLeaves) {
    if (pending.contains(k))
      continue;
    auto it = resident.find(k);
    if (it == resident.end() || it->second.stale)
      fresh.push_back(k);
  }

  std::vector<terrain::ChunkKey> dropped;
  {
    std::lock_guard lock(mtx);
    workerCam = cam;
    // Cancel queued requests the camera no longer wants.
    std::vector<terrain::ChunkKey> kept;
    kept.reserve(requests.size() + fresh.size());
    for (const auto &k : requests) {
      if (desiredSet.contains(k))
        kept.push_back(k);
      else
        dropped.push_back(k);
    }
    kept.insert(kept.end(), fresh.begin(), fresh.end());
    requests = std::move(kept);
  }
  for (const auto &k : dropped)
    pending.erase(k);
  for (const auto &k : fresh)
    pending.insert(k);
  workCv.notify_all();
}

uint32_t TerrainSys::recordUploads(vk::CommandBuffer cmd, uint32_t stagingIndex,
                                   uint32_t maxCount) {
  std::vector<Generated> batch;
  {
    std::lock_guard lock(mtx);
    for (size_t i = 0; i < completed.size() && batch.size() < maxCount;) {
      Generated &g = completed[i];
      if (!g.ok && !warnedGenFailure) {
        std::fprintf(stderr, "[terrain] chunk generation failed; will retry\n");
        warnedGenFailure = true;
      }
      if (!g.ok || !desiredSet.contains(g.key)) {
        // Failed, or the camera moved on while this was generating - discard.
        pending.erase(g.key);
        completed[i] = std::move(completed.back());
        completed.pop_back();
        continue;
      }
      if (freeSlots.empty())
        break; // pool full: leave the rest queued until slots free up
      batch.push_back(std::move(g));
      completed[i] = std::move(completed.back());
      completed.pop_back();
      // (swap-remove: don't advance i, re-examine the swapped-in element)
      uint32_t slot = freeSlots.back();
      freeSlots.pop_back();
      const terrain::ChunkKey &bk = batch.back().key;
      // Replacing a stale copy: its slot may still be read by frames in
      // flight, so retire it like an eviction instead of reusing it.
      if (auto old = resident.find(bk); old != resident.end())
        retired.push_back(
            {old->second.slot,
             frameCounter + context.vulkan.maxFramesInFlight + 1});
      Resident r;
      r.slot = slot;
      r.bounds = batch.back().bounds;
      r.lastNeeded = frameCounter;
      r.pinned = batch.back().pinned;
      r.edges = terrain::extractEdges(params, batch.back().verts);
      r.heights.reserve(batch.back().verts.size());
      for (const auto &vertex : batch.back().verts)
        r.heights.push_back(vertex.pos.y);
      // Zones may have changed while this was generating.
      r.stale = !r.pinned && needsPin(bk);
      resident[bk] = std::move(r);
      pending.erase(batch.back().key);
    }
    if (!completed.empty() && freeSlots.empty() && !warnedPoolFull) {
      std::fprintf(stderr,
                   "[terrain] chunk slot pool exhausted (%u slots); raise "
                   "CreateInfo::maxChunks\n",
                   maxChunks);
      warnedPoolFull = true;
    }
  }
  if (batch.empty())
    return 0;

  Buffer<std::byte> &stage = *staging[stagingIndex];
  std::vector<vk::BufferCopy> regions;
  regions.reserve(batch.size());
  for (size_t i = 0; i < batch.size(); ++i) {
    const Resident &r = resident.at(batch[i].key);
    vk::DeviceSize srcOffset = static_cast<vk::DeviceSize>(i) * chunkBytes;
    stage.write(batch[i].verts.data(), chunkBytes, srcOffset);
    regions.push_back(vk::BufferCopy{
        srcOffset, static_cast<vk::DeviceSize>(r.slot) * chunkBytes,
        chunkBytes});
  }

  cmd.copyBuffer(stage, *vertexPool, static_cast<uint32_t>(regions.size()),
                 regions.data());
  {
    // Staging holds a copy now; hand the arrays back to the workers.
    std::lock_guard lock(mtx);
    for (auto &g : batch)
      spareVerts.push_back(std::move(g.verts));
  }

  vk::MemoryBarrier barrier{vk::AccessFlagBits::eTransferWrite,
                            vk::AccessFlagBits::eVertexAttributeRead};
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                      vk::PipelineStageFlagBits::eVertexInput,
                      vk::DependencyFlags(), barrier, nullptr, nullptr);
  return static_cast<uint32_t>(batch.size());
}

void TerrainSys::rebuildDrawList(const glm::vec3 &cam) {
  std::vector<terrain::ChunkKey> nextKeys;
  terrain::buildDrawList(
      params, cam,
      [this](const terrain::ChunkKey &k) { return resident.contains(k); },
      nextKeys, zones.get());

  bool unchanged = nextKeys == drawKeys && drawItems.size() == nextKeys.size();
  for (size_t i = 0; unchanged && i < nextKeys.size(); ++i)
    unchanged = drawItems[i].slot == resident.at(nextKeys[i]).slot;
  if (unchanged) {
    for (const auto &k : drawKeys)
      resident.at(k).lastNeeded = frameCounter;
    for (const auto &k : desiredLeaves)
      if (auto it = resident.find(k); it != resident.end())
        it->second.lastNeeded = frameCounter;
    return;
  }
  drawKeys = std::move(nextKeys);
  drawIndices.clear();
  for (size_t i = 0; i < drawKeys.size(); ++i)
    drawIndices.emplace(drawKeys[i], i);

  drawItems.clear();
  drawItems.reserve(drawKeys.size());
  for (const auto &k : drawKeys) {
    Resident &r = resident.at(k);
    r.lastNeeded = frameCounter;
    drawItems.push_back({r.slot, r.bounds});
  }
  std::vector<const terrain::EdgeSamples *> edges;
  for (const auto &key : drawKeys)
    edges.push_back(&resident.at(key).edges);
  seamSamples.clear();
  ++seamRevision;
  std::vector<glm::vec4> stitched;
  for (uint32_t lod = 0; lod < terrain::kIndexLodCount; ++lod) {
    terrain::stitchEdges(params, drawKeys, edges, 1u << lod, stitched);
    seamSamples.insert(seamSamples.end(), stitched.begin(), stitched.end());
  }
  // Include any moved boundary heights in frustum bounds.
  const size_t edgeCount = 4u * (params.quadsPerChunk + 1u);
  for (size_t i = 0; i < drawItems.size(); ++i)
    for (uint32_t lod = 0; lod < terrain::kIndexLodCount; ++lod)
      for (size_t e = 0; e < edgeCount; ++e) {
        const float y =
            seamSamples[(lod * drawItems.size() + i) * edgeCount + e].x;
        drawItems[i].bounds.min.y = std::min(drawItems[i].bounds.min.y, y);
        drawItems[i].bounds.max.y = std::max(drawItems[i].bounds.max.y, y);
      }
  // Chunks we want but don't have yet are protected too (their coarse
  // parents are already in drawKeys while they stream in).
  for (const auto &k : desiredLeaves) {
    auto it = resident.find(k);
    if (it != resident.end())
      it->second.lastNeeded = frameCounter;
  }
}

void TerrainSys::evict(const terrain::ChunkKey &key) {
  auto it = resident.find(key);
  if (it == resident.end())
    return;
  // The slot may still be read by frames in flight - only recycle it after
  // they've all retired.
  retired.push_back(
      {it->second.slot, frameCounter + context.vulkan.maxFramesInFlight + 1});
  resident.erase(it);
}

void TerrainSys::evictStale() {
  std::vector<terrain::ChunkKey> stale;
  std::unordered_set<terrain::ChunkKey, terrain::ChunkKeyHash> drawing(
      drawKeys.begin(), drawKeys.end());
  for (const auto &[key, r] : resident)
    if (!drawing.contains(key) && !desiredSet.contains(key) &&
        (frameCounter - r.lastNeeded > kEvictGraceFrames ||
         freeSlots.size() + retired.size() < uploadsPerFrame * 2u))
      stale.push_back(key);
  std::sort(stale.begin(), stale.end(), [&](const auto &a, const auto &b) {
    return resident.at(a).lastNeeded < resident.at(b).lastNeeded;
  });
  for (const auto &k : stale)
    if (frameCounter - resident.at(k).lastNeeded > kEvictGraceFrames ||
        freeSlots.size() + retired.size() < uploadsPerFrame * 2u)
      evict(k);
}

void TerrainSys::prewarm(const glm::vec3 &cam) {
  camPos = cam;
  lastSelectCam = cam;
  lastSelectFrame = frameCounter;
  // frameCounter stays put: nothing is in flight yet, and any slot we
  // retire here only needs to outlive frames that don't exist.
  updateDesired(cam);
  while (true) {
    {
      std::unique_lock lock(mtx);
      doneCv.wait(lock, [&] {
        return !completed.empty() || (requests.empty() && inFlight == 0);
      });
      if (completed.empty() && requests.empty() && inFlight == 0)
        break;
    }
    // Upload while workers are still building, recycling their CPU arrays
    // instead of retaining the entire world until the final job finishes.
    auto cmd = beginSingleTimeCommands(context);
    uint32_t n = recordUploads(cmd, 0, uploadsPerFrame);
    endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
    if (n == 0)
      break;
  }
  rebuildDrawList(cam);
  stats.residentChunks = static_cast<uint32_t>(resident.size());
  stats.pendingChunks = static_cast<uint32_t>(pending.size());
}

void TerrainSys::update(vk::CommandBuffer cmd) {
  ++frameCounter;
  camPos = glm::vec3(context.camera.inverseViewMatrix[3]);
  releaseRetiredSlots();
  evictStale();

  const glm::vec3 d = camPos - lastSelectCam;
  const bool reselect = glm::dot(d, d) > kReselectDist2 ||
                        frameCounter - lastSelectFrame >= kReselectInterval ||
                        forceReselect;
  if (reselect) {
    forceReselect = false;
    updateDesired(camPos);
    lastSelectCam = camPos;
    lastSelectFrame = frameCounter;
  }

  const uint32_t uploaded =
      recordUploads(cmd, static_cast<uint32_t>(context.frameInfo.frameIndex),
                    uploadsPerFrame);

  // The draw list only changes when the desired cut or the resident set did.
  if (reselect || uploaded > 0) {
    rebuildDrawList(camPos);
    evictStale();
  }
  const auto frame = context.frameInfo.frameIndex;
  if (!seamSamples.empty() && frameSeamRevisions[frame] != seamRevision) {
    seamBuffers[context.frameInfo.frameIndex]->write(
        seamSamples.data(), seamSamples.size() * sizeof(glm::vec4));
    frameSeamRevisions[frame] = seamRevision;
  }

  stats.residentChunks = static_cast<uint32_t>(resident.size());
  stats.pendingChunks = static_cast<uint32_t>(pending.size());
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

float TerrainSys::sampleHeight(glm::vec2 xz, bool allowGeneration) const {
  for (uint32_t lod = 0; lod < params.lodCount; ++lod) {
    const float size = terrain::chunkSize(params, lod);
    const terrain::ChunkKey key{int32_t(std::floor(xz.x / size)),
                                int32_t(std::floor(xz.y / size)), lod};
    const auto drawn = drawIndices.find(key);
    if (drawn == drawIndices.end())
      continue;
    const auto &chunk = resident.at(key);
    const size_t edgeCount = 4u * (params.quadsPerChunk + 1u);
    const auto edges = std::span<const glm::vec4>(seamSamples)
                           .subspan(drawn->second * edgeCount, edgeCount);
    return terrain::sampleChunkHeight(params, key, chunk.heights, xz, edges);
  }
  if (!allowGeneration)
    return std::numeric_limits<float>::quiet_NaN();
  const float cell = params.lod0CellSize;
  const glm::vec2 corner = glm::floor(xz / cell) * cell;
  const glm::vec2 f = (xz - corner) / cell;
  const float a = heightFn(corner, cell);
  const float b = heightFn(corner + glm::vec2(cell, 0), cell);
  const float c = heightFn(corner + glm::vec2(0, cell), cell);
  if (f.x + f.y <= 1)
    return a + f.x * (b - a) + f.y * (c - a);
  const float d = heightFn(corner + glm::vec2(cell, cell), cell);
  return d + (1 - f.x) * (c - d) + (1 - f.y) * (b - d);
}

void TerrainSys::render(vk::DescriptorSet globalSetOverride,
                        const glm::vec4 &clipPlane, const glm::mat4 *viewProj,
                        const TerrainRenderOptions &options) {
  if (drawItems.empty())
    return;

  auto cmd = context.frameInfo.cmd;
  int frameIndex = context.frameInfo.frameIndex;

  debug::beginLabel(context, cmd, "Terrain", {.35f, .75f, .3f, 1.f});

  pipeline->bind(cmd);

  PushConstants pc = pushData;
  pc.clipPlane = clipPlane;
  cmd.pushConstants(pipeline->getLayout(),
                    vk::ShaderStageFlagBits::eVertex |
                        vk::ShaderStageFlagBits::eFragment,
                    0, sizeof(PushConstants), &pc);

  vk::DescriptorSet sets[] = {
      globalSetOverride ? globalSetOverride
                        : context.vulkan.globalDescriptorSets[frameIndex],
      frameSets[frameIndex]};
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                         pipeline->getLayout(), 0, 2, sets, 0, nullptr);

  vk::Buffer vertexBuffers[] = {*vertexPool};
  vk::DeviceSize offsets[] = {0};
  cmd.bindVertexBuffers(0, 1, vertexBuffers, offsets);
  cmd.bindIndexBuffer(*indexBuffer, 0, vk::IndexType::eUint32);

  const glm::mat4 vp =
      viewProj ? *viewProj
               : context.camera.projectionMatrix * context.camera.viewMatrix;
  const auto planes = terrain::extractFrustumPlanes(vp);

  const terrain::IndexLod range =
      indexLods[std::min(options.indexLod, terrain::kIndexLodCount - 1u)];
  const bool limitRange =
      options.maxDistance < std::numeric_limits<float>::max();

  uint32_t drawn = 0;
  for (size_t i = 0; i < drawItems.size(); ++i) {
    const DrawItem &item = drawItems[i];
    if (limitRange &&
        terrain::distanceToBox(item.bounds, camPos) > options.maxDistance)
      continue;
    if (!terrain::boxInFrustum(planes, item.bounds))
      continue;
    pc.meshParams =
        glm::uvec4(params.quadsPerChunk,
                   (std::min(options.indexLod, terrain::kIndexLodCount - 1u) *
                        drawItems.size() +
                    i) *
                       4u * (params.quadsPerChunk + 1u),
                   0, 0);
    cmd.pushConstants(pipeline->getLayout(),
                      vk::ShaderStageFlagBits::eVertex |
                          vk::ShaderStageFlagBits::eFragment,
                      0, sizeof(PushConstants), &pc);
    cmd.drawIndexed(range.count, 1, range.first,
                    static_cast<int32_t>(item.slot * vertsPerChunk), 0);
    ++drawn;
  }
  // stats.drawnChunks reports the main pass (the last render() of a frame).
  stats.drawnChunks = drawn;

  debug::endLabel(context, cmd);
}

} // namespace vkh
