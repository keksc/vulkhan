#include "skybox.hpp"

#include "../debug.hpp"
#include "../pipeline.hpp"
#include "../scene.hpp"
#include "../swapChain.hpp"
#include <vulkan/vulkan.hpp>

namespace vkh {

struct PushConstantData {
  glm::mat4 modelMatrix{1.f};
  glm::mat4 normalMatrix{1.f};
};

void SkyboxSys::createSetLayout() {
  setLayout = buildDescriptorSetLayout(
      context, {vk::DescriptorSetLayoutBinding{
                   0, vk::DescriptorType::eCombinedImageSampler, 1,
                   vk::ShaderStageFlagBits::eFragment, nullptr}});

  debug::setObjName(
      context, vk::ObjectType::eDescriptorSetLayout,
      reinterpret_cast<uint64_t>(static_cast<VkDescriptorSetLayout>(setLayout)),
      "skyboxSys set layout");
  set = context.vulkan.globalDescriptorAllocator->allocate(setLayout);

  DescriptorWriter writer(context);
  writer.writeImage(0, cubeMap.getDescriptorInfo(context.vulkan.defaultSampler),
                    vk::DescriptorType::eCombinedImageSampler);
  writer.updateSet(set);
}

SkyboxSys::SkyboxSys(EngineContext &context)
    : System(context), cubeMap(context, "textures/night.ktx2") {
  createSetLayout();

  cubeScene = std::make_unique<Scene<Vertex>>(context, "models/cube.glb",
                                              setLayout, true);

  vk::PushConstantRange pushConstantRange{
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
      sizeof(PushConstantData)};

  std::vector<vk::DescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout, setLayout};

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  pipelineInfo.attributeDescriptions = Vertex::getAttributeDescriptions();
  pipelineInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  pipelineInfo.depthStencilInfo.depthTestEnable = true;
  pipelineInfo.depthStencilInfo.depthCompareOp = vk::CompareOp::eGreaterOrEqual;
  pipelineInfo.depthStencilInfo.depthWriteEnable = false;
  pipelineInfo.vertpath = "shaders/skybox.vert.spv";
  pipelineInfo.fragpath = "shaders/skybox.frag.spv";

  pipelineInfo.subpass = 0;
  pipelineInfo.multisampleInfo.rasterizationSamples =
      context.vulkan.msaaSamples;

  pipeline =
      std::make_unique<GraphicsPipeline>(context, pipelineInfo, "skybox");
}

SkyboxSys::~SkyboxSys() {
  if (context.vulkan.device) {
    context.vulkan.device.destroyDescriptorSetLayout(setLayout, nullptr);
  }
}

void SkyboxSys::render() {
  auto &cmd = context.frameInfo.cmd;

  debug::beginLabel(context, cmd, "skybox rendering", {.3f, .3f, 1.f, 1.f});
  pipeline->bind(cmd);

  std::vector<vk::DescriptorSet> sets = {
      context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex], set};
  cmd.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, pipeline->getLayout(), 0,
      static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

  cubeScene->bind(context, cmd, *pipeline);
  cubeScene->meshes.begin()->primitives.begin()->draw(cmd);
  debug::endLabel(context, cmd);
}

} // namespace vkh
