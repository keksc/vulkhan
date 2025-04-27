#include "solidColor.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cassert>

#include "../../buffer.hpp"
#include "../../pipeline.hpp"
#include "../../swapChain.hpp"

namespace vkh {
void SolidColorSys::createBuffers() {
  BufferCreateInfo bufInfo{};
  bufInfo.instanceSize = sizeof(Vertex);
  bufInfo.instanceCount = maxVertexCount;
  bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  vertexBuffer = std::make_unique<Buffer>(context, bufInfo);
  vertexBuffer->map();
}
void SolidColorSys::createPipeline() {
  PipelineCreateInfo pipelineConfig{};
  GraphicsPipeline::enableAlphaBlending(pipelineConfig);
  pipelineConfig.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  pipelineConfig.renderPass = context.vulkan.swapChain->renderPass;
  pipelineConfig.attributeDescriptions = Vertex::getAttributeDescriptions();
  pipelineConfig.bindingDescriptions = Vertex::getBindingDescriptions();
  pipeline = std::make_unique<GraphicsPipeline>(
      context, "shaders/lines.vert.spv", "shaders/lines.frag.spv",
      pipelineConfig);
}
SolidColorSys::SolidColorSys(EngineContext &context) : System(context) {
  createBuffers();

  createPipeline();
}

void SolidColorSys::render(size_t verticesSize) {
  pipeline->bind(context.frameInfo.commandBuffer);

  VkBuffer buffers[] = {*vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(context.frameInfo.commandBuffer, 0, 1, buffers,
                         offsets);

  vkCmdDraw(context.frameInfo.commandBuffer, static_cast<uint32_t>(verticesSize), 1, 0, 0);
}
} // namespace vkh
