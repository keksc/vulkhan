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

struct PushConstantData {
  alignas(16) glm::vec4 color;
  alignas(4) uint32_t useColorTexture;
};

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
        {VkDescriptorSetLayoutBinding{
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        }},
        0, &bindingFlagsInfo);
  }

  {
    instanceSetLayout = buildDescriptorSetLayout(
        context, {VkDescriptorSetLayoutBinding{
                     .binding = 0,
                     .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                     .descriptorCount = 1,
                     .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                 }});
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
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstantData);

  std::vector<VkDescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout, textureSetLayout,
      instanceSetLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

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

void EntitySys::updateBuffers(std::vector<Entity> &sortedEntities) {
  renderBatches.clear();
  if (sortedEntities.empty())
    return;

  std::vector<GPUInstanceData> instanceData;
  std::vector<VkDrawIndexedIndirectCommand> drawCommands;

  instanceData.reserve(sortedEntities.size());
  drawCommands.reserve(sortedEntities.size() * 2);

  uint32_t currentInstanceIndex = 0;

  for (size_t i = 0; i < sortedEntities.size();) {
    size_t j = i + 1;
    while (j < sortedEntities.size() &&
           sortedEntities[j].scene == sortedEntities[i].scene &&
           sortedEntities[j].meshIndex == sortedEntities[i].meshIndex) {
      j++;
    }

    uint32_t instanceCount = static_cast<uint32_t>(j - i);

    for (size_t k = i; k < j; k++) {
      instanceData.push_back(
          {.modelMatrix = sortedEntities[k].transform.mat4() *
                          sortedEntities[k].getMesh().transform,
           .normalMatrix =
               glm::mat4(sortedEntities[k].transform.normalMatrix())});
    }

    auto &entity = sortedEntities[i];
    auto &mesh = entity.getMesh();

    for (const auto &primitive : mesh.primitives) {
      VkDrawIndexedIndirectCommand cmd{};
      cmd.indexCount = primitive.indexCount;
      cmd.instanceCount = instanceCount;
      cmd.firstIndex = primitive.indexOffset;
      cmd.vertexOffset = 0;
      cmd.firstInstance = currentInstanceIndex;

      uint32_t cmdOffset = static_cast<uint32_t>(
          drawCommands.size() * sizeof(VkDrawIndexedIndirectCommand));
      drawCommands.push_back(cmd);

      auto &mat = entity.scene->materials[primitive.materialIndex];
      IndirectBatch batch{};
      batch.scene = entity.scene;
      batch.materialIndex = primitive.materialIndex;
      batch.indirectOffset = cmdOffset;
      batch.baseColorFactor = mat.baseColorFactor;
      batch.useTexture = mat.baseColorTextureIndex.has_value();

      renderBatches.push_back(batch);
    }

    currentInstanceIndex += instanceCount;
    i = j;
  }

  VkDeviceSize instanceBufferSize =
      instanceData.size() * sizeof(GPUInstanceData);

  if (!instanceBuffer || instanceBuffer->getSize() < instanceBufferSize) {
    instanceBuffer = std::make_unique<Buffer<GPUInstanceData>>(
        context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        instanceData.size());

    instanceDescriptorSet =
        context.vulkan.globalDescriptorAllocator->allocate(instanceSetLayout);
    DescriptorWriter writer(context);
    VkDescriptorBufferInfo bufferInfo = instanceBuffer->descriptorInfo();
    writer.writeBuffer(0, bufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
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
}

void EntitySys::render() {
  if (renderBatches.empty())
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

  std::shared_ptr<Scene<Vertex>> currentScene = nullptr;

  for (const auto &batch : renderBatches) {
    if (batch.scene != currentScene) {
      currentScene = batch.scene;
      currentScene->bind(context, cmd, *pipeline);
    }

    auto &mat = currentScene->materials[batch.materialIndex];
    if (batch.useTexture && mat.baseColorTextureIndex.has_value()) {
      vkCmdBindDescriptorSets(
          cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 1, 1,
          &currentScene->imageDescriptorSets[mat.baseColorTextureIndex.value()],
          0, nullptr);
    } else {
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline,
                              1, 1, &dummyTextureSet, 0, nullptr);
    }

    PushConstantData push{};
    push.color = batch.baseColorFactor;
    push.useColorTexture = batch.useTexture ? 1 : 0;
    vkCmdPushConstants(cmd, *pipeline, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(PushConstantData), &push);

    vkCmdDrawIndexedIndirect(cmd, *indirectDrawBuffer, batch.indirectOffset, 1,
                             sizeof(VkDrawIndexedIndirectCommand));
  }

  debug::endLabel(context, cmd);
}
} // namespace vkh
