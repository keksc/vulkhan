#include "point_light_system.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "../renderer.hpp"

#include <array>
#include <cassert>
#include <map>
#include <stdexcept>

namespace vkh {
namespace pointLightSys {
std::unique_ptr<Pipeline> pipeline;
VkPipelineLayout pipelineLayout;
struct PointLightPushConstants {
  glm::vec4 position{};
  glm::vec4 color{};
  float radius;
};

void createPipelineLayout(EngineContext &context,
                          VkDescriptorSetLayout globalSetLayout) {
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PointLightPushConstants);

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
      context, "pointLightSys", "shaders/point_light.vert.spv", "shaders/point_light.frag.spv",
      pipelineConfig);
}
void init(EngineContext &context, VkDescriptorSetLayout globalSetLayout) {
  createPipelineLayout(context, globalSetLayout);
  createPipeline(context);
}

void cleanup(EngineContext &context) {
  pipeline = nullptr;
  vkDestroyPipelineLayout(context.vulkan.device, pipelineLayout, nullptr);
}


void update(EngineContext &context, FrameInfo &frameInfo, GlobalUbo &ubo) {
  auto rotateLight =
      glm::rotate(glm::mat4(1.f), 0.5f * frameInfo.frameTime, {0.f, -1.f, 0.f});
  int lightIndex = 0;
  for (auto &pointLight : context.pointLights) {
    auto &transform = pointLight.transform;

    assert(lightIndex < MAX_LIGHTS && "Point lights exceed maximum specified");

    // update light position
    transform.translation =
        glm::vec3(rotateLight * glm::vec4(transform.translation, 1.f));

    // copy light to ubo
    ubo.pointLights[lightIndex].position =
        glm::vec4(transform.translation, 1.f);
    ubo.pointLights[lightIndex].color =
        glm::vec4(pointLight.color, pointLight.lightIntensity);

    lightIndex += 1;
  }
  ubo.numLights = lightIndex;
}

void render(EngineContext &context, FrameInfo &frameInfo) {
  // sort lights
  std::map<float, PointLight> sorted;
  for (auto pointLight : context.pointLights) {
    auto &transform = pointLight.transform;

    // calculate distance
    auto offset = frameInfo.camera.getPosition() - transform.translation;
    float disSquared = glm::dot(offset, offset);
    sorted[disSquared] = pointLight;
  }

  pipeline->bind(frameInfo.commandBuffer);

  vkCmdBindDescriptorSets(frameInfo.commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                          &frameInfo.globalDescriptorSet, 0, nullptr);

  // iterate through sorted lights in reverse order
  for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
    // use game obj id to find light object
    auto &pointLight = it->second;
    auto &transform = pointLight.transform;
    ;

    PointLightPushConstants push{};
    push.position = glm::vec4(transform.translation, 1.f);
    push.color = glm::vec4(pointLight.color, pointLight.lightIntensity);
    push.radius = transform.scale.x;

    vkCmdPushConstants(frameInfo.commandBuffer, pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT |
                           VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PointLightPushConstants), &push);
    vkCmdDraw(frameInfo.commandBuffer, 6, 1, 0, 0);
  }
}
} // namespace pointLightSys
} // namespace vkh
