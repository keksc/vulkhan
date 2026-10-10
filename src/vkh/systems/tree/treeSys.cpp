#include "treeSys.hpp"

#include "../../camera.hpp"
#include "../../debug.hpp"
#include "../../pipeline.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>

namespace vkh {

void TreeSys::createSetLayouts() {
  {
    std::vector<vk::DescriptorBindingFlags> bindingFlags = {
        vk::DescriptorBindingFlagBits::ePartiallyBound};

    vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
    bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
    bindingFlagsInfo.pBindingFlags = bindingFlags.data();

    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        vk::DescriptorSetLayoutBinding{
            0, vk::DescriptorType::eCombinedImageSampler, 256,
            vk::ShaderStageFlagBits::eFragment, nullptr}};

    texturesSetLayout = buildDescriptorSetLayout(
        context, bindings, vk::DescriptorSetLayoutCreateFlags{},
        &bindingFlagsInfo);
  }

  {
    // 0: compacted instance buffer (culled/compacted GPUInstanceData, read
    //    by tree.vert). 1: wind UBO (time/strength/dir, read by tree.vert).
    // No joint buffer - trees are never skinned.
    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1,
                                       vk::ShaderStageFlagBits::eVertex},
        vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eUniformBuffer, 1,
                                       vk::ShaderStageFlagBits::eVertex}};

    instanceSetLayout = buildDescriptorSetLayout(context, bindings);
  }
}

void TreeSys::createPipeline() {
  std::vector<vk::DescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout, texturesSetLayout,
      instanceSetLayout};

  // Same clip-plane push constant as EntitySys, for planar reflections of
  // forests near lake shorelines.
  vk::PushConstantRange clipPlaneRange{};
  clipPlaneRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
  clipPlaneRange.offset = 0;
  clipPlaneRange.size = sizeof(glm::vec4);

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &clipPlaneRange;

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.attributeDescriptions = Vertex::getAttributeDescriptions();
  pipelineInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  pipelineInfo.vertpath = "shaders/tree/tree.vert.spv";
  pipelineInfo.fragpath = "shaders/tree/tree.frag.spv";

  // Foliage meshes are double-sided (no back-face culling) so the flip
  // logic in tree.frag actually gets used from both sides; trunks happen
  // to be closed solids so this costs them nothing but unused triangle
  // setup for hidden backfaces.
  pipelineInfo.rasterizationInfo.cullMode = vk::CullModeFlagBits::eNone;

  pipelineInfo.multisampleInfo.rasterizationSamples =
      static_cast<vk::SampleCountFlagBits>(context.vulkan.msaaSamples);
  // Soft, MSAA-resolved leaf-cutout edges instead of a hard `discard`
  // silhouette - requires rasterizationSamples > 1 (already true here) and
  // alpha to be a meaningful coverage value, which tree.frag's alpha
  // output is for foliage. If your PipelineCreateInfo/GraphicsPipeline
  // doesn't yet expose alphaToCoverageEnable, add it there; tree.frag
  // still works correctly (just with harder edges) without it.
  pipelineInfo.multisampleInfo.alphaToCoverageEnable = VK_TRUE;

  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo, "trees");
}

