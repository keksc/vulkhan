#include "entities.hpp"

#include "../../camera.hpp"
#include "../../debug.hpp"
#include "../../pipeline.hpp"
#include <vulkan/vulkan.hpp>

namespace vkh {

void EntitySys::createSetLayouts() {
  {
    std::vector<vk::DescriptorBindingFlags> bindingFlags = {
        vk::DescriptorBindingFlagBits::ePartiallyBound};

    vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
    bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
    bindingFlagsInfo.pBindingFlags = bindingFlags.data();

    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        vk::DescriptorSetLayoutBinding{
            0,                                         // binding
            vk::DescriptorType::eCombinedImageSampler, // descriptorType
            256, // descriptorCount (Max textures per scene array)
            vk::ShaderStageFlagBits::eFragment, // stageFlags
            nullptr                             // pImmutableSamplers
        }};

    texturesSetLayout = buildDescriptorSetLayout(
        context, bindings, vk::DescriptorSetLayoutCreateFlags{},
        &bindingFlagsInfo);
  }

  {
    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1,
                                       vk::ShaderStageFlagBits::eVertex},
        vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1,
                                       vk::ShaderStageFlagBits::eVertex}};

    instanceSetLayout = buildDescriptorSetLayout(context, bindings);
  }
}

void EntitySys::createPipeline() {
  std::vector<vk::DescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout, texturesSetLayout,
      instanceSetLayout};

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 0;
  pipelineLayoutInfo.pPushConstantRanges = nullptr;

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.attributeDescriptions = Vertex::getAttributeDescriptions();
  pipelineInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  pipelineInfo.vertpath = "shaders/entities.vert.spv";
  pipelineInfo.fragpath = "shaders/entities.frag.spv";

  pipelineInfo.multisampleInfo.rasterizationSamples =
      static_cast<vk::SampleCountFlagBits>(context.vulkan.msaaSamples);

  pipeline =
      std::make_unique<GraphicsPipeline>(context, pipelineInfo, "entities");
}

void EntitySys::createCullingPipeline() {
  // Bindings shared by both culling passes:
  //  0 CullingUbo                 1 InstanceBuffer (raw, CPU-authored)
  //  2 CompactedInstanceBuffer    3 GroupFirstInstanceBuffer
  //  4 GroupVisibleCountBuffer    5 CommandGroupIndexBuffer
  //  6 IndirectBuffer (draw commands)
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
      context, "shaders/culling.comp.spv", layoutInfo, "culling compute");

  uint32_t framesInFlight = context.vulkan.maxFramesInFlight;
  cullingDescriptorSets.resize(framesInFlight);
  cullingUboBuffers.resize(framesInFlight);
  for (uint32_t i = 0; i < framesInFlight; i++) {
    cullingUboBuffers[i] = std::make_unique<Buffer<CullingUbo>>(
        context, vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        1);
  }
}

void EntitySys::createFinalizePipeline() {
  // Reuses cullingSetLayout - same bindings, just a different shader that
  // only touches bindings 0, 4, 5, 6.
  vk::PipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &cullingSetLayout;

  finalizePipeline = std::make_unique<ComputePipeline>(
      context, "shaders/cullingFinalize.comp.spv", layoutInfo,
      "culling finalize compute");
}

EntitySys::EntitySys(EngineContext &context) : System(context) {
  createSetLayouts();
  createPipeline();
  createCullingPipeline();
  createFinalizePipeline();

  uint32_t framesInFlight = context.vulkan.maxFramesInFlight;
  instanceBuffers.resize(framesInFlight);
  indirectDrawBuffers.resize(framesInFlight);
  jointBuffers.resize(framesInFlight);
  compactedInstanceBuffers.resize(framesInFlight);
  groupFirstInstanceBuffers.resize(framesInFlight);
  commandGroupIndexBuffers.resize(framesInFlight);
  groupVisibleCountBuffers.resize(framesInFlight);
  instanceDescriptorSets.resize(framesInFlight, nullptr);
  framesDirty.resize(framesInFlight, false);
}

