#include "freezeAnimation.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vulkan/vulkan_core.h>

#include <stdexcept>

#include "../descriptors.hpp"
#include "../pipeline.hpp"
#include "../renderer.hpp"

namespace vkh {
struct PushConstantData {
  float time;
};

void FreezeAnimationSys::createPipeline() {
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstantData);

  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      *context.vulkan.globalDescriptorSetLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  GraphicsPipeline::enableAlphaBlending(pipelineInfo);
  pipelineInfo.renderPass = renderer::getSwapChainRenderPass(context);
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipeline = std::make_unique<GraphicsPipeline>(
      context, "shaders/freezeAnimation.vert.spv",
      "shaders/freezeAnimation.frag.spv", pipelineInfo);
}
FreezeAnimationSys::FreezeAnimationSys(EngineContext &context) : System(context) { createPipeline(); }

void FreezeAnimationSys::render() {
  pipeline->bind(context.frameInfo.commandBuffer);

  vkCmdBindDescriptorSets(context.frameInfo.commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, 1,
                          &context.frameInfo.globalDescriptorSet, 0, nullptr);

  PushConstantData push{};
  push.time = glfwGetTime();

  vkCmdPushConstants(context.frameInfo.commandBuffer, *pipeline,
                     VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData),
                     &push);
  vkCmdDraw(context.frameInfo.commandBuffer, 6, 1, 0, 0);
}
} // namespace vkh
