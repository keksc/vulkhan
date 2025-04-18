#include "entities.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vulkan/vulkan_core.h>

#include <stdexcept>
#include <vector>

#include "../../descriptors.hpp"
#include "../../mesh.hpp"
#include "../../pipeline.hpp"
#include "../../renderer.hpp"
#include "entity.hpp"

namespace vkh {

struct PushConstantData {
  glm::mat4 modelMatrix{1.f};
  glm::mat4 normalMatrix{1.f};
};

EntitySys::EntitySys(EngineContext &context) : System(context) {
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstantData);

  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      *context.vulkan.globalDescriptorSetLayout,
      *context.vulkan.modelDescriptorSetLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

  PipelineCreateInfo pipelineConfig{};
  pipelineConfig.layoutInfo = pipelineLayoutInfo;
  pipelineConfig.renderPass = renderer::getSwapChainRenderPass(context);
  pipelineConfig.attributeDescriptions = Vertex::getAttributeDescriptions();
  pipelineConfig.bindingDescriptions = Vertex::getBindingDescriptions();
  pipeline = std::make_unique<GraphicsPipeline>(
      context, "shaders/entities.vert.spv", "shaders/entities.frag.spv",
      pipelineConfig);
}

void EntitySys::render() {
  pipeline->bind(context.frameInfo.commandBuffer);

  /*vkCmdBindDescriptorSets(context.frameInfo.commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                          &context.frameInfo.globalDescriptorSet, 0, nullptr);*/

  for (auto &entity : context.entities) {
    if (entity.model == nullptr)
      continue;
    auto &transform = entity.transform;
    auto &model = entity.model;
    PushConstantData push{};
    push.modelMatrix = transform.mat4();
    push.normalMatrix = transform.normalMatrix();

    vkCmdPushConstants(context.frameInfo.commandBuffer, pipeline->getLayout(),
                       VK_SHADER_STAGE_VERTEX_BIT |
                           VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstantData), &push);
    model->bind(
        context, context.frameInfo.commandBuffer, *pipeline,
        {context.frameInfo.globalDescriptorSet, model->textureDescriptorSet});
    model->draw(context.frameInfo.commandBuffer);
  }
}
} // namespace vkh
