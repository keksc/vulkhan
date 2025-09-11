#include "skybox.hpp"
#include <ktx.h>
#include <memory>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <ktx.h>
#include <ktxvulkan.h>
#include <vulkan/vulkan_core.h>

#include <vector>

#include "../descriptors.hpp"
#include "../pipeline.hpp"
#include "../scene.hpp"
#include "../swapChain.hpp"

namespace vkh {

struct PushConstantData {
  glm::mat4 modelMatrix{1.f};
  glm::mat4 normalMatrix{1.f};
};

void SkyboxSys::createSetLayout() {
  setLayout = DescriptorSetLayout::Builder(context)
                  .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                              VK_SHADER_STAGE_FRAGMENT_BIT) // Base color
                  .build();
  auto descInfo = cubeMap.getDescriptorInfo(context.vulkan.defaultSampler);
  DescriptorWriter(*setLayout, *context.vulkan.globalDescriptorPool)
      .writeImage(0, &descInfo)
      .build(set);
}
SkyboxSys::SkyboxSys(EngineContext &context)
    : System(context), cubeMap(context, "textures/skybox.ktx2") {
  createSetLayout();

  cubeScene = std::make_unique<Scene<Vertex>>(context, "models/cube.glb", true);

  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstantData);

  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      *context.vulkan.globalDescriptorSetLayout, *setLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  pipelineInfo.attributeDescriptions = Vertex::getAttributeDescriptions();
  pipelineInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  pipelineInfo.depthStencilInfo.depthTestEnable = VK_FALSE;
  pipelineInfo.depthStencilInfo.depthWriteEnable = VK_FALSE;
  pipelineInfo.vertpath = "shaders/skybox.vert.spv";
  pipelineInfo.fragpath = "shaders/skybox.frag.spv";
  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo);
}

void SkyboxSys::render() {
  pipeline->bind(context.frameInfo.cmd);

  VkDescriptorSet sets[] = {context.frameInfo.globalDescriptorSet, set};
  vkCmdBindDescriptorSets(context.frameInfo.cmd,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, 2,
                          sets, 0, nullptr);
  cubeScene->bind(context, context.frameInfo.cmd, *pipeline);
  cubeScene->meshes.begin()->primitives.begin()->draw(context.frameInfo.cmd);
}
} // namespace vkh