EntitySys::~EntitySys() {
  if (context.vulkan.device) {
    context.vulkan.device.destroyDescriptorSetLayout(texturesSetLayout,
                                                     nullptr);
    context.vulkan.device.destroyDescriptorSetLayout(instanceSetLayout,
                                                     nullptr);
    context.vulkan.device.destroyDescriptorSetLayout(cullingSetLayout, nullptr);
  }
}

void EntitySys::updateJoints() {
  if (entities.empty())
    return;

  int frameIndex = context.frameInfo.frameIndex;

  flushBuffers(frameIndex);

  if (jointBuffers.empty() || !jointBuffers[frameIndex])
    return;

  std::vector<glm::mat4> jointData;
  for (auto &entity : entities) {
    auto &mesh = entity.getMesh();
    if (mesh.skinIndex.has_value()) {
      auto &skin = entity.scene->skins[mesh.skinIndex.value()];
      for (size_t i = 0; i < skin.joints.size(); ++i) {
        glm::mat4 jointMatrix =
            entity.scene->nodes[skin.joints[i]].globalTransform *
            skin.inverseBindMatrices[i];
        jointData.push_back(jointMatrix);
      }
    }
  }

  if (!jointData.empty()) {
    jointBuffers[frameIndex]->map();
    jointBuffers[frameIndex]->write(jointData.data(),
                                    jointData.size() * sizeof(glm::mat4));
    jointBuffers[frameIndex]->unmap();
  }
}

