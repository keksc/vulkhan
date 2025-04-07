#include "hud.hpp"
#include <fmt/core.h>
#include <functional>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vulkan/vulkan_core.h>

#include <memory>
#include <vector>

#include "../../buffer.hpp"
#include "../../descriptors.hpp"
#include "../../image.hpp"
#include "../../pipeline.hpp"
#include "../../renderer.hpp"
#include "hudElements.hpp"

namespace vkh {
namespace hudSys {
DrawInfo drawInfo;
namespace font {} // namespace font
std::optional<std::reference_wrapper<View>> view;
std::unique_ptr<GraphicsPipeline> pipeline;
std::unique_ptr<DescriptorSetLayout> descriptorSetLayout;
VkDescriptorSet descriptorSet;

std::vector<VkVertexInputAttributeDescription> attributeDescriptions = {
    {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SolidColorVertex, position)},
    {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SolidColorVertex, color)}};
std::vector<VkVertexInputBindingDescription> bindingDescriptions = {
    {0, sizeof(SolidColorVertex), VK_VERTEX_INPUT_RATE_VERTEX}};

std::unique_ptr<Buffer> vertexBuffer;
std::unique_ptr<Buffer> indexBuffer;

std::shared_ptr<Image> fontAtlas;
const int maxHudRects = 10;
const int maxVertexCount = 4 * maxHudRects; // 4 vertices = 1 quad = 1 glyph
VkDeviceSize maxVertexSize = sizeof(SolidColorVertex) * maxVertexCount;

void createBuffers(EngineContext &context) {
  BufferCreateInfo bufInfo{};
  bufInfo.instanceSize = sizeof(SolidColorVertex);
  bufInfo.instanceCount = maxVertexCount;
  bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  bufInfo.memoryProperties =
      /*std::string text*/ VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  vertexBuffer = std::make_unique<Buffer>(context, bufInfo);
  vertexBuffer->map();

  bufInfo.instanceSize = sizeof(uint32_t);
  // bufInfo.instanceCount = maxIndexCount;
  bufInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  indexBuffer = std::make_unique<Buffer>(context, bufInfo);
  indexBuffer->map();
}

void createPipeline(EngineContext &context) {
  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.renderPass = renderer::getSwapChainRenderPass(context);
  pipelineInfo.attributeDescriptions = attributeDescriptions;
  pipelineInfo.bindingDescriptions = bindingDescriptions;
  pipeline = std::make_unique<GraphicsPipeline>(
      context, "shaders/solidColor.vert.spv", "shaders/solidColor.frag.spv",
      pipelineInfo);
}
void init(EngineContext &context) {
  textSys::init(context);

  createBuffers(context);

  createPipeline(context);
}
void addElementToDraw(std::shared_ptr<Element> element) {
  for (auto &child : element->children) {
    addElementToDraw(child);
  }
  element->addToDrawInfo(drawInfo);
}
void setView(View &newView) {
  view = newView;
  newView.setCurrent();
}
View &getView() { return view.value(); }
void update(EngineContext &context) {
  drawInfo.solidColorVertices.clear();
  drawInfo.solidColorIndices.clear();
  drawInfo.textVertices.clear();
  drawInfo.textIndices.clear();
  for (const auto &element : view.value().get()) {
    addElementToDraw(element);
  }
  vertexBuffer->write(drawInfo.solidColorVertices.data(),
                      drawInfo.solidColorVertices.size() *
                          sizeof(SolidColorVertex));
  indexBuffer->write(drawInfo.solidColorIndices.data(),
                     drawInfo.solidColorIndices.size() * sizeof(uint32_t));
  textSys::vertexBuffer->write(drawInfo.textVertices.data(),
                            drawInfo.textVertices.size() * sizeof(textSys::Vertex));
  textSys::indexBuffer->write(drawInfo.textIndices.data(),
                           drawInfo.textIndices.size() * sizeof(uint32_t));
}

void cleanup(EngineContext &context) {
  vertexBuffer->unmap();
  vertexBuffer = nullptr;
  indexBuffer->unmap();
  indexBuffer = nullptr;
  pipeline = nullptr;
  fontAtlas = nullptr;
  descriptorSetLayout = nullptr;
  textSys::cleanup(context);
}

void render(EngineContext &context) {
  if (!view.has_value())
    return;
  update(context);

  pipeline->bind(context.frameInfo.commandBuffer);

  VkBuffer buffers[] = {*vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(context.frameInfo.commandBuffer, 0, 1, buffers,
                         offsets);
  vkCmdBindIndexBuffer(context.frameInfo.commandBuffer, *indexBuffer, 0,
                       VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(context.frameInfo.commandBuffer,
                   drawInfo.solidColorIndices.size(), 1, 0, 0, 0);

  textSys::render(context, drawInfo.textIndices.size());
}
} // namespace hudSys
} // namespace vkh
