#include "solidColor.hpp"
#include <vulkan/vulkan_core.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cassert>

#include "../../buffer.hpp"
#include "../../descriptors.hpp"
#include "../../pipeline.hpp"
#include "../../swapChain.hpp"

namespace vkh {
void SolidColorSys::createBuffers() {
  linesVertexBuffer = std::make_unique<Buffer<LineVertex>>(
      context, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      maxLineVertexCount);
  linesVertexBuffer->map();
  trianglesVertexBuffer = std::make_unique<Buffer<TriangleVertex>>(
      context, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      maxTriangleVertexCount);
  trianglesVertexBuffer->map();

  trianglesIndexBuffer = std::make_unique<Buffer<uint32_t>>(
      context, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      maxTriangleIndexCount);
  trianglesIndexBuffer->map();
}
void SolidColorSys::createPipelines() {
  PipelineCreateInfo trianglesPipelineInfo{};
  trianglesPipelineInfo.layoutInfo.setLayoutCount = 1;
  trianglesPipelineInfo.layoutInfo.pSetLayouts = *setLayout;
  trianglesPipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  trianglesPipelineInfo.attributeDescriptions =
      TriangleVertex::getAttributeDescriptions();
  trianglesPipelineInfo.bindingDescriptions = TriangleVertex::getBindingDescriptions();
  trianglesPipelineInfo.vertpath = "shaders/solidColorTriangles.vert.spv";
  trianglesPipelineInfo.fragpath = "shaders/solidColorTriangles.frag.spv";
  trianglesPipelineInfo.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
  trianglePipeline =
      std::make_unique<GraphicsPipeline>(context, trianglesPipelineInfo);

  PipelineCreateInfo linesPipelineInfo{};
  linesPipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  linesPipelineInfo.attributeDescriptions = LineVertex::getAttributeDescriptions();
  linesPipelineInfo.bindingDescriptions = LineVertex::getBindingDescriptions();
  linesPipelineInfo.vertpath = "shaders/solidColorLines.vert.spv";
  linesPipelineInfo.fragpath = "shaders/solidColorLines.frag.spv";
  linesPipelineInfo.inputAssemblyInfo.topology =
      VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  linesPipelineInfo.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
  linesPipeline =
      std::make_unique<GraphicsPipeline>(context, linesPipelineInfo);
}
void SolidColorSys::createDescriptors() {
  setLayout = DescriptorSetLayout::Builder(context)
                  .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                              VK_SHADER_STAGE_FRAGMENT_BIT)
                  .build();
  VkDescriptorImageInfo info =
      image.getDescriptorInfo(context.vulkan.defaultSampler);
  DescriptorWriter(*setLayout, *context.vulkan.globalDescriptorPool)
      .writeImage(0, &info)
      .build(set);
}
SolidColorSys::SolidColorSys(EngineContext &context)
    : System(context), image(context, "textures/hud.png") {
  createDescriptors();
  createBuffers();
  createPipelines();
}

void SolidColorSys::render(size_t lineVerticesSize,
                           size_t triangleIndicesSize) {
  VkDeviceSize offsets[] = {0};

  linesPipeline->bind(context.frameInfo.cmd);
  VkBuffer linesBuffers[] = {*linesVertexBuffer};
  vkCmdBindVertexBuffers(context.frameInfo.cmd, 0, 1, linesBuffers, offsets);
  vkCmdBindDescriptorSets(context.frameInfo.cmd,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, *trianglePipeline, 0,
                          1, &set, 0, nullptr);
  vkCmdDraw(context.frameInfo.cmd, static_cast<uint32_t>(lineVerticesSize), 1,
            0, 0);

  trianglePipeline->bind(context.frameInfo.cmd);
  VkBuffer trianglesBuffers[] = {*trianglesVertexBuffer};
  vkCmdBindVertexBuffers(context.frameInfo.cmd, 0, 1, trianglesBuffers,
                         offsets);
  vkCmdBindIndexBuffer(context.frameInfo.cmd, *trianglesIndexBuffer, 0,
                       VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(context.frameInfo.cmd, triangleIndicesSize, 1, 0, 0, 0);
}
} // namespace vkh
