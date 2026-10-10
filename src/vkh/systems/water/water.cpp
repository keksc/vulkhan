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

#include <algorithm>
#include <bit>

namespace vkh {

struct WaterSys::Shared {
  EngineContext &context;
  vk::DescriptorSetLayout setLayout{};
  vk::Sampler repeatSampler{}; // eRepeat, not context.vulkan.defaultSampler
  std::unique_ptr<GraphicsPipeline> pipeline;

  explicit Shared(EngineContext &ctx) : context(ctx) {}
  ~Shared() {
    pipeline.reset(); // before the layout it was built from
    if (context.vulkan.device) {
      context.vulkan.device.destroyDescriptorSetLayout(setLayout, nullptr);
      context.vulkan.device.destroySampler(repeatSampler, nullptr);
    }
  }
};

void WaterSys::createShared() {
  // All lakes live on the main thread, so a plain static is enough.
  static std::weak_ptr<Shared> cache;
  if (auto existing = cache.lock()) {
    shared = std::move(existing);
    return;
  }
  shared = std::make_shared<Shared>(context);

  // --- descriptor set layout ---
  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      // Binding 0: UBO
      vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eUniformBuffer, 1,
                                     vk::ShaderStageFlagBits::eVertex |
                                         vk::ShaderStageFlagBits::eFragment,
                                     nullptr},
      // Binding 1: Displacement Map (RGB), Foam (A)
      vk::DescriptorSetLayoutBinding{
          1, vk::DescriptorType::eCombinedImageSampler, 1,
          vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
          nullptr},
      // Binding 2: Planar reflection color target. Always bound to SOMETHING
      // (see updateDescriptorSet()); eFragment only.
      vk::DescriptorSetLayoutBinding{
          2, vk::DescriptorType::eCombinedImageSampler, 1,
          vk::ShaderStageFlagBits::eFragment, nullptr},
      vk::DescriptorSetLayoutBinding{
          3, vk::DescriptorType::eCombinedImageSampler, 1,
          vk::ShaderStageFlagBits::eFragment, nullptr}};
  shared->setLayout = buildDescriptorSetLayout(context, bindings);
  debug::setObjName(context, vk::ObjectType::eDescriptorSetLayout,
                    reinterpret_cast<uint64_t>(
                        static_cast<VkDescriptorSetLayout>(shared->setLayout)),
                    "water set layout");

  // --- sampler ---
  // The shared displacement/foam image is one WSTessendorf tile that a lake
  // samples several times (WaterCreateInfo::uvRepeat) -> eRepeat. It has a
  // per-frame mip chain, so trilinear + anisotropic handles the grazing-angle
  // minification a flat water plane sees constantly.
  vk::SamplerCreateInfo samplerInfo{};
  samplerInfo.magFilter = vk::Filter::eLinear;
  samplerInfo.minFilter = vk::Filter::eLinear;
  samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
  samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
  samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
  samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
  samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.maxAnisotropy =
      context.vulkan.physicalDeviceProperties.limits.maxSamplerAnisotropy;
  shared->repeatSampler = context.vulkan.device.createSampler(samplerInfo);
  debug::setObjName(
      context, vk::ObjectType::eSampler,
      reinterpret_cast<uint64_t>(static_cast<VkSampler>(shared->repeatSampler)),
      "water repeat sampler");

  // --- pipeline ---
  PipelineCreateInfo pipelineInfo{};
  std::array<vk::DescriptorSetLayout, 2> setLayouts = {
      context.vulkan.globalDescriptorSetLayout, shared->setLayout};
  pipelineInfo.layoutInfo.setLayoutCount =
      static_cast<uint32_t>(setLayouts.size());
  pipelineInfo.layoutInfo.pSetLayouts = setLayouts.data();
  pipelineInfo.inputAssemblyInfo.topology =
      vk::PrimitiveTopology::eTriangleList;
  pipelineInfo.attributeDescriptions = Vertex::attribDescriptions;
  pipelineInfo.bindingDescriptions = Vertex::bindingDescriptions;
  pipelineInfo.rasterizationInfo.polygonMode = vk::PolygonMode::eFill;
  pipelineInfo.vertpath = "shaders/water/water.vert.spv";
  pipelineInfo.fragpath = "shaders/water/water.frag.spv";
  pipelineInfo.multisampleInfo.rasterizationSamples =
      static_cast<vk::SampleCountFlagBits>(context.vulkan.msaaSamples);
  // The lake bed has already been drawn. Blend surface colour over it,
  // while retaining the opaque scene-alpha contract for postprocessing.
  auto &blend = pipelineInfo.colorBlendAttachment;
  blend.blendEnable = vk::True;
  blend.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
  blend.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
  blend.srcAlphaBlendFactor = vk::BlendFactor::eZero;
  blend.dstAlphaBlendFactor = vk::BlendFactor::eOne;
  shared->pipeline =
      std::make_unique<GraphicsPipeline>(context, pipelineInfo, "water");

  cache = shared;
}

