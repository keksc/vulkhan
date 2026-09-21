#include "proceduralGrass.hpp"

#include <vulkan/vulkan.hpp>

#include "../../buffer.hpp"
#include "../../camera.hpp"
#include "../../debug.hpp"
#include "../../descriptors.hpp"
#include "../../pipeline.hpp"

#include <cmath>
#include <print>
#include <unordered_set>

namespace vkh {

namespace {
constexpr uint32_t kMaxVisibleGrassBlades = 250000;
}

void ProceduralGrassSys::createSetLayouts() {
  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eVertex},
      vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eUniformBuffer, 1,
                                     vk::ShaderStageFlagBits::eVertex}};
  instanceSetLayout = buildDescriptorSetLayout(context, bindings);
}

void ProceduralGrassSys::createPipeline() {
  std::vector<vk::DescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout, instanceSetLayout};

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.vertpath = "shaders/grass/grass.vert.spv";
  pipelineInfo.fragpath = "shaders/grass/grass.frag.spv";
  pipelineInfo.rasterizationInfo.cullMode = vk::CullModeFlagBits::eNone;
  // Each blade is VERTS_PER_BLADE (7) vertices meant to be read as an
  // overlapping-triangle strip - (v0,v1,v2),(v1,v2,v3),... - to form the
  // tapered/curved blade shape. Without this, the default topology reads
  // the same 7 vertices as independent triangle-list triangles: only 2
  // complete triangles per instance (v0-v2, v3-v5), the tip vertex (v6)
  // silently dropped, no taper, and winding that doesn't match what the
  // strip layout assumes - producing single flat disconnected triangles
  // with inconsistent front/back-facing per blade. Vulkan resets the
  // strip automatically at each instance boundary for non-indexed
  // drawIndirect, so no primitive-restart setup is needed here.
  pipelineInfo.inputAssemblyInfo.topology =
      vk::PrimitiveTopology::eTriangleStrip;
  pipelineInfo.multisampleInfo.rasterizationSamples =
      static_cast<vk::SampleCountFlagBits>(context.vulkan.msaaSamples);

  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo,
                                                "procedural grass");
}

