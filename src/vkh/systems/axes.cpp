#include "axes.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <fmt/format.h>

#include <cassert>
#include <stdexcept>

#include "../pipeline.hpp"
#include "../renderer.hpp"
#include "../descriptors.hpp"

namespace vkh {
namespace axesSys {
std::unique_ptr<Pipeline> pipeline;
VkPipelineLayout pipelineLayout;

void createPipelineLayout(EngineContext &context) {
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{context.vulkan.globalDescriptorSetLayout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
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
  pipelineConfig.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  pipelineConfig.renderPass = renderer::getSwapChainRenderPass(context);
  pipelineConfig.pipelineLayout = pipelineLayout;
  pipeline = std::make_unique<Pipeline>(
      context, "axes system", "shaders/axes.vert.spv", "shaders/axes.frag.spv",
      pipelineConfig);
}
void init(EngineContext &context) {
  createPipelineLayout(context);
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

  vkCmdDraw(context.frameInfo.commandBuffer, 6, 1, 0, 0);
}
} // namespace axesSys
} // namespace vkh
