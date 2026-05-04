#include "entities.hpp"

#include "../../camera.hpp"
#include "../../debug.hpp"
#include "../../pipeline.hpp"
#include "../../swapChain.hpp"
#include <vulkan/vulkan_core.h>

namespace vkh {

void EntitySys::createSetLayouts() {
  {
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{
        .sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    VkDescriptorBindingFlags bindingFlags[] = {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT};
    bindingFlagsInfo.pBindingFlags = bindingFlags;
    bindingFlagsInfo.bindingCount = 1;

    texturesSetLayout = buildDescriptorSetLayout(
        context,
        {
            VkDescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 256, // Max textures per scene array
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
        },
        0, &bindingFlagsInfo);
  }

  {
    instanceSetLayout = buildDescriptorSetLayout(
        context, {
                     VkDescriptorSetLayoutBinding{
                         .binding = 0,
                         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                         .descriptorCount = 1,
                         .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                     },
                     VkDescriptorSetLayoutBinding{
                         .binding = 1,
                         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                         .descriptorCount = 1,
                         .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                     },
                 });
  }
}

void EntitySys::createPipeline() {
  std::vector<VkDescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout, texturesSetLayout,
      instanceSetLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
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
      context.vulkan.msaaSamples;

  pipeline =
      std::make_unique<GraphicsPipeline>(context, pipelineInfo, "entities");
}

void EntitySys::createCullingPipeline() {
  cullingSetLayout = buildDescriptorSetLayout(
      context, {
                   VkDescriptorSetLayoutBinding{
                       .binding = 0,
                       .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                       .descriptorCount = 1,
                       .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                   },
                   VkDescriptorSetLayoutBinding{
                       .binding = 1,
                       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                       .descriptorCount = 1,
                       .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                   },
                   VkDescriptorSetLayoutBinding{
                       .binding = 2,
                       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                       .descriptorCount = 1,
                       .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                   },
               });

  VkPipelineLayoutCreateInfo layoutInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &cullingSetLayout};

  cullingPipeline = std::make_unique<ComputePipeline>(
      context, "shaders/culling.comp.spv", layoutInfo, "culling compute");

  uint32_t framesInFlight = context.vulkan.maxFramesInFlight;
  cullingDescriptorSets.resize(framesInFlight);
  cullingUboBuffers.resize(framesInFlight);
  for (uint32_t i = 0; i < framesInFlight; i++) {
    cullingUboBuffers[i] = std::make_unique<Buffer<CullingUbo>>(
        context, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
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
  instanceDescriptorSets.resize(framesInFlight, VK_NULL_HANDLE);
  framesDirty.resize(framesInFlight, false);
}

EntitySys::~EntitySys() {
  vkDestroyDescriptorSetLayout(context.vulkan.device, texturesSetLayout,
                               nullptr);
  vkDestroyDescriptorSetLayout(context.vulkan.device, instanceSetLayout,
                               nullptr);
  vkDestroyDescriptorSetLayout(context.vulkan.device, cullingSetLayout, nullptr);
}

void EntitySys::updateJoints() {
  if (entities.empty())
    return;

  int frameIndex = context.frameInfo.frameIndex;

  // Ensure buffers are allocated and up-to-date for this frame
  // before we try to map them dynamically.
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
    for (size_t i = 0; i < framesDirty.size(); ++i) framesDirty[i] = true;
    return;
  }

  // With compute culling, we only rebuild structure if structuralDirty is true.
  // Data updates (transforms, colors) happen every frame but can be optimized later.
  // However, frustum planes change every frame, so we must update the UBO.
  
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
      uint32_t meshInstanceStart = static_cast<uint32_t>(cpuInstanceData.size());

      if (structuralDirty) {
          for (const auto &primitive : mesh.primitives) {
            VkDrawIndexedIndirectCommand cmd{};
            cmd.indexCount = primitive.indexCount;
            cmd.instanceCount = instanceCount; // We will use isVisible in vertex shader instead of instanceCount for now
            cmd.firstIndex = primitive.indexOffset;
            cmd.vertexOffset = 0;
            cmd.firstInstance = meshInstanceStart;

            cpuDrawCommands.push_back(cmd);
            drawCount++;
          }
      }

      auto &firstMat = currentScene->materials[mesh.primitives[0].materialIndex];
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
        data.jointOffset = mesh.skinIndex.has_value() ? static_cast<int32_t>(currentJointOffset) : -1;
        data.isVisible = 1; // Default to visible, compute shader will update

        cpuInstanceData.push_back(data);
      }
      
      if (mesh.skinIndex.has_value()) {
          // Simplification for joint offsets in this demo
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

  // Update joint data...
  for (auto &entity : entities) {
    auto &mesh = entity.getMesh();
    if (mesh.skinIndex.has_value()) {
      auto &skin = entity.scene->skins[mesh.skinIndex.value()];
      for (size_t i = 0; i < skin.joints.size(); ++i) {
        cpuJointData.push_back(entity.scene->nodes[skin.joints[i]].globalTransform * skin.inverseBindMatrices[i]);
      }
    }
  }

  // Update Culling UBO
  int frameIndex = context.frameInfo.frameIndex;
  auto planes = camera::getFrustumPlanes(context.camera.projectionMatrix * context.camera.viewMatrix);
  CullingUbo ubo{};
  for(int i=0; i<6; i++) ubo.frustumPlanes[i] = planes[i];
  ubo.totalInstances = static_cast<uint32_t>(cpuInstanceData.size());
  
  cullingUboBuffers[frameIndex]->map();
  cullingUboBuffers[frameIndex]->write(&ubo, sizeof(CullingUbo));
  cullingUboBuffers[frameIndex]->unmap();

  structuralDirty = false;
  for (size_t i = 0; i < framesDirty.size(); ++i) framesDirty[i] = true;
}

void EntitySys::flushBuffers(int frameIndex) {
  if (!framesDirty[frameIndex])
    return;

  VkDeviceSize instanceBufferSize =
      cpuInstanceData.size() * sizeof(GPUInstanceData);
  VkDeviceSize jointBufferSize = std::max<VkDeviceSize>(
      cpuJointData.size() * sizeof(glm::mat4), sizeof(glm::mat4));
  VkDeviceSize cmdBufferSize =
      cpuDrawCommands.size() * sizeof(VkDrawIndexedIndirectCommand);

  bool updateDescriptor = false;

  if (!instanceBuffers[frameIndex] ||
      instanceBuffers[frameIndex]->getSize() < instanceBufferSize) {
    instanceBuffers[frameIndex] = std::make_unique<Buffer<GPUInstanceData>>(
        context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        std::max<uint32_t>(cpuInstanceData.size(), 1));
    updateDescriptor = true;
  }

  if (!jointBuffers[frameIndex] ||
      jointBuffers[frameIndex]->getSize() < jointBufferSize) {
    jointBuffers[frameIndex] = std::make_unique<Buffer<glm::mat4>>(
        context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        std::max<uint32_t>(cpuJointData.size(), 1));
    updateDescriptor = true;
  }

  if (!indirectDrawBuffers[frameIndex] ||
      indirectDrawBuffers[frameIndex]->getSize() < cmdBufferSize) {
    indirectDrawBuffers[frameIndex] =
        std::make_unique<Buffer<VkDrawIndexedIndirectCommand>>(
            context,
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            std::max<uint32_t>(cpuDrawCommands.size(), 1));
  }

  if (updateDescriptor ||
      instanceDescriptorSets[frameIndex] == VK_NULL_HANDLE) {
    if (instanceDescriptorSets[frameIndex] == VK_NULL_HANDLE) {
      instanceDescriptorSets[frameIndex] =
          context.vulkan.globalDescriptorAllocator->allocate(instanceSetLayout);
    }
    DescriptorWriter writer(context);
    VkDescriptorBufferInfo bInfo =
        instanceBuffers[frameIndex]->descriptorInfo();
    writer.writeBuffer(0, bInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

    VkDescriptorBufferInfo jInfo = jointBuffers[frameIndex]->descriptorInfo();
    writer.writeBuffer(1, jInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

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

void EntitySys::cull(VkCommandBuffer cmd) {
  if (sceneBatches.empty())
    return;

  int frameIndex = context.frameInfo.frameIndex;

  debug::beginLabel(context, cmd, "Culling Dispatch", {.3f, .8f, .3f, 1.f});
  
  // Update Culling Descriptor Set
  if (cullingDescriptorSets[frameIndex] == VK_NULL_HANDLE) {
      cullingDescriptorSets[frameIndex] = context.vulkan.globalDescriptorAllocator->allocate(cullingSetLayout);
  }
  
  DescriptorWriter cWriter(context);
  VkDescriptorBufferInfo uInfo = cullingUboBuffers[frameIndex]->descriptorInfo();
  VkDescriptorBufferInfo iInfo = instanceBuffers[frameIndex]->descriptorInfo();
  VkDescriptorBufferInfo dInfo = indirectDrawBuffers[frameIndex]->descriptorInfo();
  
  cWriter.writeBuffer(0, uInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
  cWriter.writeBuffer(1, iInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  cWriter.writeBuffer(2, dInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  cWriter.updateSet(cullingDescriptorSets[frameIndex]);

  // Transition indirect buffer for compute write
  VkBufferMemoryBarrier indirectBarrier{};
  indirectBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  indirectBarrier.srcAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
  indirectBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
  indirectBarrier.buffer = *indirectDrawBuffers[frameIndex];
  indirectBarrier.size = indirectDrawBuffers[frameIndex]->getSize();

  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                       0, 0, nullptr, 1, &indirectBarrier, 0, nullptr);

  cullingPipeline->bind(cmd);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullingPipeline->getLayout(), 0, 1,
                          &cullingDescriptorSets[frameIndex], 0, nullptr);
  
  uint32_t groupCount = (static_cast<uint32_t>(cpuInstanceData.size()) + 63) / 64;
  if (groupCount > 0) {
    vkCmdDispatch(cmd, groupCount, 1, 1);
  }

  // Transition back to indirect read
  indirectBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  indirectBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
  
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 
                       0, 0, nullptr, 1, &indirectBarrier, 0, nullptr);

  debug::endLabel(context, cmd);
}

void EntitySys::render() {
  if (sceneBatches.empty())
    return;

  auto cmd = context.frameInfo.cmd;
  int frameIndex = context.frameInfo.frameIndex;

  // Ensure buffers are flushed just in case
  flushBuffers(frameIndex);

  debug::beginLabel(context, cmd, "Indirect Entities", {.7f, .3f, 1.f, 1.f});

  pipeline->bind(cmd);

  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, 1,
                          &context.vulkan.globalDescriptorSets[frameIndex], 0,
                          nullptr);

  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 2, 1,
                          &instanceDescriptorSets[frameIndex], 0, nullptr);

  for (const auto &batch : sceneBatches) {
    batch.scene->bind(context, cmd, *pipeline);

    VkDescriptorSet texSet = batch.scene->sceneTextureSet;
    if (texSet != VK_NULL_HANDLE) {
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 1,
                              1, &texSet, 0, nullptr);
    }

    vkCmdDrawIndexedIndirect(
        cmd, *(indirectDrawBuffers[frameIndex]),
        batch.firstDrawCommandOffset * sizeof(VkDrawIndexedIndirectCommand),
        batch.drawCommandCount, sizeof(VkDrawIndexedIndirectCommand));
  }

  debug::endLabel(context, cmd);
}

bool EntitySys::checkCollision(const AABB &aabb) const {
  for (const auto &entity : entities) {
    if (entity.getWorldAABB().intersects(aabb)) {
      return true;
    }
  }
  return false;
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
