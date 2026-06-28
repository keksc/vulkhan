#include "entities.hpp"

#include "../../camera.hpp"
#include "../../debug.hpp"
#include "../../pipeline.hpp"
#include "../../swapChain.hpp"
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
  pipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  pipelineInfo.attributeDescriptions = Vertex::getAttributeDescriptions();
  pipelineInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  pipelineInfo.vertpath = "shaders/entities.vert.spv";
  pipelineInfo.fragpath = "shaders/entities.frag.spv";

  pipelineInfo.subpass = 0;
  pipelineInfo.multisampleInfo.rasterizationSamples =
      static_cast<vk::SampleCountFlagBits>(context.vulkan.msaaSamples);

  pipeline =
      std::make_unique<GraphicsPipeline>(context, pipelineInfo, "entities");
}

void EntitySys::createCullingPipeline() {
  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eUniformBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute},
      vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eStorageBuffer, 1,
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

EntitySys::EntitySys(EngineContext &context) : System(context) {
  createSetLayouts();
  createPipeline();
  createCullingPipeline();

  uint32_t framesInFlight = context.vulkan.maxFramesInFlight;
  instanceBuffers.resize(framesInFlight);
  indirectDrawBuffers.resize(framesInFlight);
  jointBuffers.resize(framesInFlight);
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
    for (size_t i = 0; i < framesDirty.size(); ++i)
      framesDirty[i] = true;
    return;
  }

  if (structuralDirty) {
    cpuDrawCommands.clear();
    sceneBatches.clear();
  }
  cpuInstanceData.clear();
  cpuJointData.clear();

  uint32_t currentJointOffset = 0;

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

      if (structuralDirty) {
        for (const auto &primitive : mesh.primitives) {
          vk::DrawIndexedIndirectCommand cmd{};
          cmd.indexCount = primitive.indexCount;
          cmd.instanceCount = instanceCount;
          cmd.firstIndex = primitive.indexOffset;
          cmd.vertexOffset = 0;
          cmd.firstInstance = meshInstanceStart;

          cpuDrawCommands.push_back(cmd);
          drawCount++;
        }
      }

      auto &firstMat =
          currentScene->materials[mesh.primitives[0].materialIndex];
      int32_t texIdx = firstMat.baseColorTextureIndex.value_or(-1);
      int32_t mrTexIdx = firstMat.metallicRoughnessTextureIndex.value_or(-1);

      for (size_t inst = j; inst < k; ++inst) {
        AABB worldAABB = entities[inst].getWorldAABB();

        GPUInstanceData data;
        data.modelMatrix = entities[inst].transform.mat4() * mesh.transform;
        data.normalMatrix = glm::mat4(entities[inst].transform.normalMatrix());
        data.color = entities[inst].color * firstMat.baseColorFactor;
        data.aabbMin = worldAABB.min;
        data.aabbMax = worldAABB.max;
        data.textureIndex = texIdx;
        data.metallicRoughnessTextureIndex = mrTexIdx;
        data.roughnessFactor = firstMat.roughnessFactor;
        data.metallicFactor = firstMat.metallicFactor.x;
        data.jointOffset = mesh.skinIndex.has_value()
                               ? static_cast<int32_t>(currentJointOffset)
                               : -1;
        data.isVisible = 1;

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

  bool updateDescriptor = false;

  if (!instanceBuffers[frameIndex] ||
      instanceBuffers[frameIndex]->getSize() < instanceBufferSize) {
    instanceBuffers[frameIndex] = std::make_unique<Buffer<GPUInstanceData>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        std::max<uint32_t>(cpuInstanceData.size(), 1));
    updateDescriptor = true;
  }

  if (!jointBuffers[frameIndex] ||
      jointBuffers[frameIndex]->getSize() < jointBufferSize) {
    jointBuffers[frameIndex] = std::make_unique<Buffer<glm::mat4>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        std::max<uint32_t>(cpuJointData.size(), 1));
    updateDescriptor = true;
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
  }

  if (updateDescriptor || !instanceDescriptorSets[frameIndex]) {
    if (!instanceDescriptorSets[frameIndex]) {
      instanceDescriptorSets[frameIndex] =
          context.vulkan.globalDescriptorAllocator->allocate(instanceSetLayout);
    }
    DescriptorWriter writer(context);
    vk::DescriptorBufferInfo bInfo =
        instanceBuffers[frameIndex]->descriptorInfo();
    writer.writeBuffer(0, bInfo, vk::DescriptorType::eStorageBuffer);

    vk::DescriptorBufferInfo jInfo = jointBuffers[frameIndex]->descriptorInfo();
    writer.writeBuffer(1, jInfo, vk::DescriptorType::eStorageBuffer);

    writer.updateSet(instanceDescriptorSets[frameIndex]);
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

  debug::beginLabel(context, cmd, "Culling Dispatch", {.3f, .8f, .3f, 1.f});

  if (!cullingDescriptorSets[frameIndex]) {
    cullingDescriptorSets[frameIndex] =
        context.vulkan.globalDescriptorAllocator->allocate(cullingSetLayout);
  }

  DescriptorWriter cWriter(context);
  vk::DescriptorBufferInfo uInfo =
      cullingUboBuffers[frameIndex]->descriptorInfo();
  vk::DescriptorBufferInfo iInfo =
      instanceBuffers[frameIndex]->descriptorInfo();
  vk::DescriptorBufferInfo dInfo =
      indirectDrawBuffers[frameIndex]->descriptorInfo();

  cWriter.writeBuffer(0, uInfo, vk::DescriptorType::eUniformBuffer);
  cWriter.writeBuffer(1, iInfo, vk::DescriptorType::eStorageBuffer);
  cWriter.writeBuffer(2, dInfo, vk::DescriptorType::eStorageBuffer);
  cWriter.updateSet(cullingDescriptorSets[frameIndex]);

  vk::BufferMemoryBarrier indirectBarrier{};
  indirectBarrier.srcAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
  indirectBarrier.dstAccessMask =
      vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead;
  indirectBarrier.buffer = *indirectDrawBuffers[frameIndex];
  indirectBarrier.size = indirectDrawBuffers[frameIndex]->getSize();

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eDrawIndirect,
                      vk::PipelineStageFlagBits::eComputeShader,
                      vk::DependencyFlags(), nullptr, indirectBarrier, nullptr);

  cullingPipeline->bind(cmd);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                         cullingPipeline->getLayout(), 0, 1,
                         &cullingDescriptorSets[frameIndex], 0, nullptr);

  uint32_t groupCount =
      (static_cast<uint32_t>(cpuInstanceData.size()) + 63) / 64;
  if (groupCount > 0) {
    cmd.dispatch(groupCount, 1, 1);
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
