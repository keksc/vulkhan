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
}

void EntitySys::createPipeline() {
  std::vector<VkDescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout, textureSetLayout,
      instanceSetLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 0; // Removed Push Constants!
  pipelineLayoutInfo.pPushConstantRanges = nullptr;

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  pipelineInfo.attributeDescriptions = Vertex::getAttributeDescriptions();
  pipelineInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  pipelineInfo.vertpath = "shaders/entities.vert.spv";
  pipelineInfo.fragpath = "shaders/entities.frag.spv";

  pipeline =
      std::make_unique<GraphicsPipeline>(context, pipelineInfo, "entities");
}

EntitySys::~EntitySys() {
  vkDestroyDescriptorSetLayout(context.vulkan.device, textureSetLayout,
                               nullptr);
  vkDestroyDescriptorSetLayout(context.vulkan.device, instanceSetLayout,
                               nullptr);
}

void EntitySys::setEntities(std::vector<Entity> &entities) {
  std::sort(entities.begin(), entities.end(),
            [](const Entity &a, const Entity &b) {
              if (a.scene != b.scene)
                return a.scene < b.scene;
              return a.meshIndex < b.meshIndex;
            });

  updateBuffers(entities);
}
void EntitySys::updateJoints(std::vector<Entity> &sortedEntities) {
  if (!jointBuffer || sortedEntities.empty())
    return;

  std::vector<glm::mat4> jointData;
  for (auto &entity : sortedEntities) {
    auto &mesh = entity.getMesh();
    if (mesh.skinIndex.has_value()) {
      auto &skin = entity.scene->skins[mesh.skinIndex.value()];
      for (size_t i = 0; i < skin.joints.size(); ++i) {
        // Joint Matrix = Joint Global Transform * Inverse Bind Matrix
        glm::mat4 jointMatrix =
            entity.scene->nodes[skin.joints[i]].globalTransform *
            skin.inverseBindMatrices[i];
        jointData.push_back(jointMatrix);
      }
    }
  }

  if (!jointData.empty()) {
    jointBuffer->map();
    jointBuffer->write(jointData.data(), jointData.size() * sizeof(glm::mat4));
    jointBuffer->unmap();
  }
}
void EntitySys::updateBuffers(std::vector<Entity> &sortedEntities) {
  sceneBatches.clear();
  if (sortedEntities.empty())
    return;

  std::vector<GPUInstanceData> instanceData;
  std::vector<VkDrawIndexedIndirectCommand> drawCommands;

  size_t totalInstancesEst = 0;
  for (auto &e : sortedEntities)
    totalInstancesEst += e.getMesh().primitives.size();
  instanceData.reserve(totalInstancesEst);
  drawCommands.reserve(totalInstancesEst);

  uint32_t currentInstanceIndex = 0;
  uint32_t currentJointOffset = 0;

  for (size_t i = 0; i < sortedEntities.size();) {
    auto currentScene = sortedEntities[i].scene;
    uint32_t firstDrawOffset = static_cast<uint32_t>(drawCommands.size());
    uint32_t drawCount = 0;

    size_t j = i;
    while (j < sortedEntities.size() &&
           sortedEntities[j].scene == currentScene) {
      size_t k = j;
      while (k < sortedEntities.size() &&
             sortedEntities[k].scene == currentScene &&
             sortedEntities[k].meshIndex == sortedEntities[j].meshIndex) {
        k++;
      }

      uint32_t instanceCount = static_cast<uint32_t>(k - j);
      auto &entity = sortedEntities[j];
      auto &mesh = entity.getMesh();

      for (const auto &primitive : mesh.primitives) {
        VkDrawIndexedIndirectCommand cmd{};
        cmd.indexCount = primitive.indexCount;
        cmd.instanceCount = instanceCount;
        cmd.firstIndex = primitive.indexOffset;
        cmd.vertexOffset = 0;
        cmd.firstInstance = currentInstanceIndex;

        drawCommands.push_back(cmd);
        drawCount++;

        auto &mat = currentScene->materials[primitive.materialIndex];
        int32_t texIdx =
            mat.baseColorTextureIndex.has_value()
                ? static_cast<int32_t>(mat.baseColorTextureIndex.value())
                : -1;

        for (size_t inst = j; inst < k; ++inst) {
          GPUInstanceData data;
          data.modelMatrix =
              sortedEntities[inst].transform.mat4() * mesh.transform;
          data.normalMatrix =
              glm::mat4(sortedEntities[inst].transform.normalMatrix());
          data.color = mat.baseColorFactor;
          data.textureIndex = texIdx;
          if (mesh.skinIndex.has_value()) {
            data.jointOffset = currentJointOffset;
            currentJointOffset +=
                currentScene->skins[mesh.skinIndex.value()].joints.size();
          } else {
            data.jointOffset = -1;
          }
          instanceData.push_back(data);
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

  VkDeviceSize instanceBufferSize =
      instanceData.size() * sizeof(GPUInstanceData);
  VkDeviceSize jointBufferSize = std::max<VkDeviceSize>(
      currentJointOffset * sizeof(glm::mat4), sizeof(glm::mat4));

  bool updateDescriptor = false;

  if (!instanceBuffer || instanceBuffer->getSize() < instanceBufferSize) {
    instanceBuffer = std::make_unique<Buffer<GPUInstanceData>>(
        context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        instanceData.size());
    updateDescriptor = true;
  }

  if (!jointBuffer || jointBuffer->getSize() < jointBufferSize) {
    jointBuffer = std::make_unique<Buffer<glm::mat4>>(
        context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        std::max<uint32_t>(currentJointOffset, 1));
    updateDescriptor = true;
  }

  if (updateDescriptor) {
    if (!instanceDescriptorSet) {
      instanceDescriptorSet =
          context.vulkan.globalDescriptorAllocator->allocate(instanceSetLayout);
    }
    DescriptorWriter writer(context);

    VkDescriptorBufferInfo bInfo = instanceBuffer->descriptorInfo();
    writer.writeBuffer(0, bInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

    VkDescriptorBufferInfo jInfo = jointBuffer->descriptorInfo();
    writer.writeBuffer(1, jInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

    writer.updateSet(instanceDescriptorSet);
  }

  if (instanceBufferSize > 0) {
    instanceBuffer->map();
    instanceBuffer->write(instanceData.data(), instanceBufferSize);
    instanceBuffer->unmap();
  }

  VkDeviceSize cmdBufferSize =
      drawCommands.size() * sizeof(VkDrawIndexedIndirectCommand);
  if (!indirectDrawBuffer || indirectDrawBuffer->getSize() < cmdBufferSize) {
    indirectDrawBuffer = std::make_unique<Buffer<VkDrawIndexedIndirectCommand>>(
        context,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        drawCommands.size());
  }

  if (cmdBufferSize > 0) {
    indirectDrawBuffer->map();
    indirectDrawBuffer->write(drawCommands.data(), cmdBufferSize);
    indirectDrawBuffer->unmap();
  }
  updateJoints(sortedEntities);
}

void EntitySys::render() {
  if (sceneBatches.empty())
    return;

  auto cmd = context.frameInfo.cmd;
  debug::beginLabel(context, cmd, "Indirect Entities", {.7f, .3f, 1.f, 1.f});

  pipeline->bind(cmd);

  vkCmdBindDescriptorSets(
      cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, 1,
      &context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex], 0,
      nullptr);

  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 2, 1,
                          &instanceDescriptorSet, 0, nullptr);

  for (const auto &batch : sceneBatches) {
    batch.scene->bind(context, cmd, *pipeline);

    VkDescriptorSet texSet = batch.scene->sceneTextureSet;
    if (texSet == VK_NULL_HANDLE) {
      texSet = dummyTextureSet;
    }

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 1,
                            1, &texSet, 0, nullptr);

    vkCmdDrawIndexedIndirect(
        cmd, *indirectDrawBuffer,
        batch.firstDrawCommandOffset * sizeof(VkDrawIndexedIndirectCommand),
        batch.drawCommandCount, sizeof(VkDrawIndexedIndirectCommand));
  }

  debug::endLabel(context, cmd);
}
} // namespace vkh