WaterSys::WaterSys(EngineContext &context, SkySys &skySys, WSTessendorf &tess,
                   const WaterCreateInfo &info)
    : System{context}, skySys{skySys}, modelTess{tess}, createInfo{info} {
  vertexUBO.scale = createInfo.waveScale;
  vertexUBO.uvRepeat = createInfo.uvRepeat;
  vertexUBO.chopScale = createInfo.chopScale;
  vertexUBO.shoreFadeDepth = createInfo.shoreFadeDepth;
  vertexUBO.cellWorld = createInfo.meshCellSize;

  createShared();
  createMesh();
  createRenderData();
}

// Pipeline, layout, sampler and vertex buffer belong to `shared` and go away
// with the last lake.
WaterSys::~WaterSys() = default;

void WaterSys::reconfigure(const WaterCreateInfo &info) {
  createInfo = info;
  vertexUBO.scale = createInfo.waveScale;
  vertexUBO.uvRepeat = createInfo.uvRepeat;
  vertexUBO.chopScale = createInfo.chopScale;
  vertexUBO.shoreFadeDepth = createInfo.shoreFadeDepth;
  vertexUBO.cellWorld = createInfo.meshCellSize;
  planarReflection = nullptr;
  createMesh(); // rewrites the index list in place
}

// CPU only: the GPU copies are made per frame slot in syncMesh().
// Converts the world-space surface mesh into the local space the shaders
// expect: model = translate(center) * scale(size.x, 1, size.y), so vertices
// are (p - center.xz) / size in [-0.5, 0.5] and uv covers 0..1 over the
// bounding box (times uvRepeat in the shaders), exactly like the old unit grid.
void WaterSys::createMesh() {
  vertices.clear();
  indices.clear();

  const auto &xz = createInfo.meshXZ;
  const auto &dep = createInfo.meshDepth;
  const auto &idx = createInfo.meshIndices;
  // Malformed input renders nothing instead of reading out of bounds.
  bool ok = !idx.empty() && idx.size() % 4 == 0 && dep.size() == xz.size() &&
            createInfo.size.x > 0.f && createInfo.size.y > 0.f;
  for (size_t i = 0; ok && i < idx.size(); ++i)
    ok = idx[i] < xz.size();

  if (ok) {
    const glm::vec2 c(createInfo.center.x, createInfo.center.z);
    vertices.reserve(xz.size());
    for (size_t i = 0; i < xz.size(); ++i) {
      const glm::vec2 uv = (xz[i] - c) / createInfo.size + 0.5f;
      vertices.emplace_back(glm::vec3(uv.x - 0.5f, 0.f, uv.y - 0.5f), uv,
                            dep[i]);
    }
    // Quad (v0,v1,v2,v3) -> two triangles, same winding as the terrain mesh
    // (i0,i2,i1 / i1,i2,i3 with i2 = (x, z+1)).
    indices.reserve(idx.size() / 4 * 6);
    for (size_t i = 0; i < idx.size(); i += 4)
      indices.insert(indices.end(), {idx[i], idx[i + 3], idx[i + 1], idx[i + 1],
                                     idx[i + 3], idx[i + 2]});
  }
  // The GPU-format copy above is all that is needed from here on; do not hold
  // a second copy of the lake mesh for the life of the instance.
  createInfo.meshXZ = {};
  createInfo.meshDepth = {};
  createInfo.meshIndices = {};
  ++meshVersion; // every frame slot re-uploads on its next update()
}

// Called from update(), i.e. once this frame slot's previous use has retired
// (the same guarantee the per-frame uniform buffer write relies on). Buffers
// are replaced only here, and no descriptor set references them, so growing
// one never touches data a frame in flight may read.
void WaterSys::syncMesh() {
  FrameMesh &fm = frameMeshes[context.frameInfo.frameIndex];
  if (fm.version == meshVersion)
    return;

  const vk::MemoryPropertyFlags hostMem =
      vk::MemoryPropertyFlagBits::eHostVisible |
      vk::MemoryPropertyFlagBits::eHostCoherent;
  auto ensure = [&](std::unique_ptr<Buffer<std::byte>> &buf,
                    vk::DeviceSize &cap, vk::BufferUsageFlags usage,
                    vk::DeviceSize need) {
    if (need == 0 || (buf && cap >= need))
      return;
    cap = std::bit_ceil(std::max<uint64_t>(need, 4096));
    buf = std::make_unique<Buffer<std::byte>>(context, usage, hostMem, cap);
    buf->map();
  };

  const vk::DeviceSize vbBytes = vertices.size() * sizeof(Vertex);
  const vk::DeviceSize ibBytes = indices.size() * sizeof(uint32_t);
  ensure(fm.vertexBuffer, fm.vertexCap, vk::BufferUsageFlagBits::eVertexBuffer,
         vbBytes);
  ensure(fm.indexBuffer, fm.indexCap, vk::BufferUsageFlagBits::eIndexBuffer,
         ibBytes);

  fm.indexCount = static_cast<uint32_t>(indices.size());
  if (vbBytes > 0)
    fm.vertexBuffer->write(vertices.data(), vbBytes);
  if (ibBytes > 0)
    fm.indexBuffer->write(indices.data(), ibBytes);
  fm.version = meshVersion;
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
    auto set =
        context.vulkan.globalDescriptorAllocator->allocate(shared->setLayout);
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
  bound.assign(imageCount, BoundImages{});

  // One FrameMesh per frame slot; its buffers are created on demand in
  // syncMesh().
  frameMeshes.clear();
  frameMeshes.resize(imageCount);
}

