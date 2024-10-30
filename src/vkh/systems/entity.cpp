#include "entity.hpp"
#include <fmt/base.h>

// libs
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cassert>
#include <stdexcept>

#include "../pipeline.hpp"
#include "../renderer.hpp"
#include "../entity.hpp"

namespace vkh {
namespace entitySys {
std::unique_ptr<Pipeline> pipeline;
VkPipelineLayout pipelineLayout;

struct PushConstantData {
  glm::mat4 modelMatrix{1.f};
  glm::mat4 normalMatrix{1.f};
};

void createPipelineLayout(EngineContext &context,
                          VkDescriptorSetLayout globalSetLayout) {
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
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
  pipelineConfig.renderPass = renderer::getSwapChainRenderPass(context);
  pipelineConfig.pipelineLayout = pipelineLayout;
  pipeline = std::make_unique<Pipeline>(
      context, "entity system", "shaders/entity.vert.spv",
      "shaders/entity.frag.spv", pipelineConfig);
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
    if(entity.model == nullptr) continue;
    auto &transform = entity.transform;
    auto model = entity.model;
    PushConstantData push{};
    push.modelMatrix = transform.mat4();
    push.normalMatrix = transform.normalMatrix();

    vkCmdPushConstants(context.frameInfo.commandBuffer, pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT |
                           VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstantData), &push);
    model->bind(context.frameInfo.commandBuffer);
    model->draw(context.frameInfo.commandBuffer);
  }
}
void update(EngineContext &context) {
  for (auto &entity : context.entities) {
    if (entity.model == nullptr)
      continue;
    entity.rigidBody.velocity +=
        entity.rigidBody.acceleration * context.frameInfo.dt;
    entity.transform.translation -=
        entity.rigidBody.velocity * context.frameInfo.dt * 0.05f;

    // Ground plane check for inverted y-axis
    if (entity.transform.translation.y >
        GROUND_LEVEL) { // Ground plane at y = 0
      entity.transform.translation.y = GROUND_LEVEL;
      entity.rigidBody.velocity.y = 0.0f; // Stop upward velocity
    }

    //entity.rigidBody.resetForces();
  }
}
} // namespace entitySys
} // namespace vkh