void ProceduralGrassSys::createGeneratePipeline() {
  // Binding 1 used to be the old raw per-blade InstanceBuffer; it's now the
  // read-only tile-origin list (replaces camera-relative tiling with a
  // fixed set of world-space tiles built once from the configured
  // patches). Bindings 3/4 are kept at the same numbers finalize.comp
  // already hard-codes (visibleCount, indirect draw command), so that
  // shader needs no changes. Binding 5 is the optional terrain heightmap
  // (see HeightmapBinding) - always present in the layout so the
  // descriptor set is valid whether or not a real heightmap was supplied;
  // see fallbackHeightmapImage_.
  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eUniformBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{3, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{4, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{5,
                                     vk::DescriptorType::eCombinedImageSampler,
                                     1, vk::ShaderStageFlagBits::eCompute}};
  generateSetLayout = buildDescriptorSetLayout(context, bindings);

  vk::PipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &generateSetLayout;

  generatePipeline = std::make_unique<ComputePipeline>(
      context, "shaders/grass/grassGenerate.comp.spv", layoutInfo,
      "procedural grass generate+cull");
}

void ProceduralGrassSys::createFinalizePipeline() {
  vk::PipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &generateSetLayout;

  finalizePipeline = std::make_unique<ComputePipeline>(
      context, "shaders/grass/finalize.comp.spv", layoutInfo,
      "procedural grass finalize");
}

void ProceduralGrassSys::buildTileOrigins() {
  std::vector<GrassPatch> effectivePatches =
      patches_.empty() ? std::vector<GrassPatch>{{glm::vec2(0.f, 0.f),
                                                  glm::vec2(70.f, 70.f)}}
                       : patches_;

  // Dedupe tiles shared by overlapping patches so overlapping meadows
  // don't double-generate (and double-count toward maxBladeCount) the
  // same ground.
  std::unordered_set<uint64_t> seen;
  for (const GrassPatch &patch : effectivePatches) {
    glm::vec2 minCorner = patch.center - patch.size * 0.5f;
    glm::vec2 maxCorner = patch.center + patch.size * 0.5f;
    int tileXStart = static_cast<int>(std::floor(minCorner.x / tileSize_));
    int tileXEnd = static_cast<int>(std::ceil(maxCorner.x / tileSize_));
    int tileZStart = static_cast<int>(std::floor(minCorner.y / tileSize_));
    int tileZEnd = static_cast<int>(std::ceil(maxCorner.y / tileSize_));

    for (int tz = tileZStart; tz < tileZEnd; tz++) {
      for (int tx = tileXStart; tx < tileXEnd; tx++) {
        // Pack the two 32-bit tile coords into one 64-bit key for the
        // dedupe set - cheaper than hashing a pair/struct.
        uint64_t key =
            (static_cast<uint64_t>(static_cast<uint32_t>(tx)) << 32) |
            static_cast<uint32_t>(tz);
        if (!seen.insert(key).second)
          continue;
        tileOrigins_.emplace_back(static_cast<float>(tx) * tileSize_,
                                  static_cast<float>(tz) * tileSize_);
      }
    }
  }
}

ProceduralGrassSys::ProceduralGrassSys(
    EngineContext &context, std::vector<GrassPatch> patches, float tileSize,
    uint32_t bladesPerTileSide, std::optional<HeightmapBinding> heightmap)
    : System(context), patches_(std::move(patches)), tileSize_(tileSize),
      bladesPerTileSide_(bladesPerTileSide), heightmap_(std::move(heightmap)) {
  buildTileOrigins();

  createSetLayouts();
  createPipeline();
  createGeneratePipeline();
  createFinalizePipeline();

  // Always need a valid image bound at binding 5, whether or not a real
  // heightmap was supplied - see the member comment on
  // fallbackHeightmapImage_. Values are irrelevant since
  // grassGenerate.comp only samples the heightmap when GrassUbo's
  // heightmapSize is nonzero, which stays (0,0) whenever heightmap_ is
  // empty.
  {
    ImageCreateInfo_data dummyInfo{};
    dummyInfo.size = glm::uvec2(1u, 1u);
    static const float dummyPixels[2] = {0.f, 0.f};
    dummyInfo.data = const_cast<void *>(static_cast<const void *>(dummyPixels));
    dummyInfo.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    dummyInfo.usage =
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    dummyInfo.format = vk::Format::eR32G32Sfloat;
    dummyInfo.name = "procedural grass fallback heightmap image";
    fallbackHeightmapImage_ = std::make_shared<Image2D>(context, dummyInfo);
  }

  uint32_t framesInFlight = context.vulkan.maxFramesInFlight;
  const uint32_t candidateBladeCount = maxBladeCount();

  // The compacted output is a fixed-size GPU buffer.  It must NOT be sized
  // to the total candidate population: large grass patches can contain
  // millions of candidates, while only a bounded number can be rendered
  // usefully in one frame.
  const uint32_t maxVisibleBlades = kMaxVisibleGrassBlades;

  std::println(
      "Grass: tiles={}, blades/tile={}, candidates={}, maxVisible={}, "
      "BladeInstance={} bytes, output={} MiB",
      tileOrigins_.size(), bladesPerTileSide_, candidateBladeCount,
      maxVisibleBlades, sizeof(BladeInstance),
      (static_cast<uint64_t>(maxVisibleBlades) * sizeof(BladeInstance)) /
          (1024ull * 1024ull));

  // Uploaded once - the tile layout never changes after construction, so
  // this doesn't need per-frame-in-flight copies like the other buffers.
  tileOriginBuffer = std::make_unique<Buffer<glm::vec2>>(
      context, vk::BufferUsageFlagBits::eStorageBuffer,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent,
      tileOrigins_.size());
  tileOriginBuffer->map();
  tileOriginBuffer->write(tileOrigins_.data(),
                          tileOrigins_.size() * sizeof(glm::vec2));
  tileOriginBuffer->unmap();

  compactedInstanceBuffers.resize(framesInFlight);
  visibleCountBuffers.resize(framesInFlight);
  indirectDrawBuffers.resize(framesInFlight);
  grassUboBuffers.resize(framesInFlight);
  windBuffers.resize(framesInFlight);
  instanceDescriptorSets.resize(framesInFlight, nullptr);
  generateDescriptorSets.resize(framesInFlight, nullptr);

  for (uint32_t i = 0; i < framesInFlight; i++) {
    grassUboBuffers[i] = std::make_unique<Buffer<GrassUbo>>(
        context, vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        1);
    windBuffers[i] = std::make_unique<Buffer<WindUbo>>(
        context, vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        1);

    // Fixed-size for the lifetime of the system - worst case is every
    // generated blade in the tile window passing the frustum test.
    compactedInstanceBuffers[i] = std::make_unique<Buffer<BladeInstance>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal, maxVisibleBlades);

    visibleCountBuffers[i] = std::make_unique<Buffer<uint32_t>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        1);

    indirectDrawBuffers[i] = std::make_unique<Buffer<vk::DrawIndirectCommand>>(
        context,
        vk::BufferUsageFlagBits::eIndirectBuffer |
            vk::BufferUsageFlagBits::eTransferDst |
            vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        1);
    vk::DrawIndirectCommand cmd{};
    cmd.vertexCount = VERTS_PER_BLADE;
    cmd.instanceCount = 0;
    cmd.firstVertex = 0;
    cmd.firstInstance = 0;
    indirectDrawBuffers[i]->map();
    indirectDrawBuffers[i]->write(&cmd, sizeof(cmd));
    indirectDrawBuffers[i]->unmap();

    instanceDescriptorSets[i] =
        context.vulkan.globalDescriptorAllocator->allocate(instanceSetLayout);
    DescriptorWriter writer(context);
    vk::DescriptorBufferInfo bInfo =
        compactedInstanceBuffers[i]->descriptorInfo();
    writer.writeBuffer(0, bInfo, vk::DescriptorType::eStorageBuffer);
    vk::DescriptorBufferInfo wInfo = windBuffers[i]->descriptorInfo();
    writer.writeBuffer(1, wInfo, vk::DescriptorType::eUniformBuffer);
    writer.updateSet(instanceDescriptorSets[i]);

    generateDescriptorSets[i] =
        context.vulkan.globalDescriptorAllocator->allocate(generateSetLayout);
    DescriptorWriter gWriter(context);
    vk::DescriptorBufferInfo uInfo = grassUboBuffers[i]->descriptorInfo();
    vk::DescriptorBufferInfo tileOriginInfo =
        tileOriginBuffer->descriptorInfo();
    vk::DescriptorBufferInfo compactedInfo =
        compactedInstanceBuffers[i]->descriptorInfo();
    vk::DescriptorBufferInfo countInfo =
        visibleCountBuffers[i]->descriptorInfo();
    vk::DescriptorBufferInfo indirectInfo =
        indirectDrawBuffers[i]->descriptorInfo();
    gWriter.writeBuffer(0, uInfo, vk::DescriptorType::eUniformBuffer);
    gWriter.writeBuffer(1, tileOriginInfo, vk::DescriptorType::eStorageBuffer);
    gWriter.writeBuffer(2, compactedInfo, vk::DescriptorType::eStorageBuffer);
    gWriter.writeBuffer(3, countInfo, vk::DescriptorType::eStorageBuffer);
    gWriter.writeBuffer(4, indirectInfo, vk::DescriptorType::eStorageBuffer);

    Image2D &heightmapImage =
        heightmap_ ? *heightmap_->image : *fallbackHeightmapImage_;
    vk::DescriptorImageInfo heightmapInfo =
        heightmapImage.getDescriptorInfo(context.vulkan.defaultSampler);
    gWriter.writeImage(5, heightmapInfo,
                       vk::DescriptorType::eCombinedImageSampler);

    gWriter.updateSet(generateDescriptorSets[i]);
  }
}