void EntitySys::updateBuffers() {
  if (entities.empty()) {
    cpuInstanceData.clear();
    cpuDrawCommands.clear();
    sceneBatches.clear();
    cpuJointData.clear();
    cpuGroupFirstInstance.clear();
    cpuCommandGroupIndex.clear();
    cpuGroupVisibleCountZeros.clear();
    for (size_t i = 0; i < framesDirty.size(); ++i)
      framesDirty[i] = true;
    return;
  }

  if (structuralDirty) {
    cpuDrawCommands.clear();
    sceneBatches.clear();
    cpuGroupFirstInstance.clear();
    cpuCommandGroupIndex.clear();
  }
  cpuInstanceData.clear();
  cpuJointData.clear();

  uint32_t currentJointOffset = 0;
  uint32_t groupCounter = 0; // one entry per (scene,mesh) run below, every
                             // frame in the same order, so this index is
                             // stable even on frames that skip rebuilding
                             // cpuGroupFirstInstance/cpuDrawCommands

  for (size_t i = 0; i < entities.size();) {
    auto currentScene = entities[i].scene;
    uint32_t firstDrawOffset = static_cast<uint32_t>(cpuDrawCommands.size());
    uint32_t drawCount = 0;

    size_t j = i;
    while (j < entities.size() && entities[j].scene == currentScene) {
      size_t k = j;
      while (k < entities.size() && entities[k].scene == currentScene &&
             entities[k].meshIndex == entities[j].meshIndex) {
        k++;
      }

      uint32_t instanceCount = static_cast<uint32_t>(k - j);
      auto &entity = entities[j];
      auto &mesh = entity.getMesh();
      uint32_t meshInstanceStart =
          static_cast<uint32_t>(cpuInstanceData.size());
      uint32_t groupIdx = groupCounter++;

      if (structuralDirty) {
        cpuGroupFirstInstance.push_back(meshInstanceStart);

        for (const auto &primitive : mesh.primitives) {
          vk::DrawIndexedIndirectCommand cmd{};
          cmd.indexCount = primitive.indexCount;
          cmd.instanceCount = instanceCount;
          cmd.firstIndex = primitive.indexOffset;
          cmd.vertexOffset = 0;
          cmd.firstInstance = meshInstanceStart;

          cpuDrawCommands.push_back(cmd);
          cpuCommandGroupIndex.push_back(groupIdx);
          drawCount++;
        }
      }

      // Some glTF assets (e.g. ones with no materials defined at all) can
      // leave `materials` empty even though a primitive's materialIndex
      // defaults to 0 — guard both that and an empty primitive list rather
      // than indexing out of bounds with operator[].
      static const Scene<Vertex>::Material kDefaultMaterial{
          .baseColorFactor = glm::vec4(1.f),
          .roughnessFactor = 1.f,
          .metallicFactor = glm::vec4(0.f),
      };
      const Scene<Vertex>::Material *firstMat = &kDefaultMaterial;
      if (!mesh.primitives.empty()) {
        size_t matIdx = mesh.primitives[0].materialIndex;
        if (matIdx < currentScene->materials.size())
          firstMat = &currentScene->materials[matIdx];
      }
      int32_t texIdx = firstMat->baseColorTextureIndex.value_or(-1);
      int32_t mrTexIdx = firstMat->metallicRoughnessTextureIndex.value_or(-1);

      for (size_t inst = j; inst < k; ++inst) {
        AABB worldAABB = entities[inst].getWorldAABB();

        GPUInstanceData data;
        data.modelMatrix = entities[inst].transform.mat4() * mesh.transform;
        data.normalMatrix = glm::mat4(entities[inst].transform.normalMatrix());
        data.color = entities[inst].color * firstMat->baseColorFactor;
        data.aabbMin = worldAABB.min;
        data.aabbMax = worldAABB.max;
        data.textureIndex = texIdx;
        data.metallicRoughnessTextureIndex = mrTexIdx;
        data.roughnessFactor = firstMat->roughnessFactor;
        data.metallicFactor = firstMat->metallicFactor.x;
        data.jointOffset = mesh.skinIndex.has_value()
                               ? static_cast<int32_t>(currentJointOffset)
                               : -1;
        data.groupIndex = static_cast<int32_t>(groupIdx);

        cpuInstanceData.push_back(data);
      }

      j = k;
    }

    if (structuralDirty && drawCount > 0) {
      SceneBatch batch{};
      batch.scene = currentScene;
      batch.firstDrawCommandOffset = firstDrawOffset;
      batch.drawCommandCount = drawCount;
      sceneBatches.push_back(batch);
    }
    i = j;
  }

  for (auto &entity : entities) {
    auto &mesh = entity.getMesh();
    if (mesh.skinIndex.has_value()) {
      auto &skin = entity.scene->skins[mesh.skinIndex.value()];
      for (size_t i = 0; i < skin.joints.size(); ++i) {
        cpuJointData.push_back(
            entity.scene->nodes[skin.joints[i]].globalTransform *
            skin.inverseBindMatrices[i]);
      }
    }
  }

  int frameIndex = context.frameInfo.frameIndex;
  auto planes = camera::getFrustumPlanes(context.camera.projectionMatrix *
                                         context.camera.viewMatrix);
  CullingUbo ubo{};
  for (int i = 0; i < 6; i++)
    ubo.frustumPlanes[i] = planes[i];
  ubo.totalInstances = static_cast<uint32_t>(cpuInstanceData.size());
  ubo.totalCommands = static_cast<uint32_t>(cpuDrawCommands.size());

  if (cpuGroupVisibleCountZeros.size() != cpuGroupFirstInstance.size()) {
    cpuGroupVisibleCountZeros.assign(cpuGroupFirstInstance.size(), 0u);
  }

  cullingUboBuffers[frameIndex]->map();
  cullingUboBuffers[frameIndex]->write(&ubo, sizeof(CullingUbo));
  cullingUboBuffers[frameIndex]->unmap();

  structuralDirty = false;
  for (size_t i = 0; i < framesDirty.size(); ++i)
    framesDirty[i] = true;
}

