#include "freezeAnimationSys.hpp"
#include <fmt/base.h>

// libs
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "../pipeline.hpp"
#include "../renderer.hpp"

#include <cassert>
#include <stdexcept>

namespace vkh {
namespace freezeAnimationSys {
std::unique_ptr<Pipeline> pipeline;
VkPipelineLayout pipelineLayout;

struct PushConstantData {
  float time;
};

void createPipelineLayout(EngineContext &context,
                          VkDescriptorSetLayout globalSetLayout) {
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstantData);

  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{globalSetLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
  if (vkCreatePipelineLayout(context.vulkan.device, &pipelineLayoutInfo,
                             nullptr, &pipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
}
void createPipeline(EngineContext &context) {
  assert(pipelineLayout != nullptr &&
         "Cannot create pipeline before pipeline layout");

  PipelineConfigInfo pipelineConfig{};
  Pipeline::enableAlphaBlending(pipelineConfig);
  pipelineConfig.attributeDescriptions.clear();
  pipelineConfig.bindingDescriptions.clear();
  pipelineConfig.renderPass = renderer::getSwapChainRenderPass(context);
  pipelineConfig.pipelineLayout = pipelineLayout;
  pipeline = std::make_unique<Pipeline>(
      context, "freezeAnimation system", "shaders/freezeAnimation.vert.spv",
      "shaders/freezeAnimation.frag.spv", pipelineConfig);
}
void init(EngineContext &context, VkDescriptorSetLayout globalSetLayout) {
  createPipelineLayout(context, globalSetLayout);
  createPipeline(context);
}

void cleanup(EngineContext &context) {
  pipeline = nullptr;
  vkDestroyPipelineLayout(context.vulkan.device, pipelineLayout, nullptr);
}

void render(EngineContext &context) {
  pipeline->bind(context.frameInfo.commandBuffer);

  auto projectionView =
      context.camera.projectionMatrix * context.camera.viewMatrix;

  vkCmdBindDescriptorSets(context.frameInfo.commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                          &context.frameInfo.globalDescriptorSet, 0, nullptr);

  for (auto entity : context.entities) {
    auto &transform = entity.transform;
    auto model = entity.model;
    PushConstantData push{};
    push.time = glfwGetTime();

    vkCmdPushConstants(context.frameInfo.commandBuffer, pipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(PushConstantData), &push);
    vkCmdDraw(context.frameInfo.commandBuffer, 6, 1, 0, 0);
  }
}
} // namespace freezeAnimationSys
} // namespace vkh
