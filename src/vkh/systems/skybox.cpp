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

#include "../debug.hpp"
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
  setLayout = buildDescriptorSetLayout(
      context, {VkDescriptorSetLayoutBinding{
                   .binding = 0,
                   .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   .descriptorCount = 1,
                   .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
               }});
  debug::setObjName(context, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                    reinterpret_cast<uint64_t>(setLayout),
                    "skyboxSys set layout");
  set = context.vulkan.globalDescriptorAllocator->allocate(setLayout);
  DescriptorWriter writer(context);
  writer.writeImage(0, cubeMap.getDescriptorInfo(context.vulkan.defaultSampler),
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
  writer.updateSet(set);
}
SkyboxSys::SkyboxSys(EngineContext &context)
    : System(context), cubeMap(context, "textures/skybox.ktx2") {
  createSetLayout();

  cubeScene = std::make_unique<Scene<Vertex>>(context, "models/cube.glb", setLayout, true);

  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstantData);

  std::vector<VkDescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout, setLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();
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
  pipeline =
      std::make_unique<GraphicsPipeline>(context, pipelineInfo, "skybox");
}
SkyboxSys::~SkyboxSys() {
  vkDestroyDescriptorSetLayout(context.vulkan.device, setLayout, nullptr);
}
void SkyboxSys::render() {
  debug::beginLabel(context, context.frameInfo.cmd, "skybox rendering",
                    {.3f, .3f, 1.f, 1.f});
  pipeline->bind(context.frameInfo.cmd);

  VkDescriptorSet sets[] = {
      context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex], set};
  vkCmdBindDescriptorSets(context.frameInfo.cmd,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, 2,
                          sets, 0, nullptr);
  cubeScene->bind(context, context.frameInfo.cmd, *pipeline);
  cubeScene->meshes.begin()->primitives.begin()->draw(context.frameInfo.cmd);
  debug::endLabel(context, context.frameInfo.cmd);
}
} // namespace vkh
