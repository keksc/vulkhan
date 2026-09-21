#include "treeSys.hpp"

#include "../../camera.hpp"
#include "../../debug.hpp"
#include "../../pipeline.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/quaternion.hpp>

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
      vk::DescriptorSetLayoutBinding{5, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{6, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute}};

  cullingSetLayout = buildDescriptorSetLayout(context, bindings);

  vk::PipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &cullingSetLayout;

  cullingPipeline = std::make_unique<ComputePipeline>(
      context, "shaders/tree/treeCulling.comp.spv", layoutInfo,
      "tree culling compute");

  uint32_t framesInFlight = context.vulkan.maxFramesInFlight;
  cullingUboBuffers.resize(framesInFlight);
  for (uint32_t i = 0; i < framesInFlight; i++) {
    cullingUboBuffers[i] = std::make_unique<Buffer<CullingUbo>>(
        context, vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        1);
  }
}

void TreeSys::createFinalizePipeline() {
  vk::PipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &cullingSetLayout;

  finalizePipeline = std::make_unique<ComputePipeline>(
      context, "shaders/tree/treeCullingFinalize.comp.spv", layoutInfo,
      "tree culling finalize compute");
}

TreeSys::TreeSys(EngineContext &context) : System(context) {
  createSetLayouts();
  createPipeline();
  createCullingPipeline();
  createFinalizePipeline();

  uint32_t framesInFlight = context.vulkan.maxFramesInFlight;
  instanceBuffers.resize(framesInFlight);
  indirectDrawBuffers.resize(framesInFlight);
  windBuffers.resize(framesInFlight);
  compactedInstanceBuffers.resize(framesInFlight);
  groupFirstInstanceBuffers.resize(framesInFlight);
  commandGroupIndexBuffers.resize(framesInFlight);
  groupVisibleCountBuffers.resize(framesInFlight);
  instanceDescriptorSets.resize(framesInFlight, nullptr);
  cullingDescriptorSets.resize(framesInFlight, nullptr);
  framesDirty.resize(framesInFlight, false);

  for (uint32_t i = 0; i < framesInFlight; i++) {
    windBuffers[i] = std::make_unique<Buffer<WindUbo>>(
        context, vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        1);
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
  placements_ = std::move(placements);
  buildInstances();
}

// Deterministic pseudo-random phase in [0, 2*PI) from world position, so a
// forest doesn't sway in lockstep without needing an RNG dependency here -
// same trick as a shader hash, just evaluated on the CPU once per tree.
static float phaseFromPosition(const glm::vec3 &p) {
  float h = glm::dot(p, glm::vec3(12.9898f, 78.233f, 37.719f));
  h = h - glm::floor(h / 6.2831853f) * 6.2831853f; // fmod into [0, 2pi)
  return h;
}

void TreeSys::buildInstances() {
  cpuInstanceData.clear();
  cpuDrawCommands.clear();
  cpuGroupFirstInstance.clear();
  cpuCommandGroupIndex.clear();
  sceneBatches.clear();

  for (uint32_t vi = 0; vi < variants_.size(); vi++) {
    const TreeVariant &variant = variants_[vi];
    if (!variant.scene)
      continue;

    uint32_t firstDrawCommandOffset =
        static_cast<uint32_t>(cpuDrawCommands.size());

    for (size_t mi = 0; mi < variant.scene->meshes.size(); mi++) {
      const auto &mesh = variant.scene->meshes[mi];
      bool isFoliage = variant.meshIsFoliage[mi];

      // A mesh can have several primitives (e.g. bark and needle-cluster
      // sub-materials that weren't merged before export); each primitive
      // is its own draw range and material, so each gets its own group/
      // indirect command, but they all share the mesh's single AABB and
      // foliage flag.
      for (const auto &prim : mesh.primitives) {
        const bool hasMaterial =
            prim.materialIndex < variant.scene->materials.size();
        const auto &material =
            hasMaterial ? variant.scene->materials[prim.materialIndex]
                        : Scene<Vertex>::Material{};

        int32_t textureIndex =
            material.baseColorTextureIndex
                ? static_cast<int32_t>(*material.baseColorTextureIndex)
                : -1;
        int32_t mrTextureIndex =
            material.metallicRoughnessTextureIndex
                ? static_cast<int32_t>(*material.metallicRoughnessTextureIndex)
                : -1;
        float roughnessFactor = material.roughnessFactor;
        // Material::metallicFactor is a vec4 (glTF's metallicFactor is a
        // scalar - this engine's Material struct just stores it widened;
        // .x is the actual value, matching how presumably every other
        // consumer of Scene<Vertex>::Material reads it).
        float metallicFactor = material.metallicFactor.x;

        uint32_t groupIndex =
            static_cast<uint32_t>(cpuGroupFirstInstance.size());
        uint32_t firstInstance = static_cast<uint32_t>(cpuInstanceData.size());

        for (const auto &tp : placements_) {
          if (tp.variant != vi)
            continue;

          glm::mat4 model =
              glm::translate(glm::mat4(1.f), tp.position) *
              glm::toMat4(glm::angleAxis(tp.yaw, glm::vec3(0.f, 1.f, 0.f))) *
              glm::scale(glm::mat4(1.f), glm::vec3(tp.scale));

          // World-space AABB from the mesh's local AABB (mesh.aabb.min/
          // max), transformed corner-by-corner.
          glm::vec3 worldMin(std::numeric_limits<float>::max());
          glm::vec3 worldMax(std::numeric_limits<float>::lowest());
          for (int c = 0; c < 8; c++) {
            glm::vec3 corner((c & 1) ? mesh.aabb.max.x : mesh.aabb.min.x,
                             (c & 2) ? mesh.aabb.max.y : mesh.aabb.min.y,
                             (c & 4) ? mesh.aabb.max.z : mesh.aabb.min.z);
            glm::vec3 worldCorner = glm::vec3(model * glm::vec4(corner, 1.f));
            worldMin = glm::min(worldMin, worldCorner);
            worldMax = glm::max(worldMax, worldCorner);
          }

          GPUInstanceData inst{};
          inst.modelMatrix = model;
          inst.normalMatrix =
              glm::mat4(glm::inverseTranspose(glm::mat3(model)));
          inst.aabbMin = worldMin;
          inst.aabbMax = worldMax;
          inst.textureIndex = textureIndex;
          inst.metallicRoughnessTextureIndex = mrTextureIndex;
          inst.roughnessFactor = roughnessFactor;
          inst.metallicFactor = metallicFactor;
          inst.windPhase = phaseFromPosition(tp.position);
          inst.flags = isFoliage ? InstanceFlagBits::eFoliage : 0u;
          inst.groupIndex = groupIndex;

          cpuInstanceData.push_back(inst);
        }

        cpuGroupFirstInstance.push_back(firstInstance);

        // Single shared vertex/index buffer per Scene (see Scene::bind()),
        // so indices are absolute into that one buffer already -
        // vertexOffset stays 0 rather than needing a per-mesh value.
        vk::DrawIndexedIndirectCommand cmd{};
        cmd.indexCount = prim.indexCount;
        cmd.instanceCount = 0; // written every frame by the finalize pass
        cmd.firstIndex = prim.indexOffset;
        cmd.vertexOffset = 0;
        cmd.firstInstance = firstInstance;

        cpuCommandGroupIndex.push_back(groupIndex);
        cpuDrawCommands.push_back(cmd);
      }
    }

    uint32_t drawCommandCount =
        static_cast<uint32_t>(cpuDrawCommands.size()) - firstDrawCommandOffset;
    if (drawCommandCount > 0)
      sceneBatches.push_back(
          {variant.scene, firstDrawCommandOffset, drawCommandCount});
  }

  std::fill(framesDirty.begin(), framesDirty.end(), true);
}

// (Re)creates and re-uploads every per-frame-in-flight buffer whose CPU-side
// source data changed since it was last built, and rewrites the descriptor
// sets that point at them. setPlacements() is the only thing that dirties
// this, and it's meant to run rarely (world build/regen), not per frame -
// each frameIndex only actually does this work once, on the first
// cull()/render() call after a setPlacements().
void TreeSys::flushBuffers(int frameIndex) {
  if (!framesDirty[frameIndex])
    return;

  uint32_t instanceCount =
      std::max<uint32_t>(1, static_cast<uint32_t>(cpuInstanceData.size()));
  uint32_t drawCount =
      std::max<uint32_t>(1, static_cast<uint32_t>(cpuDrawCommands.size()));
  uint32_t groupCount = std::max<uint32_t>(
      1, static_cast<uint32_t>(cpuGroupFirstInstance.size()));

  auto hostBuf = vk::MemoryPropertyFlagBits::eHostVisible |
                 vk::MemoryPropertyFlagBits::eHostCoherent;
  auto deviceLocal = vk::MemoryPropertyFlagBits::eDeviceLocal;

  instanceBuffers[frameIndex] = std::make_unique<Buffer<GPUInstanceData>>(
      context, vk::BufferUsageFlagBits::eStorageBuffer, hostBuf, instanceCount);
  compactedInstanceBuffers[frameIndex] =
      std::make_unique<Buffer<GPUInstanceData>>(
          context, vk::BufferUsageFlagBits::eStorageBuffer, deviceLocal,
          instanceCount);
  indirectDrawBuffers[frameIndex] =
      std::make_unique<Buffer<vk::DrawIndexedIndirectCommand>>(
          context,
          vk::BufferUsageFlagBits::eIndirectBuffer |
              vk::BufferUsageFlagBits::eStorageBuffer,
          hostBuf, drawCount);
  groupFirstInstanceBuffers[frameIndex] = std::make_unique<Buffer<uint32_t>>(
      context, vk::BufferUsageFlagBits::eStorageBuffer, hostBuf, groupCount);
  commandGroupIndexBuffers[frameIndex] = std::make_unique<Buffer<uint32_t>>(
      context, vk::BufferUsageFlagBits::eStorageBuffer, hostBuf, drawCount);
  groupVisibleCountBuffers[frameIndex] = std::make_unique<Buffer<uint32_t>>(
      context, vk::BufferUsageFlagBits::eStorageBuffer, deviceLocal,
      groupCount);

  if (!cpuInstanceData.empty()) {
    instanceBuffers[frameIndex]->map();
    instanceBuffers[frameIndex]->write(cpuInstanceData.data(),
                                       cpuInstanceData.size() *
                                           sizeof(GPUInstanceData));
    instanceBuffers[frameIndex]->unmap();
  }
  if (!cpuDrawCommands.empty()) {
    indirectDrawBuffers[frameIndex]->map();
    indirectDrawBuffers[frameIndex]->write(
        cpuDrawCommands.data(),
        cpuDrawCommands.size() * sizeof(vk::DrawIndexedIndirectCommand));
    indirectDrawBuffers[frameIndex]->unmap();
  }
  if (!cpuGroupFirstInstance.empty()) {
    groupFirstInstanceBuffers[frameIndex]->map();
    groupFirstInstanceBuffers[frameIndex]->write(cpuGroupFirstInstance.data(),
                                                 cpuGroupFirstInstance.size() *
                                                     sizeof(uint32_t));
    groupFirstInstanceBuffers[frameIndex]->unmap();
  }
  if (!cpuCommandGroupIndex.empty()) {
    commandGroupIndexBuffers[frameIndex]->map();
    commandGroupIndexBuffers[frameIndex]->write(cpuCommandGroupIndex.data(),
                                                cpuCommandGroupIndex.size() *
                                                    sizeof(uint32_t));
    commandGroupIndexBuffers[frameIndex]->unmap();
  }

  // -- instance descriptor set (set 2: compacted instances + wind UBO) --
  {
    instanceDescriptorSets[frameIndex] =
        context.vulkan.globalDescriptorAllocator->allocate(instanceSetLayout);

    vk::DescriptorBufferInfo instBufInfo{*compactedInstanceBuffers[frameIndex],
                                         0, VK_WHOLE_SIZE};
    vk::DescriptorBufferInfo windBufInfo{*windBuffers[frameIndex], 0,
                                         VK_WHOLE_SIZE};

    std::array<vk::WriteDescriptorSet, 2> writes{
        vk::WriteDescriptorSet{instanceDescriptorSets[frameIndex], 0, 0, 1,
                               vk::DescriptorType::eStorageBuffer, nullptr,
                               &instBufInfo},
        vk::WriteDescriptorSet{instanceDescriptorSets[frameIndex], 1, 0, 1,
                               vk::DescriptorType::eUniformBuffer, nullptr,
                               &windBufInfo}};
    context.vulkan.device.updateDescriptorSets(writes, nullptr);
  }

  // -- culling descriptor set (bindings 0-6, see createCullingPipeline) --
  {
    cullingDescriptorSets[frameIndex] =
        context.vulkan.globalDescriptorAllocator->allocate(cullingSetLayout);

    vk::DescriptorBufferInfo uboInfo{*cullingUboBuffers[frameIndex], 0,
                                     VK_WHOLE_SIZE};
    vk::DescriptorBufferInfo rawInstInfo{*instanceBuffers[frameIndex], 0,
                                         VK_WHOLE_SIZE};
    vk::DescriptorBufferInfo compactInstInfo{
        *compactedInstanceBuffers[frameIndex], 0, VK_WHOLE_SIZE};
    vk::DescriptorBufferInfo groupFirstInfo{
        *groupFirstInstanceBuffers[frameIndex], 0, VK_WHOLE_SIZE};
    vk::DescriptorBufferInfo groupVisibleInfo{
        *groupVisibleCountBuffers[frameIndex], 0, VK_WHOLE_SIZE};
    vk::DescriptorBufferInfo cmdGroupInfo{*commandGroupIndexBuffers[frameIndex],
                                          0, VK_WHOLE_SIZE};
    vk::DescriptorBufferInfo indirectInfo{*indirectDrawBuffers[frameIndex], 0,
                                          VK_WHOLE_SIZE};

    std::array<vk::WriteDescriptorSet, 7> writes{
        vk::WriteDescriptorSet{cullingDescriptorSets[frameIndex], 0, 0, 1,
                               vk::DescriptorType::eUniformBuffer, nullptr,
                               &uboInfo},
        vk::WriteDescriptorSet{cullingDescriptorSets[frameIndex], 1, 0, 1,
                               vk::DescriptorType::eStorageBuffer, nullptr,
                               &rawInstInfo},
        vk::WriteDescriptorSet{cullingDescriptorSets[frameIndex], 2, 0, 1,
                               vk::DescriptorType::eStorageBuffer, nullptr,
                               &compactInstInfo},
        vk::WriteDescriptorSet{cullingDescriptorSets[frameIndex], 3, 0, 1,
                               vk::DescriptorType::eStorageBuffer, nullptr,
                               &groupFirstInfo},
        vk::WriteDescriptorSet{cullingDescriptorSets[frameIndex], 4, 0, 1,
                               vk::DescriptorType::eStorageBuffer, nullptr,
                               &groupVisibleInfo},
        vk::WriteDescriptorSet{cullingDescriptorSets[frameIndex], 5, 0, 1,
                               vk::DescriptorType::eStorageBuffer, nullptr,
                               &cmdGroupInfo},
        vk::WriteDescriptorSet{cullingDescriptorSets[frameIndex], 6, 0, 1,
                               vk::DescriptorType::eStorageBuffer, nullptr,
                               &indirectInfo}};
    context.vulkan.device.updateDescriptorSets(writes, nullptr);
  }

  framesDirty[frameIndex] = false;
}

void TreeSys::updateBuffers() {
  int frameIndex = context.frameInfo.frameIndex;
  flushBuffers(frameIndex);

  WindUbo wind{};
  wind.time = context.time;
  wind.strength = windStrength;
  wind.dir = windDir;

  windBuffers[frameIndex]->map();
  windBuffers[frameIndex]->write(&wind, sizeof(WindUbo));
  windBuffers[frameIndex]->unmap();
}

void TreeSys::cull(vk::CommandBuffer cmd) {
  if (cpuInstanceData.empty())
    return;

  int frameIndex = context.frameInfo.frameIndex;
  flushBuffers(frameIndex);

  auto planes = camera::getFrustumPlanes(context.camera.projectionMatrix *
                                         context.camera.viewMatrix);
  CullingUbo ubo{};
  for (int i = 0; i < 6; i++)
    ubo.frustumPlanes[i] = planes[i];
  ubo.totalInstances = static_cast<uint32_t>(cpuInstanceData.size());
  ubo.totalCommands = static_cast<uint32_t>(cpuDrawCommands.size());

  cullingUboBuffers[frameIndex]->map();
  cullingUboBuffers[frameIndex]->write(&ubo, sizeof(CullingUbo));
  cullingUboBuffers[frameIndex]->unmap();

  dispatchCulling(cmd, frameIndex);
}

void TreeSys::cullForViewProj(vk::CommandBuffer cmd,
                              const glm::mat4 &viewProj) {
  if (cpuInstanceData.empty())
    return;

  int frameIndex = context.frameInfo.frameIndex;
  flushBuffers(frameIndex);

  auto planes = camera::getFrustumPlanes(viewProj);
  CullingUbo ubo{};
  for (int i = 0; i < 6; i++)
    ubo.frustumPlanes[i] = planes[i];
  ubo.totalInstances = static_cast<uint32_t>(cpuInstanceData.size());
  ubo.totalCommands = static_cast<uint32_t>(cpuDrawCommands.size());

  cullingUboBuffers[frameIndex]->map();
  cullingUboBuffers[frameIndex]->write(&ubo, sizeof(CullingUbo));
  cullingUboBuffers[frameIndex]->unmap();

  dispatchCulling(cmd, frameIndex);
}

// Identical barrier/dispatch sequence to EntitySys::dispatchCulling - see
// that function's comments for why each barrier is where it is (in short:
// the visible-count buffer must be re-zeroed on the GPU timeline immediately
// before every dispatch, since cull()/cullForViewProj() can each run more
// than once per frame sharing this same frame-indexed buffer).
void TreeSys::dispatchCulling(vk::CommandBuffer cmd, int frameIndex) {
  debug::beginLabel(context, cmd, "Tree Culling Dispatch",
                    {.3f, .7f, .3f, 1.f});

  vk::DeviceSize groupVisibleCountSize =
      groupVisibleCountBuffers[frameIndex]->getSize();
  if (groupVisibleCountSize > 0) {
    vk::BufferMemoryBarrier preFillBarrier{};
    preFillBarrier.srcAccessMask =
        vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
    preFillBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
    preFillBarrier.buffer = *groupVisibleCountBuffers[frameIndex];
    preFillBarrier.size = groupVisibleCountSize;

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eTransfer,
                        vk::DependencyFlags(), nullptr, preFillBarrier,
                        nullptr);

    cmd.fillBuffer(*groupVisibleCountBuffers[frameIndex], 0,
                   groupVisibleCountSize, 0u);

    vk::BufferMemoryBarrier postFillBarrier{};
    postFillBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    postFillBarrier.dstAccessMask =
        vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
    postFillBarrier.buffer = *groupVisibleCountBuffers[frameIndex];
    postFillBarrier.size = groupVisibleCountSize;

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eComputeShader,
                        vk::DependencyFlags(), nullptr, postFillBarrier,
                        nullptr);
  }

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
                         cullingPipeline->getLayout(), 0, 1,
                         &cullingDescriptorSets[frameIndex], 0, nullptr);

  cullingPipeline->bind(cmd);
  uint32_t instanceGroupCount =
      (static_cast<uint32_t>(cpuInstanceData.size()) + 63) / 64;
  if (instanceGroupCount > 0)
    cmd.dispatch(instanceGroupCount, 1, 1);

  vk::MemoryBarrier computeBarrier{vk::AccessFlagBits::eShaderWrite,
                                   vk::AccessFlagBits::eShaderRead};
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eComputeShader,
                      vk::DependencyFlags(), computeBarrier, nullptr, nullptr);

  finalizePipeline->bind(cmd);
  uint32_t commandGroupCount =
      (static_cast<uint32_t>(cpuCommandGroupIndex.size()) + 63) / 64;
  if (commandGroupCount > 0)
    cmd.dispatch(commandGroupCount, 1, 1);

  indirectBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
  indirectBarrier.dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eDrawIndirect,
                      vk::DependencyFlags(), nullptr, indirectBarrier, nullptr);

  debug::endLabel(context, cmd);
}

void TreeSys::render(vk::DescriptorSet globalSetOverride,
                     const glm::vec4 &clipPlane) {
  if (sceneBatches.empty())
    return;

  auto cmd = context.frameInfo.cmd;
  int frameIndex = context.frameInfo.frameIndex;

  flushBuffers(frameIndex);

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
                         pipeline->getLayout(), 2, 1,
                         &instanceDescriptorSets[frameIndex], 0, nullptr);

  for (const auto &batch : sceneBatches) {
    batch.scene->bind(context, cmd, *pipeline);

    vk::DescriptorSet texSet = batch.scene->sceneTextureSet;
    if (texSet) {
      cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                             pipeline->getLayout(), 1, 1, &texSet, 0, nullptr);
    }

    cmd.drawIndexedIndirect(
        *indirectDrawBuffers[frameIndex],
        batch.firstDrawCommandOffset * sizeof(vk::DrawIndexedIndirectCommand),
        batch.drawCommandCount, sizeof(vk::DrawIndexedIndirectCommand));
  }

  debug::endLabel(context, cmd);
}

} // namespace vkh
