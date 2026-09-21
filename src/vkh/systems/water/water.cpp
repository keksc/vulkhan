#include "water.hpp"

#include "../../buffer.hpp"
#include "../../debug.hpp"
#include "../../descriptors.hpp"
#include "../../deviceHelpers.hpp"
#include "../../engineContext.hpp"
#include "../../pipeline.hpp"
#include "../../sceneBuilder.hpp"
#include "../../swapChain.hpp"
#include "../planarReflectionSys.hpp"

namespace vkh {

WaterSys::WaterSys(EngineContext &context, SkySys &skySys, WSTessendorf &tess,
                   const WaterCreateInfo &info)
    : System{context}, skySys{skySys}, modelTess{tess}, createInfo{info} {
  vertexUBO.scale = createInfo.waveScale;
  vertexUBO.uvRepeat = createInfo.uvRepeat;
  vertexUBO.chopScale = createInfo.chopScale;

  createDescriptorSetLayout();
  createSampler();
  createPipeline();
  createMesh();
  createRenderData();
}

WaterSys::~WaterSys() {
  if (context.vulkan.device) {
    context.vulkan.device.destroyDescriptorSetLayout(setLayout, nullptr);
    context.vulkan.device.destroySampler(repeatSampler, nullptr);
  }
}

void WaterSys::createSampler() {
  // The shared displacement/foam image is one WSTessendorf tile. A small
  // lake patch samples it multiple times across its surface (see
  // WaterCreateInfo::uvRepeat), which needs address mode eRepeat rather
  // than context.vulkan.defaultSampler's presumed clamp-to-edge - clamping
  // would smear the tile's edge texels across the rest of the lake
  // instead of tiling the wave pattern.
  vk::SamplerCreateInfo samplerInfo{};
  samplerInfo.magFilter = vk::Filter::eLinear;
  samplerInfo.minFilter = vk::Filter::eLinear;
  samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
  samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
  samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
  samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
  samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
  // Now that displacementFoamMap actually has a mip chain (see
  // WSTessendorf::recordGenerateMips()), anisotropic filtering helps with
  // the grazing-angle minification a flat water plane sees a lot of -
  // trilinear alone still isotropically blurs across a squarish footprint,
  // which is a poor match for the very elongated (steep-angle) texture
  // footprint most on-screen water pixels actually have.
  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.maxAnisotropy =
      context.vulkan.physicalDeviceProperties.limits.maxSamplerAnisotropy;

  repeatSampler = context.vulkan.device.createSampler(samplerInfo);
  debug::setObjName(
      context, vk::ObjectType::eSampler,
      reinterpret_cast<uint64_t>(static_cast<VkSampler>(repeatSampler)),
      "water repeat sampler");
}

void WaterSys::createMesh() {
  std::vector<Vertex> vertices = createGridVertices();
  std::vector<uint32_t> indices = createGridIndices();

  SceneCreateInfo<Vertex> meshInfo{.vertices = vertices, .indices = indices};
  scene = std::make_unique<Scene<Vertex>>(context, meshInfo);
}

void WaterSys::createPipeline() {
  PipelineCreateInfo pipelineInfo{};
  std::array<vk::DescriptorSetLayout, 2> setLayouts = {
      context.vulkan.globalDescriptorSetLayout, setLayout};

  pipelineInfo.layoutInfo.setLayoutCount =
      static_cast<uint32_t>(setLayouts.size());
  pipelineInfo.layoutInfo.pSetLayouts = setLayouts.data();
  pipelineInfo.inputAssemblyInfo.topology = vk::PrimitiveTopology::ePatchList;
  pipelineInfo.attributeDescriptions = Vertex::attribDescriptions;
  pipelineInfo.bindingDescriptions = Vertex::bindingDescriptions;
  pipelineInfo.rasterizationInfo.polygonMode = vk::PolygonMode::eFill;
  pipelineInfo.vertpath = "shaders/water/water.vert.spv";
  pipelineInfo.fragpath = "shaders/water/water.frag.spv";
  pipelineInfo.tescpath = "shaders/water/water.tesc.spv";
  pipelineInfo.tesepath = "shaders/water/water.tese.spv";

  pipelineInfo.multisampleInfo.rasterizationSamples =
      static_cast<vk::SampleCountFlagBits>(context.vulkan.msaaSamples);

  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo, "water");
}

void WaterSys::createUniformBuffers(const uint32_t bufferCount) {
  const vk::DeviceSize bufferSize =
      getUniformBufferAlignment(context, sizeof(VertexUBO));

  uniformBuffers.reserve(bufferCount);
  for (size_t i = 0; i < bufferCount; i++) {
    auto &buffer = uniformBuffers.emplace_back(
        context, vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible, bufferSize);
    buffer.map();
  }
}

