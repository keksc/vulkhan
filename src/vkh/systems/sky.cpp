#include "sky.hpp"

#include "../debug.hpp"
#include "../pipeline.hpp"
#include "../sceneBuilder.hpp"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>

namespace vkh {

struct PushConstantData {
  glm::mat4 modelMatrix{1.f};
  glm::mat4 normalMatrix{1.f};
};

SkySys::SkySys(EngineContext &context) : System(context) {

  std::vector<vk::DescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout,
  };

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.depthStencilInfo.depthTestEnable = true;
  pipelineInfo.depthStencilInfo.depthCompareOp = vk::CompareOp::eGreaterOrEqual;
  pipelineInfo.depthStencilInfo.depthWriteEnable = false;
  pipelineInfo.vertpath = "shaders/sky.vert.spv";
  pipelineInfo.fragpath = "shaders/sky.frag.spv";

  pipelineInfo.multisampleInfo.rasterizationSamples =
      context.vulkan.msaaSamples;

  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo, "sky");
}

void SkySys::render() {
  auto &cmd = context.frameInfo.cmd;

  debug::beginLabel(context, cmd, "sky rendering", {.3f, .3f, 1.f, 1.f});
  pipeline->bind(cmd);

  cmd.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, pipeline->getLayout(), 0, 1,
      &context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex], 0,
      nullptr);

  cmd.draw(3, 1, 0, 0);
  debug::endLabel(context, cmd);
}

} // namespace vkh
