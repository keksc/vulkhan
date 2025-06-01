#include "entities.hpp"
#include <memory>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vulkan/vulkan_core.h>

#include <vector>

#include "../../descriptors.hpp"
#include "../../pipeline.hpp"
#include "../../scene.hpp"
#include "../../swapChain.hpp"

namespace vkh {

struct PushConstantData {
  glm::mat4 modelMatrix{1.f};
  glm::mat4 normalMatrix{1.f};
};

void EntitySys::createSetLayout() {
  setLayout = DescriptorSetLayout::Builder(context)
                  .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                              VK_SHADER_STAGE_FRAGMENT_BIT)
                  .build();
}
void EntitySys::createSampler() {
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(context.vulkan.physicalDevice, &properties);

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

  if (vkCreateSampler(context.vulkan.device, &samplerInfo, nullptr, &sampler) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create texture sampler!");
  }
}
EntitySys::~EntitySys() {
  vkDestroySampler(context.vulkan.device, sampler, nullptr);
}
EntitySys::EntitySys(EngineContext &context, std::vector<Entity> &entities)
    : System(context), entities{entities} {
  createSampler();
  createSetLayout();

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

  PipelineCreateInfo pipelineConfig{};
  pipelineConfig.layoutInfo = pipelineLayoutInfo;
  pipelineConfig.renderPass = context.vulkan.swapChain->renderPass;
  pipelineConfig.attributeDescriptions = Vertex::getAttributeDescriptions();
  pipelineConfig.bindingDescriptions = Vertex::getBindingDescriptions();
  pipeline = std::make_unique<GraphicsPipeline>(
      context, "shaders/entities.vert.spv", "shaders/entities.frag.spv",
      pipelineConfig);
}

void EntitySys::render() {
  pipeline->bind(context.frameInfo.cmd);

  /*vkCmdBindDescriptorSets(context.frameInfo.cmd,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                          &context.frameInfo.globalDescriptorSet, 0, nullptr);*/

  for (auto &entity : entities) {
    auto &transform = entity.transform;
    auto &scene = entity.mesh;
    scene->bind(
        context, context.frameInfo.cmd, *pipeline,
        {context.frameInfo.globalDescriptorSet, scene->textureDescriptorSet});

    PushConstantData push{};
    for (auto &mesh : *scene) {
      push.modelMatrix = transform.mat4() * mesh.transform;
      push.normalMatrix = transform.normalMatrix();

      vkCmdPushConstants(context.frameInfo.cmd, pipeline->getLayout(),
                         VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT,
                         0, sizeof(PushConstantData), &push);
      mesh.draw(context.frameInfo.cmd);
    }
  }
}
void EntitySys::addEntity(Transform transform,
                          const std::filesystem::path &path,
                          RigidBody rigidBody) {
  entities.emplace_back(transform, rigidBody, createMesh(path));
}
std::shared_ptr<Scene<EntitySys::Vertex>>
EntitySys::createMesh(const std::filesystem::path &path) {
  return std::make_shared<Scene<Vertex>>(context, path, sampler, *setLayout);
}
} // namespace vkh
