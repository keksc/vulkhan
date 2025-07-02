#include "solidColor.hpp"
#include <vulkan/vulkan_core.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cassert>

#include "../../buffer.hpp"
#include "../../pipeline.hpp"
#include "../../swapChain.hpp"

namespace vkh {
void SolidColorSys::createBuffers() {
  linesVertexBuffer = std::make_unique<Buffer<Vertex>>(
      context, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      maxLineVertexCount);
  linesVertexBuffer->map();
  trianglesVertexBuffer = std::make_unique<Buffer<Vertex>>(
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
void SolidColorSys::createPipeline() {
  PipelineCreateInfo trianglesPipelineInfo{};
  trianglesPipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  trianglesPipelineInfo.attributeDescriptions =
      Vertex::getAttributeDescriptions();
  trianglesPipelineInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  trianglesPipelineInfo.vertpath = "shaders/solidColor.vert.spv";
  trianglesPipelineInfo.fragpath = "shaders/solidColor.frag.spv";
  trianglePipeline =
      std::make_unique<GraphicsPipeline>(context, trianglesPipelineInfo);

  PipelineCreateInfo linesPipelineInfo{};
  linesPipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  linesPipelineInfo.attributeDescriptions = Vertex::getAttributeDescriptions();
  linesPipelineInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  linesPipelineInfo.vertpath = "shaders/solidColor.vert.spv";
  linesPipelineInfo.fragpath = "shaders/solidColor.frag.spv";
  linesPipelineInfo.inputAssemblyInfo.topology =
      VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  linesPipeline =
      std::make_unique<GraphicsPipeline>(context, linesPipelineInfo);
}
SolidColorSys::SolidColorSys(EngineContext &context) : System(context) {
  createBuffers();

  createPipeline();
}

void SolidColorSys::render(size_t lineVerticesSize,
                           size_t triangleIndicesSize) {
  VkDeviceSize offsets[] = {0};

  linesPipeline->bind(context.frameInfo.cmd);
  VkBuffer linesBuffers[] = {*linesVertexBuffer};
  vkCmdBindVertexBuffers(context.frameInfo.cmd, 0, 1, linesBuffers, offsets);
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