void TreeSys::createCullingPipeline() {
  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{3, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{4, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{5, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{6, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute}};

  cullingSetLayout = buildDescriptorSetLayout(context, bindings);

  vk::PipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &cullingSetLayout;
  const vk::PushConstantRange cameraRange{vk::ShaderStageFlagBits::eCompute, 0,
                                          sizeof(CullingPush)};
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &cameraRange;

  cullingPipeline = std::make_unique<ComputePipeline>(
      context, "shaders/tree/treeCulling.comp.spv", layoutInfo,
      "tree culling compute");
}

void TreeSys::createFinalizePipeline() {
  vk::PipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &cullingSetLayout;
  const vk::PushConstantRange cameraRange{vk::ShaderStageFlagBits::eCompute, 0,
                                          sizeof(CullingPush)};
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &cameraRange;

  finalizePipeline = std::make_unique<ComputePipeline>(
      context, "shaders/tree/treeCullingFinalize.comp.spv", layoutInfo,
      "tree culling finalize compute");
}

TreeSys::TreeSys(EngineContext &context) : System(context) {
  createSetLayouts();
  createPipeline();
  createCullingPipeline();
  createFinalizePipeline();

  const uint32_t framesInFlight = context.vulkan.maxFramesInFlight;
  slots_.resize(framesInFlight);

  const auto hostBuf = vk::MemoryPropertyFlagBits::eHostVisible |
                       vk::MemoryPropertyFlagBits::eHostCoherent;
  for (Slot &slot : slots_) {
    slot.wind = std::make_unique<Buffer<WindUbo>>(
        context, vk::BufferUsageFlagBits::eUniformBuffer, hostBuf, 1);
  }
}

TreeSys::~TreeSys() {
  if (context.vulkan.device) {
    context.vulkan.device.destroyDescriptorSetLayout(texturesSetLayout,
                                                     nullptr);
    context.vulkan.device.destroyDescriptorSetLayout(instanceSetLayout,
                                                     nullptr);
    context.vulkan.device.destroyDescriptorSetLayout(cullingSetLayout, nullptr);
  }
}

uint32_t TreeSys::addVariant(TreeVariant variant) {
  assert(variant.scene && "TreeSys::addVariant: null scene");
  assert(variant.meshIsFoliage.size() == variant.scene->meshes.size() &&
         "TreeSys::addVariant: meshIsFoliage must have one entry per "
         "scene->meshes[] entry");
  uint32_t index = static_cast<uint32_t>(variants_.size());
  variants_.push_back(std::move(variant));
  return index;
}

void TreeSys::setPlacements(std::vector<TreePlacement> placements) {
  setInstanceSet(buildInstanceSet(placements));
}

void TreeSys::setInstanceSet(std::shared_ptr<const InstanceSet> set) {
  current_ = std::move(set);
}

// Deterministic pseudo-random phase in [0, 2*PI) from world position, so a
// forest doesn't sway in lockstep without needing an RNG dependency here.
static float phaseFromPosition(const glm::vec3 &p) {
  float h = glm::dot(p, glm::vec3(12.9898f, 78.233f, 37.719f));
  h = h - glm::floor(h / 6.2831853f) * 6.2831853f; // fmod into [0, 2pi)
  return h;
}

std::shared_ptr<const TreeSys::InstanceSet>
TreeSys::buildInstanceSet(std::span<const TreePlacement> placements) const {
  auto out = std::make_shared<InstanceSet>();
  InstanceSet &set = *out;

  // Counting-sort placement indices by variant: one pass over the (possibly
  // tens of thousands of) placements instead of one per (variant, primitive).
  const size_t variantCount = variants_.size();
  std::vector<uint32_t> bucketStart(variantCount + 1, 0u);
  for (const TreePlacement &tp : placements)
    if (tp.variant < variantCount)
      ++bucketStart[tp.variant + 1];
  for (size_t v = 0; v < variantCount; ++v)
    bucketStart[v + 1] += bucketStart[v];
  std::vector<uint32_t> order(bucketStart[variantCount]);
  {
    std::vector<uint32_t> cursor(bucketStart.begin(), bucketStart.end() - 1);
    for (uint32_t i = 0; i < placements.size(); ++i)
      if (placements[i].variant < variantCount)
        order[cursor[placements[i].variant]++] = i;
  }

  // Rough reserve: every placement appears once per primitive of its variant.
  {
    size_t total = 0;
    for (size_t v = 0; v < variantCount; ++v) {
      size_t prims = 0;
      if (variants_[v].scene)
        for (const auto &m : variants_[v].scene->meshes)
          prims += m.primitives.size();
      total += prims * (bucketStart[v + 1] - bucketStart[v]);
    }
    set.instances.reserve(total);
  }

  struct Xf {
    glm::mat4 model;
    glm::mat4 normal;
    glm::mat3 absLinear; // |upper 3x3|, for transforming AABB extents
    float phase;
  };
  std::vector<Xf> xf;

  for (uint32_t vi = 0; vi < variantCount; vi++) {
    const TreeVariant &variant = variants_[vi];
    if (!variant.scene)
      continue;

    // Transforms depend only on the placement, not on the primitive: build
    // them once per tree. Yaw about Y + uniform scale, so the matrices are
    // written out directly (the normal matrix is the rotation / scale).
    const uint32_t first = bucketStart[vi], last = bucketStart[vi + 1];
    xf.resize(last - first);
    for (uint32_t k = first; k < last; ++k) {
      const TreePlacement &tp = placements[order[k]];
      const float c = std::cos(tp.yaw), s = std::sin(tp.yaw), sc = tp.scale;
      Xf &x = xf[k - first];
      x.model = glm::mat4(
          glm::vec4(c * sc, 0.f, -s * sc, 0.f), glm::vec4(0.f, sc, 0.f, 0.f),
          glm::vec4(s * sc, 0.f, c * sc, 0.f), glm::vec4(tp.position, 1.f));
      const float inv = 1.f / sc;
      x.normal = glm::mat4(
          glm::vec4(c * inv, 0.f, -s * inv, 0.f), glm::vec4(0.f, inv, 0.f, 0.f),
          glm::vec4(s * inv, 0.f, c * inv, 0.f), glm::vec4(0.f, 0.f, 0.f, 1.f));
      x.absLinear = glm::mat3(glm::abs(glm::vec3(x.model[0])),
                              glm::abs(glm::vec3(x.model[1])),
                              glm::abs(glm::vec3(x.model[2])));
      x.phase = phaseFromPosition(tp.position);
    }

    const uint32_t firstDrawCommandOffset =
        static_cast<uint32_t>(set.commands.size());

    for (size_t mi = 0; mi < variant.scene->meshes.size(); mi++) {
      const auto &mesh = variant.scene->meshes[mi];
      const bool isFoliage = variant.meshIsFoliage[mi];
      const glm::vec3 center = 0.5f * (mesh.aabb.min + mesh.aabb.max);
      const glm::vec3 extent = 0.5f * (mesh.aabb.max - mesh.aabb.min);

      // A mesh can have several primitives; each is its own draw range and
      // material, so each gets its own group/indirect command, sharing the
      // mesh's AABB and foliage flag.
      for (const auto &prim : mesh.primitives) {
        const bool hasMaterial =
            prim.materialIndex < variant.scene->materials.size();
        const auto &material =
            hasMaterial ? variant.scene->materials[prim.materialIndex]
                        : Scene<Vertex>::Material{};

        const int32_t textureIndex =
            material.baseColorTextureIndex
                ? static_cast<int32_t>(*material.baseColorTextureIndex)
                : -1;
        const int32_t mrTextureIndex =
            material.metallicRoughnessTextureIndex
                ? static_cast<int32_t>(*material.metallicRoughnessTextureIndex)
                : -1;
        const float roughnessFactor = material.roughnessFactor;
        // Material::metallicFactor is a vec4; .x is the glTF scalar.
        const float metallicFactor = material.metallicFactor.x;

        const uint32_t groupIndex =
            static_cast<uint32_t>(set.groupFirstInstance.size());
        const uint32_t firstInstance =
            static_cast<uint32_t>(set.instances.size());

        for (const Xf &x : xf) {
          GPUInstanceData inst{};
          inst.modelMatrix = x.model;
          inst.normalMatrix = x.normal;
          // World AABB of the local AABB: transformed centre +- |M| * extent.
          const glm::vec3 wc = glm::vec3(x.model * glm::vec4(center, 1.f));
          const glm::vec3 we = x.absLinear * extent;
          const glm::vec3 windPad = (variant.flags & InstanceFlagBits::eRock)
                                        ? glm::vec3(0.f)
                                        : glm::vec3(2.f, 0.f, 2.f);
          inst.aabbMin = wc - we - windPad;
          inst.aabbMax = wc + we + windPad;
          inst.textureIndex = textureIndex;
          inst.metallicRoughnessTextureIndex = mrTextureIndex;
          inst.roughnessFactor = roughnessFactor;
          inst.metallicFactor = metallicFactor;
          inst.windPhase = x.phase;
          inst.flags =
              variant.flags | (isFoliage ? InstanceFlagBits::eFoliage : 0u);
          auto tint = isFoliage && foliageTint
                          ? foliageTint(glm::vec3(x.model[3]))
                          : glm::vec3(1.f);
          if ((variant.flags & InstanceFlagBits::eRock) && rockTint)
            tint = rockTint(glm::vec3(x.model[3]));
          inst._pad0 = tint.r;
          inst._pad1 = tint.g;
          inst._pad2 = tint.b;
          inst.groupIndex = groupIndex;
          set.instances.push_back(inst);
        }

        set.groupFirstInstance.push_back(firstInstance);

        // Single shared vertex/index buffer per Scene (see Scene::bind()),
        // so indices are absolute and vertexOffset stays 0.
        vk::DrawIndexedIndirectCommand cmd{};
        cmd.indexCount = prim.indexCount;
        cmd.instanceCount = 0; // written every frame by the finalize pass
        cmd.firstIndex = prim.indexOffset;
        cmd.vertexOffset = 0;
        cmd.firstInstance = firstInstance;

        set.commandGroupIndex.push_back(groupIndex);
        set.commands.push_back(cmd);
      }
    }

    const uint32_t drawCommandCount =
        static_cast<uint32_t>(set.commands.size()) - firstDrawCommandOffset;
    if (drawCommandCount > 0)
      set.batches.push_back({variant.scene, firstDrawCommandOffset,
                             drawCommandCount, variant.passMask});
  }

  // Variants that share a scene (e.g. several LOD tiers pointing at the same
  // low-poly mesh) end up adjacent, so render() binds each scene once.
  std::stable_sort(set.batches.begin(), set.batches.end(),
                   [](const MeshGroupBatch &a, const MeshGroupBatch &b) {
                     return a.scene.get() < b.scene.get();
                   });
  return out;
}

namespace {
// Capacity policy: grow with headroom so a forest that breathes by a few
// percent as tiles stream in never reallocates; shrink only when well over-
// provisioned so a one-off spike does not pin the memory forever.
uint32_t chooseCapacity(uint32_t needed, uint32_t current, uint32_t quantum) {
  needed = std::max(needed, 1u);
  if (needed <= current && needed * 3u >= current)
    return current;
  const uint64_t grown = static_cast<uint64_t>(needed) * 5u / 4u + quantum;
  const uint64_t rounded = (grown + quantum - 1) / quantum * quantum;
  return static_cast<uint32_t>(
      std::min<uint64_t>(rounded, std::numeric_limits<uint32_t>::max()));
}
} // namespace

// Adopts the newest instance set for this slot and makes sure the buffers
// and descriptor sets are big enough for it. Descriptor sets are allocated
// once per slot and only REWRITTEN when a buffer is recreated (the old code
// allocated fresh sets on every rebuild; descriptor sets are never freed back
// to the global allocator, so a streaming forest would have exhausted it).
void TreeSys::ensureBuffers(int frameIndex) {
  Slot &slot = slots_[frameIndex];
  if (!slot.set)
    return;
  const InstanceSet &set = *slot.set;

  const uint32_t instanceCap = chooseCapacity(
      static_cast<uint32_t>(set.instances.size()), slot.instanceCap, 4096u);
  const uint32_t drawCap = chooseCapacity(
      static_cast<uint32_t>(set.commands.size()), slot.drawCap, 16u);
  const uint32_t groupCap = chooseCapacity(
      static_cast<uint32_t>(set.groupFirstInstance.size()), slot.groupCap, 16u);

  const bool instancesChanged = instanceCap != slot.instanceCap;
  const bool drawsChanged = drawCap != slot.drawCap;
  const bool groupsChanged = groupCap != slot.groupCap;
  const bool needDescriptors = !slot.instanceSet || !slot.cullingSet ||
                               instancesChanged || drawsChanged ||
                               groupsChanged;
  if (!needDescriptors)
    return;

  const auto hostBuf = vk::MemoryPropertyFlagBits::eHostVisible |
                       vk::MemoryPropertyFlagBits::eHostCoherent;
  const auto deviceLocal = vk::MemoryPropertyFlagBits::eDeviceLocal;

  if (instancesChanged || !slot.rawInstances) {
    slot.rawInstances = std::make_unique<Buffer<GPUInstanceData>>(
        context,
        vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferDst,
        deviceLocal, instanceCap);
    slot.compacted = std::make_unique<Buffer<GPUInstanceData>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer, deviceLocal,
        instanceCap);
    slot.instanceCap = instanceCap;
    slot.uploaded = false; // new, empty VRAM buffer
  }
  if (drawsChanged || !slot.indirect) {
    slot.indirect = std::make_unique<Buffer<vk::DrawIndexedIndirectCommand>>(
        context,
        vk::BufferUsageFlagBits::eIndirectBuffer |
            vk::BufferUsageFlagBits::eStorageBuffer,
        hostBuf, drawCap);
    slot.commandGroupIndex = std::make_unique<Buffer<uint32_t>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer, hostBuf, drawCap);
    slot.drawCap = drawCap;
  }
  if (groupsChanged || !slot.groupFirstInstance) {
    slot.groupFirstInstance = std::make_unique<Buffer<uint32_t>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer, hostBuf, groupCap);
    // eTransferDst: dispatchCulling() zeroes this with vkCmdFillBuffer before
    // every cull dispatch.
    slot.groupVisibleCount = std::make_unique<Buffer<uint32_t>>(
        context,
        vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferDst,
        deviceLocal, groupCap);
    slot.groupCap = groupCap;
  }

  if (!slot.instanceSet)
    slot.instanceSet =
        context.vulkan.globalDescriptorAllocator->allocate(instanceSetLayout);
  if (!slot.cullingSet)
    slot.cullingSet =
        context.vulkan.globalDescriptorAllocator->allocate(cullingSetLayout);

  // -- instance descriptor set (set 2: compacted instances + wind UBO) --
  {
    vk::DescriptorBufferInfo instBufInfo{*slot.compacted, 0, VK_WHOLE_SIZE};
    vk::DescriptorBufferInfo windBufInfo{*slot.wind, 0, VK_WHOLE_SIZE};
    std::array<vk::WriteDescriptorSet, 2> writes{
        vk::WriteDescriptorSet{slot.instanceSet, 0, 0, 1,
                               vk::DescriptorType::eStorageBuffer, nullptr,
                               &instBufInfo},
        vk::WriteDescriptorSet{slot.instanceSet, 1, 0, 1,
                               vk::DescriptorType::eUniformBuffer, nullptr,
                               &windBufInfo}};
    context.vulkan.device.updateDescriptorSets(writes, nullptr);
  }

  // -- culling descriptor set (bindings 1-6, see createCullingPipeline) --
  {
    vk::DescriptorBufferInfo rawInstInfo{*slot.rawInstances, 0, VK_WHOLE_SIZE};
    vk::DescriptorBufferInfo compactInstInfo{*slot.compacted, 0, VK_WHOLE_SIZE};
    vk::DescriptorBufferInfo groupFirstInfo{*slot.groupFirstInstance, 0,
                                            VK_WHOLE_SIZE};
    vk::DescriptorBufferInfo groupVisibleInfo{*slot.groupVisibleCount, 0,
                                              VK_WHOLE_SIZE};
    vk::DescriptorBufferInfo cmdGroupInfo{*slot.commandGroupIndex, 0,
                                          VK_WHOLE_SIZE};
    vk::DescriptorBufferInfo indirectInfo{*slot.indirect, 0, VK_WHOLE_SIZE};

    std::array<vk::WriteDescriptorSet, 6> writes{
        vk::WriteDescriptorSet{slot.cullingSet, 1, 0, 1,
                               vk::DescriptorType::eStorageBuffer, nullptr,
                               &rawInstInfo},
        vk::WriteDescriptorSet{slot.cullingSet, 2, 0, 1,
                               vk::DescriptorType::eStorageBuffer, nullptr,
                               &compactInstInfo},
        vk::WriteDescriptorSet{slot.cullingSet, 3, 0, 1,
                               vk::DescriptorType::eStorageBuffer, nullptr,
                               &groupFirstInfo},
        vk::WriteDescriptorSet{slot.cullingSet, 4, 0, 1,
                               vk::DescriptorType::eStorageBuffer, nullptr,
                               &groupVisibleInfo},
        vk::WriteDescriptorSet{slot.cullingSet, 5, 0, 1,
                               vk::DescriptorType::eStorageBuffer, nullptr,
                               &cmdGroupInfo},
        vk::WriteDescriptorSet{slot.cullingSet, 6, 0, 1,
                               vk::DescriptorType::eStorageBuffer, nullptr,
                               &indirectInfo}};
    context.vulkan.device.updateDescriptorSets(writes, nullptr);
  }
}

