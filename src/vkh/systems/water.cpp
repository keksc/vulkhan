#include "water.hpp"
#include <GLFW/glfw3.h>
#include <memory>
#include <optional>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cassert>
#include <stdexcept>

#include "../entity.hpp"
#include "../pipeline.hpp"
#include "../renderer.hpp"

namespace vkh {
namespace waterSys {
std::unique_ptr<Pipeline> pipeline;
VkPipelineLayout pipelineLayout;

struct PushConstantData {
  glm::mat4 modelMatrix{1.f};
  glm::mat4 normalMatrix{1.f};
  float time;
};

std::unique_ptr<Entity> entity;

void createPipelineLayout(EngineContext &context,
                          VkDescriptorSetLayout globalSetLayout) {
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
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
                             nullptr, &pipelineLayout) != VK_SUCCESS)
    throw std::runtime_error("failed to create pipeline layout!");
}
void createPipeline(EngineContext &context) {
  assert(pipelineLayout != nullptr &&
         "Cannot create pipeline before pipeline layout");

  PipelineConfigInfo pipelineConfig{};
  pipelineConfig.renderPass = renderer::getSwapChainRenderPass(context);
  pipelineConfig.pipelineLayout = pipelineLayout;
  pipeline = std::make_unique<Pipeline>(
      context, "entity system", "shaders/water.vert.spv",
      "shaders/water.frag.spv", pipelineConfig);
}
void init(EngineContext &context, VkDescriptorSetLayout globalSetLayout) {
  createPipelineLayout(context, globalSetLayout);
  createPipeline(context);
  Transform transform{.position{60.f, 0.f, 0.f}, .scale{50.f, 1.f, 50.f}};
  entity = std::make_unique<Entity>(context, transform, "subdivided quad",
                                    "models/quadsubdivided.obj");
}

void cleanup(EngineContext &context) {
  entity = nullptr;
  pipeline = nullptr;
  vkDestroyPipelineLayout(context.vulkan.device, pipelineLayout, nullptr);
}

void render(EngineContext &context) {
  pipeline->bind(context.frameInfo.commandBuffer);

  vkCmdBindDescriptorSets(context.frameInfo.commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                          &context.frameInfo.globalDescriptorSet, 0, nullptr);

  auto &transform = entity->transform;
  auto &model = entity->model;
  PushConstantData push{};
  push.modelMatrix = transform.mat4();
  push.normalMatrix = transform.normalMatrix();
  push.time = glfwGetTime();

  vkCmdPushConstants(context.frameInfo.commandBuffer, pipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantData),
                     &push);
  model->bind(context, context.frameInfo.commandBuffer, pipelineLayout);
  model->draw(context.frameInfo.commandBuffer);
}
} // namespace waterSys
} // namespace vkh
