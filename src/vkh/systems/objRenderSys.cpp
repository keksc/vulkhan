#include "objRenderSys.hpp"

// libs
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "../renderer.hpp"
#include "../pipeline.hpp"

#include <cassert>
#include <stdexcept>

namespace vkh {
namespace objRenderSys {
std::unique_ptr<Pipeline> pipeline;
VkPipelineLayout pipelineLayout;

struct SimplePushConstantData {
  glm::mat4 modelMatrix{1.f};
  glm::mat4 normalMatrix{1.f};
};

void createPipelineLayout(EngineContext &context,
                          VkDescriptorSetLayout globalSetLayout) {
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(SimplePushConstantData);

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
  pipelineConfig.renderPass = renderer::getSwapChainRenderPass(context);
  pipelineConfig.pipelineLayout = pipelineLayout;
  pipeline = std::make_unique<Pipeline>(
      context, "objRenderSys", "shaders/simple_shader.vert.spv",
      "shaders/simple_shader.frag.spv", pipelineConfig);
}
void init(EngineContext &context, VkDescriptorSetLayout globalSetLayout) {
  createPipelineLayout(context, globalSetLayout);
  createPipeline(context);
}

void cleanup(EngineContext &context) {
  pipeline = nullptr;
  vkDestroyPipelineLayout(context.vulkan.device, pipelineLayout, nullptr);
}

void renderGameObjects(EngineContext &context) {
  pipeline->bind(context.frameInfo.commandBuffer);

  auto projectionView =
      context.camera.getProjection() * context.camera.getView();

  vkCmdBindDescriptorSets(context.frameInfo.commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                          &context.frameInfo.globalDescriptorSet, 0, nullptr);

  for (auto entity : context.entities) {
    auto &transform = entity.transform;
    auto model = entity.model;
    SimplePushConstantData push{};
    push.modelMatrix = transform.mat4();
    push.normalMatrix = transform.normalMatrix();

    vkCmdPushConstants(context.frameInfo.commandBuffer, pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT |
                           VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(SimplePushConstantData), &push);
    model->bind(context.frameInfo.commandBuffer);
    model->draw(context.frameInfo.commandBuffer);
  }
}

} // namespace objRenderSys
} // namespace vkh