void TreeSys::updateBuffers() {
  const int frameIndex = context.frameInfo.frameIndex;
  Slot &slot = slots_[frameIndex];

  // The slot's previous frame has retired: last upload's staging is free.
  slot.staging.reset();

  if (current_ && slot.set != current_) {
    slot.set = current_;
    slot.uploaded = false;
  }
  ensureBuffers(frameIndex);

  // The small host-visible tables are rewritten whenever this slot adopts a
  // new set (cheap: a few dozen draw commands and group offsets).
  if (slot.set && !slot.uploaded) {
    const InstanceSet &set = *slot.set;
    if (!set.commands.empty()) {
      slot.indirect->map();
      slot.indirect->write(set.commands.data(),
                           set.commands.size() *
                               sizeof(vk::DrawIndexedIndirectCommand));
      slot.indirect->unmap();
      slot.commandGroupIndex->map();
      slot.commandGroupIndex->write(set.commandGroupIndex.data(),
                                    set.commandGroupIndex.size() *
                                        sizeof(uint32_t));
      slot.commandGroupIndex->unmap();
    }
    if (!set.groupFirstInstance.empty()) {
      slot.groupFirstInstance->map();
      slot.groupFirstInstance->write(set.groupFirstInstance.data(),
                                     set.groupFirstInstance.size() *
                                         sizeof(uint32_t));
      slot.groupFirstInstance->unmap();
    }
  }

  WindUbo wind{};
  wind.time = context.time;
  wind.strength = context.environment.windStrength;
  wind.dir = safeWindDirection(context.environment);
  slot.wind->map();
  slot.wind->write(&wind, sizeof(WindUbo));
  slot.wind->unmap();
}

