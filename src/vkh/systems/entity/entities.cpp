#include "entities.hpp"
#include <algorithm>
#include <memory>
#include <print>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vulkan/vulkan_core.h>

#include "../../descriptors.hpp"
#include "../../pipeline.hpp"
#include "../../scene.hpp"
#include "../../swapChain.hpp"

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

    textureSetLayout = buildDescriptorSetLayout(
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

EntitySys::EntitySys(EngineContext &context) : System(context) {
  createSetLayouts();

  dummyTextureSet =
      context.vulkan.globalDescriptorAllocator->allocate(textureSetLayout);
  DescriptorWriter writer(context);
  VkDescriptorImageInfo imageInfo{.sampler = VK_NULL_HANDLE,
                                  .imageView = VK_NULL_HANDLE,
                                  .imageLayout =
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  writer.writeImage(0, imageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
  writer.updateSet(dummyTextureSet);

  createPipeline();

  uint32_t framesInFlight = context.vulkan.maxFramesInFlight;
  instanceBuffers.resize(framesInFlight);
  indirectDrawBuffers.resize(framesInFlight);
  jointBuffers.resize(framesInFlight);
  instanceDescriptorSets.resize(framesInFlight, VK_NULL_HANDLE);
  framesDirty.resize(framesInFlight, false); // Initialize dirty flags
}

void EntitySys::createPipeline() {
  std::vector<VkDescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout, textureSetLayout,
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

EntitySys::~EntitySys() {
  vkDestroyDescriptorSetLayout(context.vulkan.device, textureSetLayout,
                               nullptr);
  vkDestroyDescriptorSetLayout(context.vulkan.device, instanceSetLayout,
                               nullptr);
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
  sceneBatches.clear();
  cpuInstanceData.clear();
  cpuDrawCommands.clear();
  cpuJointData.clear();

  if (entities.empty())
    return;

  size_t totalInstancesEst = 0;
  for (auto &e : entities)
    totalInstancesEst += e.getMesh().primitives.size();

  cpuInstanceData.reserve(totalInstancesEst);
  cpuDrawCommands.reserve(totalInstancesEst);

  uint32_t currentInstanceIndex = 0;
  uint32_t currentJointOffset = 0;

  for (size_t i = 0; i < entities.size();) {
    auto currentScene = entities[i].scene;
    uint32_t firstDrawOffset = static_cast<uint32_t>(cpuDrawCommands.size());
    uint32_t drawCount = 0;

    size_t j = i;
    while (j < entities.size() &&
           entities[j].scene == currentScene) {
      size_t k = j;
      while (k < entities.size() &&
             entities[k].scene == currentScene &&
             entities[k].meshIndex == entities[j].meshIndex) {
        k++;
      }

      uint32_t instanceCount = static_cast<uint32_t>(k - j);
      auto &entity = entities[j];
      auto &mesh = entity.getMesh();

      for (const auto &primitive : mesh.primitives) {
        VkDrawIndexedIndirectCommand cmd{};
        cmd.indexCount = primitive.indexCount;
        cmd.instanceCount = instanceCount;
        cmd.firstIndex = primitive.indexOffset;
        cmd.vertexOffset = 0;
        cmd.firstInstance = currentInstanceIndex;

        cpuDrawCommands.push_back(cmd);
        drawCount++;

        auto &mat = currentScene->materials[primitive.materialIndex];
        int32_t texIdx =
            mat.baseColorTextureIndex.has_value()
                ? static_cast<int32_t>(mat.baseColorTextureIndex.value())
                : -1;

        for (size_t inst = j; inst < k; ++inst) {
          GPUInstanceData data;
          data.modelMatrix =
              entities[inst].transform.mat4() * mesh.transform;
          data.normalMatrix =
              glm::mat4(entities[inst].transform.normalMatrix());
          data.color = mat.baseColorFactor;
          data.textureIndex = texIdx;
          if (mesh.skinIndex.has_value()) {
            data.jointOffset = currentJointOffset;
            currentJointOffset +=
                currentScene->skins[mesh.skinIndex.value()].joints.size();
          } else {
            data.jointOffset = -1;
          }
          cpuInstanceData.push_back(data);
        }
        currentInstanceIndex += instanceCount;
      }
      j = k;
    }

    SceneBatch batch{};
    batch.scene = currentScene;
    batch.firstDrawCommandOffset = firstDrawOffset;
    batch.drawCommandCount = drawCount;
    sceneBatches.push_back(batch);

    i = j;
  }

  // Pre-calculate joint data once so it's ready for the deferred upload
  for (auto &entity : entities) {
    auto &mesh = entity.getMesh();
    if (mesh.skinIndex.has_value()) {
      auto &skin = entity.scene->skins[mesh.skinIndex.value()];
      for (size_t i = 0; i < skin.joints.size(); ++i) {
        glm::mat4 jointMatrix =
            entity.scene->nodes[skin.joints[i]].globalTransform *
            skin.inverseBindMatrices[i];
        cpuJointData.push_back(jointMatrix);
      }
    }
  }

  // Flag ALL frames as dirty so they update their buffers when it's their turn
  for (size_t i = 0; i < framesDirty.size(); ++i) {
    framesDirty[i] = true;
  }
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
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
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

void EntitySys::render() {
  if (sceneBatches.empty())
    return;

  auto cmd = context.frameInfo.cmd;
  int frameIndex = context.frameInfo.frameIndex;

  // Ensure buffers are flushed just in case updateJoints wasn't called this
  // frame
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
    if (texSet == VK_NULL_HANDLE) {
      texSet = dummyTextureSet;
    }

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 1,
                            1, &texSet, 0, nullptr);

    vkCmdDrawIndexedIndirect(
        cmd, *(indirectDrawBuffers[frameIndex]),
        batch.firstDrawCommandOffset * sizeof(VkDrawIndexedIndirectCommand),
        batch.drawCommandCount, sizeof(VkDrawIndexedIndirectCommand));
  }

  debug::endLabel(context, cmd);
}
} // namespace vkh
