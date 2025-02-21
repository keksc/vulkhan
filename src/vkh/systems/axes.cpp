#include "axes.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cassert>
#include <stdexcept>

#include "../descriptors.hpp"
#include "../pipeline.hpp"
#include "../renderer.hpp"

namespace vkh {
namespace axesSys {
std::unique_ptr<GraphicsPipeline> pipeline;

void createPipeline(EngineContext &context) {
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      *context.vulkan.globalDescriptorSetLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  PipelineCreateInfo pipelineConfig{};
  GraphicsPipeline::enableAlphaBlending(pipelineConfig);
  pipelineConfig.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  pipelineConfig.renderPass = renderer::getSwapChainRenderPass(context);
  pipelineConfig.layoutInfo = pipelineLayoutInfo;
  pipeline = std::make_unique<GraphicsPipeline>(
      context, "shaders/axes.vert.spv", "shaders/axes.frag.spv",
      pipelineConfig);
}
void init(EngineContext &context) {
  createPipeline(context);
}

void cleanup(EngineContext &context) {
  pipeline = nullptr;
}

void render(EngineContext &context) {
  pipeline->bind(context.frameInfo.commandBuffer);

  auto projectionView =
      context.camera.projectionMatrix * context.camera.viewMatrix;

  vkCmdBindDescriptorSets(context.frameInfo.commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, 1,
                          &context.frameInfo.globalDescriptorSet, 0, nullptr);

  vkCmdDraw(context.frameInfo.commandBuffer, 6, 1, 0, 0);
}
} // namespace axesSys
} // namespace vkh