// Copies the slot's instance array into its device-local buffer (once per
// new set). Recorded into the first cull of the frame, outside any render
// pass; the barrier orders it before the culling compute reads.
void TreeSys::uploadInstances(vk::CommandBuffer cmd, int frameIndex) {
  Slot &slot = slots_[frameIndex];
  if (slot.uploaded || !slot.set)
    return;
  const InstanceSet &set = *slot.set;
  if (set.instances.empty()) {
    slot.uploaded = true;
    return;
  }

  const vk::DeviceSize bytes = set.instances.size() * sizeof(GPUInstanceData);
  slot.staging = std::make_unique<Buffer<GPUInstanceData>>(
      context, vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent,
      set.instances.size());
  slot.staging->map();
  slot.staging->write(set.instances.data(), bytes);
  slot.staging->unmap();

  vk::BufferCopy region{0, 0, bytes};
  cmd.copyBuffer(*slot.staging, *slot.rawInstances, 1, &region);

  vk::BufferMemoryBarrier barrier{};
  barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
  barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = *slot.rawInstances;
  barrier.offset = 0;
  barrier.size = bytes;
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                      vk::PipelineStageFlagBits::eComputeShader,
                      vk::DependencyFlags(), nullptr, barrier, nullptr);

  slot.uploaded = true;
}