void WaterSys::updateDescriptorSet(vk::DescriptorSet set) {
  BoundImages &cached = bound[context.frameInfo.frameIndex];

  // Binding 1: displacement map, through the shared eRepeat sampler.
  VkDescriptorImageInfo imageInfo =
      modelTess.getDisplacementFoamImage().getDescriptorInfo(
          shared->repeatSampler);
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkDescriptorImageInfo detailInfo =
      modelTess.getDetailImage().getDescriptorInfo(shared->repeatSampler);
  detailInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  // Binding 2: planar reflection target. Must ALWAYS be a valid, readable
  // image even when nothing samples it, so fall back to re-binding the
  // displacement map when there is no reflection source.
  VkDescriptorImageInfo planarReflInfo =
      planarReflection ? getPlanarReflectionImageInfo() : imageInfo;

  // The UBO binding never changes for a given frame slot, and the images only
  // change when the reflection target does - skip the write (and its driver
  // cost, per lake per frame) when nothing differs from what's bound.
  if (cached.uboWritten && cached.disp == imageInfo.imageView &&
      cached.refl == planarReflInfo.imageView)
    return;

  VkDescriptorBufferInfo bufferInfo{};
  bufferInfo.buffer =
      static_cast<vk::Buffer>(uniformBuffers[context.frameInfo.frameIndex]);
  bufferInfo.offset = 0;
  bufferInfo.range = sizeof(VertexUBO);

  DescriptorWriter writer(context);
  writer.writeBuffer(0, bufferInfo, vk::DescriptorType::eUniformBuffer);
  writer.writeImage(1, imageInfo, vk::DescriptorType::eCombinedImageSampler);
  writer.writeImage(2, planarReflInfo,
                    vk::DescriptorType::eCombinedImageSampler);
  writer.writeImage(3, detailInfo, vk::DescriptorType::eCombinedImageSampler);
  writer.updateSet(set);

  cached.uboWritten = true;
  cached.disp = imageInfo.imageView;
  cached.refl = planarReflInfo.imageView;
}

void WaterSys::updateUniformBuffer() {
  auto &buffer = uniformBuffers[context.frameInfo.frameIndex];
  buffer.write(&vertexUBO, sizeof(vertexUBO));
  buffer.flush();
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
  vertexUBO.time = context.time;

  if (planarReflection)
    vertexUBO.reflViewProj = getPlanarReflectionViewProj();

  // Upload this slot's mesh first, so render() always finds it in place.
  syncMesh();

  updateUniformBuffer();
  updateDescriptorSet(sets[context.frameInfo.frameIndex]);

  if (driveSimulation && playAnimation) {
    modelTess.recordComputeWaves(context.frameInfo.cmd,
                                 static_cast<float>(glfwGetTime()) *
                                     createInfo.animSpeed);
  }
}

void WaterSys::render() {
  const FrameMesh &fm = frameMeshes[context.frameInfo.frameIndex];
  if (fm.indexCount == 0 || !fm.vertexBuffer || !fm.indexBuffer)
    return; // empty/malformed mesh, or not synced yet: nothing to draw

  auto cmd = context.frameInfo.cmd;
  debug::beginLabel(context, cmd, "WaterSys render", {0.f, 0.f, 1.f, 1.f});
  shared->pipeline->bind(cmd);

  const std::array<vk::DescriptorSet, 2> bindSets = {
      context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex],
      sets[context.frameInfo.frameIndex]};
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *shared->pipeline, 0,
                         static_cast<uint32_t>(bindSets.size()),
                         bindSets.data(), 0, nullptr);

  const vk::Buffer vb = static_cast<vk::Buffer>(*fm.vertexBuffer);
  const vk::DeviceSize zero = 0;
  cmd.bindVertexBuffers(0, 1, &vb, &zero);
  cmd.bindIndexBuffer(static_cast<vk::Buffer>(*fm.indexBuffer), 0,
                      vk::IndexType::eUint32);
  cmd.drawIndexed(fm.indexCount, 1, 0, 0, 0);
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