ProceduralGrassSys::~ProceduralGrassSys() {
  if (context.vulkan.device) {
    context.vulkan.device.destroyDescriptorSetLayout(instanceSetLayout,
                                                     nullptr);
    context.vulkan.device.destroyDescriptorSetLayout(generateSetLayout,
                                                     nullptr);
  }
}

void ProceduralGrassSys::updateBuffers() {
  int frameIndex = context.frameInfo.frameIndex;

  auto planes = camera::getFrustumPlanes(context.camera.projectionMatrix *
                                         context.camera.viewMatrix);
  GrassUbo ubo{};
  for (int i = 0; i < 6; i++)
    ubo.frustumPlanes[i] = planes[i];
  ubo.camPos = glm::vec2(context.camera.position.x, context.camera.position.z);
  ubo.heightmapOrigin = heightmap_ ? heightmap_->worldMin : glm::vec2(0.f);
  // (0,0) is the sentinel grassGenerate.comp checks to fall back to flat
  // ground at y=0, so this must stay exactly zero when no heightmap was
  // supplied - even a legitimately tiny real heightmap should never hit
  // (0,0) in practice, but if yours somehow does, nudge worldSize instead
  // of relying on this sentinel.
  ubo.heightmapSize = heightmap_ ? heightmap_->worldSize : glm::vec2(0.f);
  ubo.tileSize = tileSize_;
  ubo.bladesPerTileSide = bladesPerTileSide_;
  ubo.tileCount = static_cast<uint32_t>(tileOrigins_.size());
  ubo.maxDist = maxDist;
  ubo.lodNearDist = lodNearDist;
  ubo.flowerChance = flowerChance;

  grassUboBuffers[frameIndex]->map();
  grassUboBuffers[frameIndex]->write(&ubo, sizeof(GrassUbo));
  grassUboBuffers[frameIndex]->unmap();

  WindUbo wind{};
  wind.time = context.time;
  wind.strength = windStrength;
  wind.dir = windDir;
  wind.lodNearDist = lodNearDist;
  windBuffers[frameIndex]->map();
  windBuffers[frameIndex]->write(&wind, sizeof(WindUbo));
  windBuffers[frameIndex]->unmap();

  // Reset the atomic counter every frame - this frame's generate+cull pass
  // accumulates it from scratch.
  uint32_t zero = 0;
  visibleCountBuffers[frameIndex]->map();
  visibleCountBuffers[frameIndex]->write(&zero, sizeof(uint32_t));
  visibleCountBuffers[frameIndex]->unmap();
}

