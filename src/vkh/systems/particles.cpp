#include "particles.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vulkan/vulkan_core.h>

#include <fmt/format.h>

#include <memory>
#include <stdexcept>

#include "../descriptors.hpp"
#include "../pipeline.hpp"
#include "../renderer.hpp"

namespace vkh {
namespace particleSys {
std::unique_ptr<GraphicsPipeline> pipeline;

struct PushConstantData {
  int particleIndex;
};

void createPipeline(EngineContext &context) {
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      *context.vulkan.globalDescriptorSetLayout};

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

  PipelineCreateInfo pipelineConfig{};
  GraphicsPipeline::enableAlphaBlending(pipelineConfig);
  pipelineConfig.renderPass = renderer::getSwapChainRenderPass(context);
  pipelineConfig.layoutInfo = pipelineLayoutInfo;
  pipelineConfig.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  pipeline = std::make_unique<GraphicsPipeline>(
      context, "shaders/particles.vert.spv", "shaders/particles.frag.spv",
      pipelineConfig);
}
void init(EngineContext &context) { createPipeline(context); }

void cleanup(EngineContext &context) { pipeline = nullptr; }

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
  pipeline->bind(context.frameInfo.commandBuffer);

  vkCmdBindDescriptorSets(context.frameInfo.commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, 1,
                          &context.frameInfo.globalDescriptorSet, 0, nullptr);
  for (int i = 0; i < context.particles.size(); i++) {
    PushConstantData push{.particleIndex = i};
    vkCmdPushConstants(context.frameInfo.commandBuffer, *pipeline,
                       VK_SHADER_STAGE_VERTEX_BIT |
                           VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstantData), &push);
    vkCmdDraw(context.frameInfo.commandBuffer, 1, 1, 0, 0);
  }
}
} // namespace particleSys
} // namespace vkh