void EntitySys::flushBuffers(int frameIndex) {
  if (!framesDirty[frameIndex])
    return;

  vk::DeviceSize instanceBufferSize =
      cpuInstanceData.size() * sizeof(GPUInstanceData);
  vk::DeviceSize jointBufferSize = std::max<vk::DeviceSize>(
      cpuJointData.size() * sizeof(glm::mat4), sizeof(glm::mat4));
  vk::DeviceSize cmdBufferSize =
      cpuDrawCommands.size() * sizeof(vk::DrawIndexedIndirectCommand);
  uint32_t groupCount = static_cast<uint32_t>(cpuGroupFirstInstance.size());
  uint32_t commandCount = static_cast<uint32_t>(cpuCommandGroupIndex.size());

  bool updateGraphicsDescriptor = false;
  bool updateCullingDescriptor = false;

  if (!instanceBuffers[frameIndex] ||
      instanceBuffers[frameIndex]->getSize() < instanceBufferSize) {
    instanceBuffers[frameIndex] = std::make_unique<Buffer<GPUInstanceData>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        std::max<uint32_t>(cpuInstanceData.size(), 1));
    updateCullingDescriptor = true;
  }

  // Device-local: only ever written by the culling compute pass, read by
  // the vertex shader. Sized to match the raw instance buffer.
  if (!compactedInstanceBuffers[frameIndex] ||
      compactedInstanceBuffers[frameIndex]->getSize() < instanceBufferSize) {
    compactedInstanceBuffers[frameIndex] =
        std::make_unique<Buffer<GPUInstanceData>>(
            context, vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            std::max<uint32_t>(cpuInstanceData.size(), 1));
    updateGraphicsDescriptor = true;
    updateCullingDescriptor = true;
  }

  if (!groupFirstInstanceBuffers[frameIndex] ||
      groupFirstInstanceBuffers[frameIndex]->getSize() <
          groupCount * sizeof(uint32_t)) {
    groupFirstInstanceBuffers[frameIndex] = std::make_unique<Buffer<uint32_t>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        std::max<uint32_t>(groupCount, 1));
    updateCullingDescriptor = true;
  }

  if (!groupVisibleCountBuffers[frameIndex] ||
      groupVisibleCountBuffers[frameIndex]->getSize() <
          groupCount * sizeof(uint32_t)) {
    groupVisibleCountBuffers[frameIndex] = std::make_unique<Buffer<uint32_t>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        std::max<uint32_t>(groupCount, 1));
    updateCullingDescriptor = true;
  }

  if (!commandGroupIndexBuffers[frameIndex] ||
      commandGroupIndexBuffers[frameIndex]->getSize() <
          commandCount * sizeof(uint32_t)) {
    commandGroupIndexBuffers[frameIndex] = std::make_unique<Buffer<uint32_t>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        std::max<uint32_t>(commandCount, 1));
    updateCullingDescriptor = true;
  }

  if (!indirectDrawBuffers[frameIndex] ||
      indirectDrawBuffers[frameIndex]->getSize() < cmdBufferSize) {
    indirectDrawBuffers[frameIndex] =
        std::make_unique<Buffer<vk::DrawIndexedIndirectCommand>>(
            context,
            vk::BufferUsageFlagBits::eIndirectBuffer |
                vk::BufferUsageFlagBits::eTransferDst |
                vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent,
            std::max<uint32_t>(cpuDrawCommands.size(), 1));
    updateCullingDescriptor = true;
  }

  if (!jointBuffers[frameIndex] ||
      jointBuffers[frameIndex]->getSize() < jointBufferSize) {
    jointBuffers[frameIndex] = std::make_unique<Buffer<glm::mat4>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        std::max<uint32_t>(cpuJointData.size(), 1));
    updateGraphicsDescriptor = true;
  }

  // Graphics-side set: vertex shader now reads the *compacted* instance
  // buffer (culling-pass output), not the raw CPU-authored one.
  if (updateGraphicsDescriptor || !instanceDescriptorSets[frameIndex]) {
    if (!instanceDescriptorSets[frameIndex]) {
      instanceDescriptorSets[frameIndex] =
          context.vulkan.globalDescriptorAllocator->allocate(instanceSetLayout);
    }
    DescriptorWriter writer(context);
    vk::DescriptorBufferInfo bInfo =
        compactedInstanceBuffers[frameIndex]->descriptorInfo();
    writer.writeBuffer(0, bInfo, vk::DescriptorType::eStorageBuffer);

    vk::DescriptorBufferInfo jInfo = jointBuffers[frameIndex]->descriptorInfo();
    writer.writeBuffer(1, jInfo, vk::DescriptorType::eStorageBuffer);

    writer.updateSet(instanceDescriptorSets[frameIndex]);
  }

  if (updateCullingDescriptor || !cullingDescriptorSets[frameIndex]) {
    if (!cullingDescriptorSets[frameIndex]) {
      cullingDescriptorSets[frameIndex] =
          context.vulkan.globalDescriptorAllocator->allocate(cullingSetLayout);
    }
    DescriptorWriter cWriter(context);
    vk::DescriptorBufferInfo uInfo =
        cullingUboBuffers[frameIndex]->descriptorInfo();
    vk::DescriptorBufferInfo rawInfo =
        instanceBuffers[frameIndex]->descriptorInfo();
    vk::DescriptorBufferInfo compactedInfo =
        compactedInstanceBuffers[frameIndex]->descriptorInfo();
    vk::DescriptorBufferInfo groupFirstInfo =
        groupFirstInstanceBuffers[frameIndex]->descriptorInfo();
    vk::DescriptorBufferInfo groupCountInfo =
        groupVisibleCountBuffers[frameIndex]->descriptorInfo();
    vk::DescriptorBufferInfo cmdGroupInfo =
        commandGroupIndexBuffers[frameIndex]->descriptorInfo();
    vk::DescriptorBufferInfo indirectInfo =
        indirectDrawBuffers[frameIndex]->descriptorInfo();

    cWriter.writeBuffer(0, uInfo, vk::DescriptorType::eUniformBuffer);
    cWriter.writeBuffer(1, rawInfo, vk::DescriptorType::eStorageBuffer);
    cWriter.writeBuffer(2, compactedInfo, vk::DescriptorType::eStorageBuffer);
    cWriter.writeBuffer(3, groupFirstInfo, vk::DescriptorType::eStorageBuffer);
    cWriter.writeBuffer(4, groupCountInfo, vk::DescriptorType::eStorageBuffer);
    cWriter.writeBuffer(5, cmdGroupInfo, vk::DescriptorType::eStorageBuffer);
    cWriter.writeBuffer(6, indirectInfo, vk::DescriptorType::eStorageBuffer);
    cWriter.updateSet(cullingDescriptorSets[frameIndex]);
  }

  if (instanceBufferSize > 0) {
    instanceBuffers[frameIndex]->map();
    instanceBuffers[frameIndex]->write(cpuInstanceData.data(),
                                       instanceBufferSize);
    instanceBuffers[frameIndex]->unmap();
  }

  if (cmdBufferSize > 0) {
    indirectDrawBuffers[frameIndex]->map();
    indirectDrawBuffers[frameIndex]->write(cpuDrawCommands.data(),
                                           cmdBufferSize);
    indirectDrawBuffers[frameIndex]->unmap();
  }

  if (groupCount > 0) {
    groupFirstInstanceBuffers[frameIndex]->map();
    groupFirstInstanceBuffers[frameIndex]->write(cpuGroupFirstInstance.data(),
                                                 groupCount * sizeof(uint32_t));
    groupFirstInstanceBuffers[frameIndex]->unmap();

    // Reset every group's visible count to 0 so this frame's culling pass
    // can atomically accumulate into it from scratch.
    groupVisibleCountBuffers[frameIndex]->map();
    groupVisibleCountBuffers[frameIndex]->write(
        cpuGroupVisibleCountZeros.data(), groupCount * sizeof(uint32_t));
    groupVisibleCountBuffers[frameIndex]->unmap();
  }

  if (commandCount > 0) {
    commandGroupIndexBuffers[frameIndex]->map();
    commandGroupIndexBuffers[frameIndex]->write(
        cpuCommandGroupIndex.data(), commandCount * sizeof(uint32_t));
    commandGroupIndexBuffers[frameIndex]->unmap();
  }

  if (jointBufferSize > 0 && !cpuJointData.empty()) {
    jointBuffers[frameIndex]->map();
    jointBuffers[frameIndex]->write(cpuJointData.data(),
                                    cpuJointData.size() * sizeof(glm::mat4));
    jointBuffers[frameIndex]->unmap();
  }

  framesDirty[frameIndex] = false;
}