void ProceduralGrassSys::cull(vk::CommandBuffer cmd) {
  int frameIndex = context.frameInfo.frameIndex;

  debug::beginLabel(context, cmd, "Procedural Grass Generate+Cull",
                    {.3f, .9f, .5f, 1.f});

  vk::BufferMemoryBarrier indirectBarrier{};
  indirectBarrier.srcAccessMask =
      vk::AccessFlagBits::eIndirectCommandRead | vk::AccessFlagBits::eHostWrite;
  indirectBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
  indirectBarrier.buffer = *indirectDrawBuffers[frameIndex];
  indirectBarrier.size = indirectDrawBuffers[frameIndex]->getSize();

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eDrawIndirect |
                          vk::PipelineStageFlagBits::eHost,
                      vk::PipelineStageFlagBits::eComputeShader,
                      vk::DependencyFlags(), nullptr, indirectBarrier, nullptr);

  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                         generatePipeline->getLayout(), 0, 1,
                         &generateDescriptorSets[frameIndex], 0, nullptr);

  generatePipeline->bind(cmd);
  const uint32_t candidateBladeCount = maxBladeCount();
  const uint32_t groupCount = (candidateBladeCount + 63u) / 64u;
  cmd.dispatch(groupCount, 1, 1);

  // Makes the generate+cull pass's writes (visibleCountBuffers,
  // compactedInstanceBuffers) visible to both the finalize dispatch and
  // the vertex shader in render(), which reads compactedInstanceBuffers
  // directly. A barrier that only covers compute->compute here would
  // leave the vertex shader racing the write with no visibility
  // guarantee - the flicker bug from before.
  vk::MemoryBarrier computeBarrier{vk::AccessFlagBits::eShaderWrite,
                                   vk::AccessFlagBits::eShaderRead};
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eComputeShader |
                          vk::PipelineStageFlagBits::eVertexShader,
                      vk::DependencyFlags(), computeBarrier, nullptr, nullptr);

  finalizePipeline->bind(cmd);

  cmd.dispatch(1, 1, 1);

  indirectBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
  indirectBarrier.dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eDrawIndirect,
                      vk::DependencyFlags(), nullptr, indirectBarrier, nullptr);

  debug::endLabel(context, cmd);
}

void ProceduralGrassSys::render() {
  auto cmd = context.frameInfo.cmd;
  int frameIndex = context.frameInfo.frameIndex;

  debug::beginLabel(context, cmd, "Procedural Grass", {.4f, .9f, .3f, 1.f});

  pipeline->bind(cmd);

  cmd.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, pipeline->getLayout(), 0, 1,
      &context.vulkan.globalDescriptorSets[frameIndex], 0, nullptr);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                         pipeline->getLayout(), 1, 1,
                         &instanceDescriptorSets[frameIndex], 0, nullptr);

  cmd.drawIndirect(*indirectDrawBuffers[frameIndex], 0, 1,
                   sizeof(vk::DrawIndirectCommand));

  debug::endLabel(context, cmd);
}

} // namespace vkh