void TreeSys::cull(vk::CommandBuffer cmd) {
  cullForViewProj(cmd,
                  context.camera.projectionMatrix * context.camera.viewMatrix);
}

void TreeSys::cullForViewProj(vk::CommandBuffer cmd,
                              const glm::mat4 &viewProj) {
  const int frameIndex = context.frameInfo.frameIndex;
  Slot &slot = slots_[frameIndex];
  if (!slot.set || slot.set->instances.empty())
    return;

  uploadInstances(cmd, frameIndex);

  auto planes = camera::getFrustumPlanes(viewProj);
  CullingPush ubo{};
  for (int i = 0; i < 6; i++)
    ubo.frustumPlanes[i] = planes[i];
  ubo.totalInstances = static_cast<uint32_t>(slot.set->instances.size());
  ubo.totalCommands = static_cast<uint32_t>(slot.set->commands.size());

  dispatchCulling(cmd, frameIndex, ubo);
}

void TreeSys::dispatchCulling(vk::CommandBuffer cmd, int frameIndex,
                              const CullingPush &view) {
  Slot &slot = slots_[frameIndex];
  debug::beginLabel(context, cmd, "Tree Culling Dispatch",
                    {.3f, .7f, .3f, 1.f});
  recordCulling(cmd, view,
                {static_cast<vk::Pipeline>(*cullingPipeline),
                 static_cast<vk::Pipeline>(*finalizePipeline),
                 cullingPipeline->getLayout(), slot.cullingSet, *slot.compacted,
                 *slot.indirect, *slot.groupVisibleCount,
                 static_cast<uint32_t>(slot.set->groupFirstInstance.size())});
  debug::endLabel(context, cmd);
}