void EntitySys::cull(vk::CommandBuffer cmd) {
  if (sceneBatches.empty())
    return;

  int frameIndex = context.frameInfo.frameIndex;
  flushBuffers(frameIndex);

  debug::beginLabel(context, cmd, "Culling Dispatch", {.3f, .8f, .3f, 1.f});

  // Pass 2 below overwrites every command's instanceCount outright (not an
  // accumulation), so the CPU-uploaded value doesn't matter - but we still
  // need last frame's indirect-command reads and this frame's host upload
  // to complete before pass 2's shader write can safely land.
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

  // Pass 1: frustum-cull instances, compact survivors, accumulate each
  // group's visible count via atomics.
  cullingPipeline->bind(cmd);
  uint32_t instanceGroupCount =
      (static_cast<uint32_t>(cpuInstanceData.size()) + 63) / 64;
  if (instanceGroupCount > 0) {
    cmd.dispatch(instanceGroupCount, 1, 1);
  }

  // Pass 2 reads what pass 1 wrote (compacted buffer + visible counts).
  vk::MemoryBarrier computeBarrier{vk::AccessFlagBits::eShaderWrite,
                                   vk::AccessFlagBits::eShaderRead};
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eComputeShader,
                      vk::DependencyFlags(), computeBarrier, nullptr, nullptr);

  // Pass 2: broadcast each group's final visible count into its indirect
  // draw command(s).
  finalizePipeline->bind(cmd);
  uint32_t commandGroupCount =
      (static_cast<uint32_t>(cpuCommandGroupIndex.size()) + 63) / 64;
  if (commandGroupCount > 0) {
    cmd.dispatch(commandGroupCount, 1, 1);
  }

  indirectBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
  indirectBarrier.dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eDrawIndirect,
                      vk::DependencyFlags(), nullptr, indirectBarrier, nullptr);

  debug::endLabel(context, cmd);
}

void EntitySys::render() {
  if (sceneBatches.empty())
    return;

  auto cmd = context.frameInfo.cmd;
  int frameIndex = context.frameInfo.frameIndex;

  flushBuffers(frameIndex);

  debug::beginLabel(context, cmd, "Indirect Entities", {.7f, .3f, 1.f, 1.f});

  pipeline->bind(cmd);

  cmd.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, pipeline->getLayout(), 0, 1,
      &context.vulkan.globalDescriptorSets[frameIndex], 0, nullptr);

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

EntitySys::Entity *EntitySys::pickEntity(const Ray &ray, float &distance,
                                         float maxDistance) {
  Entity *bestEntity = nullptr;
  float minDistance = maxDistance;

  for (auto &entity : entities) {
    if (auto d = ray.intersects(entity.getWorldAABB())) {
      if (*d < minDistance) {
        minDistance = *d;
        bestEntity = &entity;
      }
    }
  }

  if (bestEntity) {
    distance = minDistance;
  }
  return bestEntity;
}

EntitySys::Entity *EntitySys::getPointingAt(float maxDistance) {
  Ray ray = camera::getPickingRay(context, glm::vec2(0.0f));
  float distance;
  return pickEntity(ray, distance, maxDistance);
}

} // namespace vkh
