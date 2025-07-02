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
  PipelineCreateInfo pipelineInfo{};
  GraphicsPipeline::enableAlphaBlending(pipelineInfo);
  pipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  pipelineInfo.attributeDescriptions = Vertex::getAttributeDescriptions();
  pipelineInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  pipelineInfo.vertpath = "shaders/solidColor.vert.spv";
  pipelineInfo.fragpath = "shaders/solidColor.frag.spv";
  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo);
}
SolidColorSys::SolidColorSys(EngineContext &context) : System(context) {
  createBuffers();

  createPipeline();
}

void SolidColorSys::render(size_t lineVerticesSize,
                           size_t triangleVerticesSize) {
  pipeline->bind(context.frameInfo.cmd);

  VkBuffer trianglesBuffers[] = {*trianglesVertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(context.frameInfo.cmd, 0, 1, trianglesBuffers,
                         offsets);

  vkCmdDraw(context.frameInfo.cmd, static_cast<uint32_t>(triangleVerticesSize),
            1, 0, 0);
}
} // namespace vkh