void WaterSys::createDescriptorSets(const uint32_t count) {
  sets.reserve(count);
  for (size_t i = 0; i < count; i++) {
    auto set = context.vulkan.globalDescriptorAllocator->allocate(setLayout);
    std::string str = std::format("set #{} for WaterSys", i);
    debug::setObjName(
        context, vk::ObjectType::eDescriptorSet,
        reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(set)),
        str.c_str());
    sets.push_back(set);
  }
}

void WaterSys::createRenderData() {
  uint32_t imageCount = context.vulkan.swapChain->imageCount();
  createUniformBuffers(imageCount);
  createDescriptorSets(imageCount);
}

// A fixed unit quad in XZ (-0.5..0.5), so every WaterSys (lake) instance
// can share this same generation logic; per-patch world
// size/position is applied later purely via the model matrix (see
// update()), not by regenerating geometry.
std::vector<WaterSys::Vertex> WaterSys::createGridVertices() {
  std::vector<Vertex> vertices;
  vertices.reserve(totalVertexCount);

  for (uint32_t y = 0; y <= tileResolution; ++y) {
    for (uint32_t x = 0; x <= tileResolution; ++x) {
      glm::vec2 uv{static_cast<float>(x) / tileResolution,
                  static_cast<float>(y) / tileResolution};
      vertices.emplace_back(glm::vec3(uv.x - 0.5f, 0.0f, uv.y - 0.5f), uv);
    }
  }

  return vertices;
}

std::vector<uint32_t> WaterSys::createGridIndices() {
  const uint32_t vertexCount = tileResolution + 1;
  std::vector<uint32_t> indices;
  indices.reserve(tileResolution * tileResolution * 4);

  // See WaterCreateInfo::cellMask's comment. Empty mask = old behavior
  // (fill the whole rectangle) - the common case for anything without an
  // actual basin shape to provide.
  const bool useMask = !createInfo.cellMask.empty();
  // A malformed mask (wrong resolution, or non-empty but too short for
  // its claimed resolution) renders NOTHING rather than reading out of
  // bounds or silently misaligning cells with the wrong meaning - see
  // WaterCreateInfo::cellMask's comment on failing safe/visibly. An empty
  // lake is an obvious, debuggable problem; an out-of-bounds read or a
  // subtly shifted mask is not.
  const bool maskValid =
      !useMask ||
      (createInfo.maskResolution.x == tileResolution &&
       createInfo.maskResolution.y == tileResolution &&
       createInfo.cellMask.size() ==
           static_cast<size_t>(tileResolution) * tileResolution);
  if (useMask && !maskValid)
    return indices; // empty - see comment above

  for (uint32_t y = 0; y < tileResolution; ++y) {
    for (uint32_t x = 0; x < tileResolution; ++x) {
      if (useMask && !createInfo.cellMask[y * tileResolution + x])
        continue; // outside this lake's actual footprint - see cellMask

      uint32_t v0 = y * vertexCount + x;
      uint32_t v1 = y * vertexCount + (x + 1);
      uint32_t v2 = (y + 1) * vertexCount + (x + 1);
      uint32_t v3 = (y + 1) * vertexCount + x;
      indices.emplace_back(v0);
      indices.emplace_back(v1);
      indices.emplace_back(v2);
      indices.emplace_back(v3);
    }
  }
  return indices;
}

void WaterSys::updateDescriptorSet(vk::DescriptorSet set) {
  // UBOs
  VkDescriptorBufferInfo bufferInfos[1]{};
  bufferInfos[0].buffer =
      static_cast<vk::Buffer>(uniformBuffers[context.frameInfo.frameIndex]);
  bufferInfos[0].offset = 0;
  bufferInfos[0].range = sizeof(VertexUBO);

  // Binding 1: Displacement Map - sampled through repeatSampler (see
  // createSampler()) rather than context.vulkan.defaultSampler, so small
  // lake patches can tile the wave texture via WaterCreateInfo::uvRepeat
  // instead of stretching or clamping it across their whole surface.
  VkDescriptorImageInfo imageInfo{};
  imageInfo = modelTess.getDisplacementFoamImage().getDescriptorInfo(repeatSampler);
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  // Binding 2: Planar reflection color target. Must ALWAYS be a valid,
  // currently-readable image, even when this instance's vertexUBO.
  // reflectionMode is 0 (SSR) and water.frag never actually samples it
  // for this draw - the descriptor set layout declares the binding
  // statically for every draw using this pipeline, and leaving it
  // pointing at a destroyed/nonexistent image is a validation error (and
  // undefined behavior without validation layers) regardless of whether
  // the shader's dynamic branch happens to skip reading it. When there's
  // no real planar reflection source (planarReflection == nullptr, or
  // Planar mode was never actually requested for this instance - see
  // update()), fall back to re-binding the displacement map: already
  // guaranteed valid/in the right layout every frame, and harmless since
  // it's never sampled through this binding when reflectionMode == 0.
  VkDescriptorImageInfo planarReflInfo =
      planarReflection ? getPlanarReflectionImageInfo() : imageInfo;

  DescriptorWriter writer(context);
  writer.writeBuffer(0, bufferInfos[0], vk::DescriptorType::eUniformBuffer);
  writer.writeImage(1, imageInfo, vk::DescriptorType::eCombinedImageSampler);
  writer.writeImage(2, planarReflInfo, vk::DescriptorType::eCombinedImageSampler);
  writer.updateSet(set);
}

