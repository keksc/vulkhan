#include "particles.hpp"
#include <memory>
#include <vulkan/vulkan_core.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <fmt/format.h>

#include <cassert>
#include <stdexcept>

#include "../descriptors.hpp"
#include "../model.hpp"
#include "../pipeline.hpp"
#include "../renderer.hpp"

namespace vkh {
namespace particleSys {
std::unique_ptr<Pipeline> pipeline;
VkPipelineLayout pipelineLayout;

struct PushConstantData {
  int particleIndex;
};

void createPipelineLayout(EngineContext &context) {
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      context.vulkan.globalDescriptorSetLayout->getDescriptorSetLayout()};

  VkPushConstantRange pushConstantRange;
  pushConstantRange.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstantData);

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
  pipelineConfig.renderPass = renderer::getSwapChainRenderPass(context);
  pipelineConfig.pipelineLayout = pipelineLayout;
  pipelineConfig.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  pipeline =
      std::make_unique<Pipeline>(context, "shaders/particles.vert.spv",
                                 "shaders/particles.frag.spv", pipelineConfig);
}
void init(EngineContext &context) {
  createPipelineLayout(context);
  createPipeline(context);
}

void cleanup(EngineContext &context) {
  pipeline = nullptr;
  vkDestroyPipelineLayout(context.vulkan.device, pipelineLayout, nullptr);
}

void update(EngineContext &context, GlobalUbo &ubo) {
  auto rotateParticle = glm::rotate(glm::mat4(1.f), 0.5f * context.frameInfo.dt,
                                    {0.f, -1.f, 0.f});
  int particleIndex = 0;
  for (int i = 0; i < 6; i++) {
    assert(particleIndex < MAX_PARTICLES &&
           "Point lights exceed maximum specified");

    // update light position
    context.particles[i].position = glm::vec4(
        rotateParticle * glm::vec4(context.particles[i].position, 1.f));

    // copy light to ubo
    ubo.particles[particleIndex].position = context.particles[i].position;
    ubo.particles[particleIndex].color = context.particles[i].color;

    particleIndex += 1;
  }
  ubo.numParticles = particleIndex;
}

void render(EngineContext &context) {
  pipeline->bindGraphics(context.frameInfo.commandBuffer);

  vkCmdBindDescriptorSets(context.frameInfo.commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                          &context.frameInfo.globalDescriptorSet, 0, nullptr);
  for (int i = 0; i < context.particles.size(); i++) {
    PushConstantData push{.particleIndex = i};
    vkCmdPushConstants(context.frameInfo.commandBuffer, pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT |
                           VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstantData), &push);
    vkCmdDraw(context.frameInfo.commandBuffer, 1, 1, 0, 0);
  }
}
} // namespace particleSys
} // namespace vkh
