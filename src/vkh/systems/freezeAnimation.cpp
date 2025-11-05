#include "freezeAnimation.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vulkan/vulkan_core.h>

#include <stdexcept>

#include "../descriptors.hpp"
#include "../pipeline.hpp"
#include "../swapChain.hpp"

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
  pipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.vertpath = "shaders/freezeAnimation.vert.spv";
  pipelineInfo.fragpath = "shaders/freezeAnimation.frag.spv";
  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo, "freeze animation");
}
FreezeAnimationSys::FreezeAnimationSys(EngineContext &context)
    : System(context) {
  createPipeline();
}

void FreezeAnimationSys::render() {
  pipeline->bind(context.frameInfo.cmd);

  vkCmdBindDescriptorSets(context.frameInfo.cmd,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, 1,
                          &context.frameInfo.globalDescriptorSet, 0, nullptr);

  PushConstantData push{};
  push.time = static_cast<float>(glfwGetTime());

  vkCmdPushConstants(context.frameInfo.cmd, *pipeline,
                     VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData),
                     &push);
  vkCmdDraw(context.frameInfo.cmd, 6, 1, 0, 0);
}
} // namespace vkh