void TreeSys::render(vk::DescriptorSet globalSetOverride,
                     const glm::vec4 &clipPlane, uint32_t passMask) {
  const int frameIndex = context.frameInfo.frameIndex;
  Slot &slot = slots_[frameIndex];
  if (!slot.set || slot.set->batches.empty() || !slot.instanceSet)
    return;

  auto cmd = context.frameInfo.cmd;

  debug::beginLabel(context, cmd, "Indirect Trees", {.3f, .6f, .2f, 1.f});

  pipeline->bind(cmd);

  cmd.pushConstants(pipeline->getLayout(), vk::ShaderStageFlagBits::eVertex, 0,
                    sizeof(glm::vec4), &clipPlane);

  vk::DescriptorSet globalSet =
      globalSetOverride ? globalSetOverride
                        : context.vulkan.globalDescriptorSets[frameIndex];
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                         pipeline->getLayout(), 0, 1, &globalSet, 0, nullptr);

  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                         pipeline->getLayout(), 2, 1, &slot.instanceSet, 0,
                         nullptr);

  // Batches are sorted by scene, so consecutive batches that share one (LOD
  // tiers pointing at the same mesh) skip the vertex/index/texture rebinds.
  const Scene<Vertex> *boundScene = nullptr;
  for (const auto &batch : slot.set->batches) {
    if (!(batch.passMask & passMask))
      continue;

    if (batch.scene.get() != boundScene) {
      batch.scene->bind(context, cmd, *pipeline);
      vk::DescriptorSet texSet = batch.scene->sceneTextureSet;
      if (texSet) {
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               pipeline->getLayout(), 1, 1, &texSet, 0,
                               nullptr);
      }
      boundScene = batch.scene.get();
    }

    cmd.drawIndexedIndirect(
        *slot.indirect,
        batch.firstDrawCommandOffset * sizeof(vk::DrawIndexedIndirectCommand),
        batch.drawCommandCount, sizeof(vk::DrawIndexedIndirectCommand));
  }

  debug::endLabel(context, cmd);
}

} // namespace vkh