void WaterSys::updateUniformBuffer() {
  auto &buffer = uniformBuffers[context.frameInfo.frameIndex];
  buffer.write(&vertexUBO, sizeof(vertexUBO));
  buffer.flush();
}

void WaterSys::createDescriptorSetLayout() {
  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      // Binding 0: UBO
      vk::DescriptorSetLayoutBinding{
          0, vk::DescriptorType::eUniformBuffer, 1,
          vk::ShaderStageFlagBits::eVertex |
              vk::ShaderStageFlagBits::eTessellationControl |
              vk::ShaderStageFlagBits::eTessellationEvaluation |
              vk::ShaderStageFlagBits::eFragment,
          nullptr},
      // Binding 1: Displacement Map (RGB), Foam (A)
      vk::DescriptorSetLayoutBinding{
          1, vk::DescriptorType::eCombinedImageSampler, 1,
          vk::ShaderStageFlagBits::eTessellationEvaluation |
              vk::ShaderStageFlagBits::eFragment,
          nullptr},
      // Binding 2: Planar reflection color target - see
      // getPlanarReflectionImageInfo()'s comment in updateDescriptorSet()
      // for why this is always bound to SOMETHING, even in SSR mode.
      // eFragment only: water.frag is the only stage that samples it
      // (water.tese only computes the UV it'll be sampled at).
      vk::DescriptorSetLayoutBinding{
          2, vk::DescriptorType::eCombinedImageSampler, 1,
          vk::ShaderStageFlagBits::eFragment, nullptr}};

  setLayout = buildDescriptorSetLayout(context, bindings);
  debug::setObjName(
      context, vk::ObjectType::eDescriptorSetLayout,
      reinterpret_cast<uint64_t>(static_cast<VkDescriptorSetLayout>(setLayout)),
      "water set layout");
}

void WaterSys::update(bool driveSimulation) {
  // Every WaterSys instance is now a static patch fixed at its own world
  // center/height and sized to its own bounding box - this is what lets
  // several lakes each sit at a different Y. There's no camera-follow
  // mode anymore (that was only ever useful for an infinite ocean tile,
  // which this engine no longer spawns).
  vertexUBO.model =
      glm::translate(glm::mat4(1.0f), createInfo.center) *
      glm::scale(glm::mat4(1.0f),
                glm::vec3(createInfo.size.x, 1.f, createInfo.size.y));
  // Drives water.frag's wind-scrolled foam offset. Deliberately its own
  // wall-clock read rather than reusing the `t` passed to
  // modelTess.recordComputeWaves() below - that time belongs to the
  // shared WSTessendorf sim (multiplied by createInfo.animSpeed) and
  // stops advancing for this instance whenever driveSimulation is false
  // (see the class doc comment on why only one WaterSys instance per
  // shared tess should drive it) - wind should keep drifting for every
  // lake regardless of which one happens to own that responsibility.
  vertexUBO.time = static_cast<float>(glfwGetTime());

  vertexUBO.reflViewProj = getPlanarReflectionViewProj();

  updateUniformBuffer();
  updateDescriptorSet(sets[context.frameInfo.frameIndex]);

  if (driveSimulation && playAnimation) {
    modelTess.recordComputeWaves(
        context.frameInfo.cmd,
        static_cast<float>(glfwGetTime()) * createInfo.animSpeed);
  }
}

void WaterSys::render() {
  auto cmd = context.frameInfo.cmd;
  debug::beginLabel(context, cmd, "WaterSys render", {0.f, 0.f, 1.f, 1.f});
  pipeline->bind(cmd);

  std::vector<vk::DescriptorSet> bindSets = {
      context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex],
      sets[context.frameInfo.frameIndex]};

  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline, 0,
                         static_cast<uint32_t>(bindSets.size()),
                         bindSets.data(), 0, nullptr);
  scene->bind(context, cmd, *pipeline);

  scene->meshes.begin()->primitives.begin()->draw(cmd);
  debug::endLabel(context, cmd);
}

void WaterSys::downloadDisplacementAtWorldPos() {
  modelTess.getDisplacementFoamImage();
}

VkDescriptorImageInfo WaterSys::getPlanarReflectionImageInfo() const {
  return planarReflection->getReflectionImageInfo();
}

glm::mat4 WaterSys::getPlanarReflectionViewProj() const {
  return planarReflection->getReflViewProj();
}

} // namespace vkh
